#include "scanwise/types.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <optional>
#include <vector>

namespace scanwise {



// Order 4 points as top-left, top-right, bottom-right, bottom-left.
static quad order_points(const std::vector<cv::Point2f>& pts) {
    cv::Point2f tl = pts[0], tr = pts[0], br = pts[0], bl = pts[0];
    float min_sum = pts[0].x + pts[0].y;
    float max_sum = min_sum;
    float min_diff = pts[0].x - pts[0].y;
    float max_diff = min_diff;

    for (const auto& p : pts) {
        float s = p.x + p.y;
        float d = p.x - p.y;
        if (s < min_sum)  { min_sum = s;  tl = p; }
        if (s > max_sum)  { max_sum = s;  br = p; }
        if (d > max_diff) { max_diff = d; tr = p; }
        if (d < min_diff) { min_diff = d; bl = p; }
    }
    return quad{{tl.x, tl.y}, {tr.x, tr.y}, {br.x, br.y}, {bl.x, bl.y}};
}

static double quad_area(const std::vector<cv::Point2f>& pts) {
    return cv::contourArea(pts);
}

static cv::Point2f quad_centroid(const std::vector<cv::Point2f>& pts) {
    cv::Point2f c{0, 0};
    for (const auto& p : pts) c += p;
    return c * (1.0f / 4.0f);
}

static double edge_alignment_score(const std::vector<cv::Point2f>& pts,
                                   const cv::Mat& gray, int samples_per_side = 64) {
    if (gray.empty()) return 0.0;

    cv::Mat gx, gy, mag;
    cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);

    double total = 0.0;
    double count = 0.0;
    for (int i = 0; i < 4; ++i) {
        cv::Point2f a = pts[i];
        cv::Point2f b = pts[(i + 1) % 4];
        for (int s = 1; s < samples_per_side; ++s) {
            float t = static_cast<float>(s) / samples_per_side;
            cv::Point2f p = a * (1.0f - t) + b * t;
            int x = static_cast<int>(std::round(p.x));
            int y = static_cast<int>(std::round(p.y));
            if (x < 0 || x >= gray.cols || y < 0 || y >= gray.rows) continue;
            total += mag.at<float>(y, x);
            count += 1.0;
        }
    }
    if (count < 1.0) return 0.0;
    double mean = total / count;
    return std::min(1.0, mean / 80.0);
}

static double score_quad(const std::vector<cv::Point2f>& pts, int img_w, int img_h,
                         const detection_options& opts,
                         const cv::Mat& gray = cv::Mat()) {
    if (pts.size() != 4) return 0.0;

    double area = quad_area(pts);
    double img_area = double(img_w) * img_h;
    double area_ratio = area / img_area;

    if (area_ratio < opts.score_area_min || area_ratio > opts.score_area_max) return 0.0;

    // Reject degenerate quads: any interior angle < 30 deg or > 150 deg.
    const double cos_extreme = 0.866; // cos(30 deg)
    for (int i = 0; i < 4; ++i) {
        cv::Point2f a = pts[i];
        cv::Point2f b = pts[(i + 1) % 4];
        cv::Point2f c = pts[(i + 2) % 4];
        cv::Point2f ab = b - a;
        cv::Point2f bc = c - b;
        double dot = ab.x * bc.x + ab.y * bc.y;
        double mag = std::sqrt((ab.x*ab.x + ab.y*ab.y) * (bc.x*bc.x + bc.y*bc.y));
        if (mag > 0) {
            double cos_angle = std::abs(dot / mag);
            if (cos_angle > cos_extreme) return 0.0;
        }
    }

    double size_score = area_ratio;
    if (area_ratio > 0.85) {
        size_score = 0.85 * (opts.score_area_max - area_ratio) / (opts.score_area_max - 0.85);
    } else if (area_ratio < opts.score_area_min) {
        size_score = area_ratio * (area_ratio / opts.score_area_min); // double penalty
    }

    double aspect_score = 1.0;
    {
        double side_lengths[4];
        for (int i = 0; i < 4; ++i) {
            cv::Point2f a = pts[i];
            cv::Point2f b = pts[(i + 1) % 4];
            side_lengths[i] = std::sqrt((a.x - b.x) * (a.x - b.x) +
                                        (a.y - b.y) * (a.y - b.y));
        }
        double long_side = std::max({side_lengths[0], side_lengths[1],
                                     side_lengths[2], side_lengths[3]});
        double short_side = std::max(1.0, std::min({side_lengths[0], side_lengths[1],
                                                    side_lengths[2], side_lengths[3]}));
        double ar = long_side / short_side;
        if (ar > opts.score_aspect_extreme) aspect_score = 0.1;
        else if (ar > opts.score_aspect_mid) aspect_score = 0.5;
        else if (ar > opts.score_aspect_start) aspect_score = 0.85;
    }

    double centrality_score = 1.0;
    {
        cv::Point2f c = quad_centroid(pts);
        double dx = (c.x - img_w * 0.5) / (img_w * 0.5);
        double dy = (c.y - img_h * 0.5) / (img_h * 0.5);
        double dist = std::sqrt(dx * dx + dy * dy);
        centrality_score = std::max(0.3, 1.0 - dist * 0.4);
    }

    double alignment_score = gray.empty() ? 1.0 : edge_alignment_score(pts, gray);

    double geometric_score = 1.0;
    {
        cv::Point2f edges[4];
        double lengths[4];
        for (int i = 0; i < 4; ++i) {
            edges[i] = pts[(i + 1) % 4] - pts[i];
            lengths[i] = std::hypot(edges[i].x, edges[i].y);
        }
        
        double max_angle_dev = 0.0;
        for (int i = 0; i < 4; ++i) {
            cv::Point2f e1 = edges[i];
            cv::Point2f e2 = pts[i] - pts[(i + 3) % 4]; // Previous edge reversed
            if (lengths[i] > 0 && lengths[(i + 3) % 4] > 0) {
                double cos_theta = (e1.x * e2.x + e1.y * e2.y) / (lengths[i] * lengths[(i + 3) % 4]);
                cos_theta = std::clamp(cos_theta, -1.0, 1.0);
                double angle_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979;
                max_angle_dev = std::max(max_angle_dev, std::abs(90.0 - angle_deg));
            }
        }
        
        double max_parallel_dev = 0.0;
        for (int i = 0; i < 2; ++i) {
            cv::Point2f e1 = edges[i];
            cv::Point2f e2 = edges[i + 2];
            if (lengths[i] > 0 && lengths[i + 2] > 0) {
                // They should be opposite direction
                double cos_theta = -(e1.x * e2.x + e1.y * e2.y) / (lengths[i] * lengths[i + 2]);
                cos_theta = std::clamp(cos_theta, -1.0, 1.0);
                double angle_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979;
                max_parallel_dev = std::max(max_parallel_dev, angle_deg);
            }
        }
        
        // Soft penalties so we don't completely kill valid extreme perspectives
        double angle_score = std::max(0.4, 1.0 - (max_angle_dev / 60.0));
        double parallel_score = std::max(0.4, 1.0 - (max_parallel_dev / 60.0));
        geometric_score = angle_score * parallel_score;
    }

    double final_score = size_score * aspect_score * centrality_score * alignment_score * geometric_score;
    std::cout << "score_quad: final=" << final_score
              << " (area=" << area_ratio 
              << ", size=" << size_score 
              << ", aspect=" << aspect_score 
              << ", cent=" << centrality_score 
              << ", align=" << alignment_score 
              << ", geo=" << geometric_score
              << ")\n";

    return final_score;
}

static void draw_quad(cv::Mat& img, const std::vector<cv::Point2f>& pts,
                      const cv::Scalar& color, int thickness = 2) {
    if (pts.size() != 4) return;
    for (int i = 0; i < 4; ++i) {
        cv::line(img, pts[i], pts[(i + 1) % 4], color, thickness);
    }
    for (const auto& p : pts) {
        cv::circle(img, p, 4, color, cv::FILLED);
    }
}

// Try to find the best 4-sided convex contour in the given edge/mask map.
static bool find_best_quad(const cv::Mat& edges, double min_area, quad& out,
                           float scale, int orig_w, int orig_h,
                           const detection_options& opts,
                           const cv::Mat& orig_gray = cv::Mat()) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    std::sort(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) > cv::contourArea(b);
    });


    double best_score = 0.0;
    quad best_quad{};

    int limit = std::min<int>(40, static_cast<int>(contours.size()));
    for (int i = 0; i < limit; ++i) {
        double area = cv::contourArea(contours[i]);
        if (area < min_area) break;

        double peri = cv::arcLength(contours[i], true);

        // Dynamic epsilon: binary search to find an epsilon that yields exactly 4 vertices
        double eps_min = 0.005;
        double eps_max = 0.15;
        
        for (int iter = 0; iter < 10; ++iter) {
            double eps = (eps_min + eps_max) / 2.0;
            std::vector<cv::Point> approx;
            cv::approxPolyDP(contours[i], approx, eps * peri, true);

            if (approx.size() == 4) {
                if (cv::isContourConvex(approx)) {
                    std::vector<cv::Point2f> pts;
                    for (const auto& p : approx) {
                        pts.push_back(cv::Point2f(p.x / scale, p.y / scale));
                    }

                    double sc = score_quad(pts, orig_w, orig_h, opts, orig_gray);
                    if (sc > best_score) {
                        best_score = sc;
                        best_quad = order_points(pts);
                    }
                }
                break; // Found 4 vertices, stop searching for this contour
            } else if (approx.size() > 4) {
                eps_min = eps; // Too many vertices, need a stronger approximation
            } else {
                eps_max = eps; // Too few vertices, need a weaker approximation
            }
        }
    }

    if (best_score > 0.0) {
        out = best_quad;
        return true;
    }
    return false;
}

static quad normalize_quad(const quad& q, int w, int h) {
    return quad{
        {q.tl.x / w, q.tl.y / h},
        {q.tr.x / w, q.tr.y / h},
        {q.br.x / w, q.br.y / h},
        {q.bl.x / w, q.bl.y / h}
    };
}

static quad denormalize_quad(const quad& q, int w, int h) {
    return quad{
        {q.tl.x * w, q.tl.y * h},
        {q.tr.x * w, q.tr.y * h},
        {q.br.x * w, q.br.y * h},
        {q.bl.x * w, q.bl.y * h}
    };
}

// ============================================================================
// Hough Lines Intersection Strategy
// ============================================================================

static bool intersect_lines(float rho1, float theta1, float rho2, float theta2,
                            cv::Point2f& out) {
    double ct1 = std::cos(theta1), st1 = std::sin(theta1);
    double ct2 = std::cos(theta2), st2 = std::sin(theta2);
    double det = ct1 * st2 - ct2 * st1;
    if (std::abs(det) < 1e-6) return false;
    out.x = static_cast<float>((rho1 * st2 - rho2 * st1) / det);
    out.y = static_cast<float>((rho2 * ct1 - rho1 * ct2) / det);
    return true;
}

static float normalize_angle(float theta) {
    while (theta < 0) theta += static_cast<float>(CV_PI);
    while (theta >= static_cast<float>(CV_PI)) theta -= static_cast<float>(CV_PI);
    return theta;
}

static float angle_diff_pi2(float a, float b) {
    float d = std::abs(normalize_angle(a) - normalize_angle(b));
    if (d > static_cast<float>(CV_PI) / 2)
        d = static_cast<float>(CV_PI) - d;
    return d;
}

struct LineCluster {
    float avg_rho = 0;
    float avg_theta = 0;
    float total_votes = 0;
    int count = 0;
};

static std::vector<LineCluster> cluster_lines(const std::vector<cv::Vec2f>& lines,
                                               float angle_thresh) {
    std::vector<LineCluster> clusters;
    for (const auto& l : lines) {
        float rho = l[0];
        float theta = normalize_angle(l[1]);

        bool merged = false;
        for (auto& c : clusters) {
            if (angle_diff_pi2(c.avg_theta, theta) < angle_thresh) {
                float w = c.total_votes;
                c.avg_rho = (c.avg_rho * w + rho) / (w + 1);
                c.avg_theta = (c.avg_theta * w + theta) / (w + 1);
                c.total_votes += 1;
                c.count++;
                merged = true;
                break;
            }
        }
        if (!merged) {
            clusters.push_back({rho, theta, 1.0f, 1});
        }
    }
    return clusters;
}

static bool hough_to_segment(float rho, float theta, int w, int h,
                             cv::Point2f& p1, cv::Point2f& p2) {
    double ct = std::cos(theta), st = std::sin(theta);
    std::vector<cv::Point2f> pts;
    auto add = [&](double x, double y) {
        if (x >= -1e-3 && x <= w + 1e-3 && y >= -1e-3 && y <= h + 1e-3)
            pts.emplace_back(static_cast<float>(x), static_cast<float>(y));
    };
    if (std::abs(st) > 1e-6) add(0.0, rho / st);
    if (std::abs(st) > 1e-6) add(w, (rho - w * ct) / st);
    if (std::abs(ct) > 1e-6) add(rho / ct, 0.0);
    if (std::abs(ct) > 1e-6) add((rho - h * st) / ct, h);
    if (pts.size() < 2) return false;
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        if (std::abs(a.x - b.x) > 1e-3f) return a.x < b.x;
        return a.y < b.y - 1e-3f;
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return std::hypot(a.x - b.x, a.y - b.y) < 3.0f;
    }), pts.end());
    if (pts.size() < 2) return false;
    p1 = pts.front();
    p2 = pts.back();
    return true;
}

static float segment_length(const cv::Point2f& a, const cv::Point2f& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

static void draw_hough_lines(cv::Mat& img, const std::vector<cv::Vec2f>& lines,
                             const cv::Scalar& color) {
    for (const auto& l : lines) {
        cv::Point2f p1, p2;
        if (hough_to_segment(l[0], l[1], img.cols, img.rows, p1, p2)) {
            cv::line(img, p1, p2, color, 1);
        }
    }
}

static bool find_quad_hough(const cv::Mat& edges, int img_w, int img_h,
                            float scale, int orig_w, int orig_h,
                            const detection_options& opts,
                            const cv::Mat& orig_gray,
                            quad& out,
                            std::vector<cv::Vec2f>* out_lines = nullptr) {
    int diag = static_cast<int>(std::sqrt(img_w * img_w + img_h * img_h));
    int threshold = std::max(opts.hough_threshold_min,
                             diag / opts.hough_threshold_divisor);

    std::vector<cv::Vec2f> lines;
    cv::HoughLines(edges, lines, 1, CV_PI / 180, threshold);

    if (lines.size() < 8) {
        cv::HoughLines(edges, lines, 1, CV_PI / 180,
                       std::max(opts.hough_threshold_min / 2, threshold / 2));
    }
    if (lines.size() < 4) return false;

    std::vector<cv::Vec2f> filtered;
    float min_seg_len = std::min(img_w, img_h) * opts.hough_min_segment_ratio;
    for (const auto& l : lines) {
        cv::Point2f p1, p2;
        if (hough_to_segment(l[0], l[1], img_w, img_h, p1, p2)) {
            if (segment_length(p1, p2) >= min_seg_len) {
                filtered.push_back(l);
            }
        }
    }
    if (filtered.size() < 4) filtered = lines;

    auto clusters = cluster_lines(filtered, opts.hough_angle_cluster_thresh);

    if (clusters.size() < 2) {
        return false;
    }

    std::sort(clusters.begin(), clusters.end(), [](const LineCluster& a, const LineCluster& b) {
        return a.total_votes > b.total_votes;
    });

    int dir1 = 0, dir2 = -1;
    for (int i = 1; i < static_cast<int>(clusters.size()); ++i) {
        float angle_diff = angle_diff_pi2(clusters[i].avg_theta, clusters[dir1].avg_theta);
        if (angle_diff > 0.5f && angle_diff < (static_cast<float>(CV_PI) - 0.5f)) {
            dir2 = i;
            break;
        }
    }
    if (dir2 < 0) {
        return false;
    }

    float theta1 = normalize_angle(clusters[dir1].avg_theta);
    float theta2 = normalize_angle(clusters[dir2].avg_theta);

    std::vector<cv::Vec2f> group1, group2;
    for (const auto& l : filtered) {
        float theta = normalize_angle(l[1]);
        if (angle_diff_pi2(theta, theta1) < 0.3f) group1.push_back(l);
        else if (angle_diff_pi2(theta, theta2) < 0.3f) group2.push_back(l);
    }

    if (group1.size() < 2 || group2.size() < 2) {
        return false;
    }

    auto get_top_rhos = [&](std::vector<cv::Vec2f>& group) -> std::vector<cv::Vec2f> {
        std::vector<std::pair<float, float>> rho_clusters;
        std::vector<int> rho_counts;
        for (const auto& l : group) {
            bool merged = false;
            for (size_t i = 0; i < rho_clusters.size(); ++i) {
                if (std::abs(l[0] - rho_clusters[i].first) < opts.hough_rho_cluster_thresh) {
                    float w = static_cast<float>(rho_counts[i]);
                    rho_clusters[i].first = (rho_clusters[i].first * w + l[0]) / (w + 1);
                    rho_clusters[i].second = (rho_clusters[i].second * w + l[1]) / (w + 1);
                    rho_counts[i]++;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                rho_clusters.push_back({l[0], l[1]});
                rho_counts.push_back(1);
            }
        }

        std::vector<int> indices(rho_clusters.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            return rho_counts[a] > rho_counts[b];
        });

        int limit = std::min<int>(15, static_cast<int>(indices.size()));
        std::vector<cv::Vec2f> top_lines;
        for (int i = 0; i < limit; ++i) {
            top_lines.push_back(cv::Vec2f(rho_clusters[indices[i]].first, rho_clusters[indices[i]].second));
        }
        return top_lines;
    };

    auto top_dir1 = get_top_rhos(group1);
    auto top_dir2 = get_top_rhos(group2);

    if (top_dir1.size() < 2 || top_dir2.size() < 2) {
        return false;
    }

    double best_score = 0.0;
    quad best_quad{};

    for (size_t i1 = 0; i1 < top_dir1.size(); ++i1) {
        for (size_t j1 = i1 + 1; j1 < top_dir1.size(); ++j1) {
            for (size_t i2 = 0; i2 < top_dir2.size(); ++i2) {
                for (size_t j2 = i2 + 1; j2 < top_dir2.size(); ++j2) {
                    cv::Point2f corners[4];
                    bool ok = true;
                    ok &= intersect_lines(top_dir1[i1][0], top_dir1[i1][1], top_dir2[i2][0], top_dir2[i2][1], corners[0]);
                    ok &= intersect_lines(top_dir1[i1][0], top_dir1[i1][1], top_dir2[j2][0], top_dir2[j2][1], corners[1]);
                    ok &= intersect_lines(top_dir1[j1][0], top_dir1[j1][1], top_dir2[i2][0], top_dir2[i2][1], corners[2]);
                    ok &= intersect_lines(top_dir1[j1][0], top_dir1[j1][1], top_dir2[j2][0], top_dir2[j2][1], corners[3]);
                    if (!ok) continue;

                    float margin = img_w * 0.15f;
                    bool in_bounds = true;
                    for (auto& c : corners) {
                        if (c.x < -margin || c.y < -margin || c.x > img_w + margin || c.y > img_h + margin) {
                            in_bounds = false;
                            break;
                        }
                    }
                    if (!in_bounds) continue;

                    std::vector<cv::Point2f> pts(4);
                    for (int k = 0; k < 4; ++k) {
                        pts[k] = cv::Point2f(
                            std::clamp(corners[k].x, 0.0f, static_cast<float>(img_w)) / scale,
                            std::clamp(corners[k].y, 0.0f, static_cast<float>(img_h)) / scale
                        );
                    }

                    detection_options fast_opts = opts;
                    double pre_sc = score_quad(pts, orig_w, orig_h, fast_opts, cv::Mat());
                    if (pre_sc < 0.01) continue;

                    double sc = score_quad(pts, orig_w, orig_h, opts, orig_gray);
                    if (sc > best_score) {
                        best_score = sc;
                        best_quad = order_points(pts);
                    }
                }
            }
        }
    }

    if (best_score > 0.0) {
        out = best_quad;
        if (out_lines) *out_lines = lines;
        return true;
    }
    return false;
}

// ============================================================================
// Corner Refinement
// ============================================================================

static cv::Point2f refine_corner(const cv::Point2f& corner,
                                 const cv::Point2f& prev, const cv::Point2f& next,
                                 const cv::Mat& gray, int radius) {
    cv::Point2f e1 = prev - corner;
    cv::Point2f e2 = next - corner;
    float len1 = std::hypot(e1.x, e1.y);
    float len2 = std::hypot(e2.x, e2.y);
    if (len1 < 1e-3f || len2 < 1e-3f) return corner;
    e1 /= len1;
    e2 /= len2;

    cv::Mat gx, gy, mag;
    cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);

    int cx = static_cast<int>(std::round(corner.x));
    int cy = static_cast<int>(std::round(corner.y));

    cv::Point2f best = corner;
    double best_score = 0.0;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int x = cx + dx;
            int y = cy + dy;
            if (x < 0 || x >= gray.cols || y < 0 || y >= gray.rows) continue;

            cv::Point2f cand(static_cast<float>(x), static_cast<float>(y));
            cv::Point2f v1 = prev - cand;
            cv::Point2f v2 = next - cand;
            float l1 = std::hypot(v1.x, v1.y);
            float l2 = std::hypot(v2.x, v2.y);
            if (l1 < 1e-3f || l2 < 1e-3f) continue;
            v1 /= l1;
            v2 /= l2;

            float g = mag.at<float>(y, x);
            float align1 = std::abs(v1.x * e1.x + v1.y * e1.y);
            float align2 = std::abs(v2.x * e2.x + v2.y * e2.y);
            float dist_penalty = 1.0f - static_cast<float>(std::hypot(dx, dy)) / (radius * 1.5f);
            if (dist_penalty < 0.0f) dist_penalty = 0.0f;

            double s = g * (align1 + align2 + 0.25) * dist_penalty;
            if (s > best_score) {
                best_score = s;
                best = cand;
            }
        }
    }
    return best;
}

static quad refine_quad(const quad& q, const cv::Mat& gray, int radius) {
    std::vector<cv::Point2f> pts = {
        {q.tl.x, q.tl.y},
        {q.tr.x, q.tr.y},
        {q.br.x, q.br.y},
        {q.bl.x, q.bl.y}
    };
    std::vector<cv::Point2f> refined(4);
    for (int i = 0; i < 4; ++i) {
        cv::Point2f prev = pts[(i + 3) % 4];
        cv::Point2f next = pts[(i + 1) % 4];
        refined[i] = refine_corner(pts[i], prev, next, gray, radius);
    }
    return order_points(refined);
}

// ============================================================================
// Core Detection at a Single Scale
// ============================================================================

struct DetectionResult {
    quad q;
    double score = 0.0;
    int strategy = -1;
};

static std::optional<DetectionResult> detect_at_scale(
    const cv::Mat& bgr, int orig_w, int orig_h, const cv::Mat& orig_gray,
    int target_size, const detection_options& opts) {

    float scale = 1.0f;
    int maxDim = std::max(orig_w, orig_h);
    if (maxDim > target_size) {
        scale = static_cast<float>(target_size) / maxDim;
    }

    cv::Mat small;
    if (scale < 1.0f) {
        cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        small = bgr;
    }

    int sw = small.cols;
    int sh = small.rows;

    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);

    if (opts.use_clahe) {
        auto clahe = cv::createCLAHE(opts.clahe_clip_limit, cv::Size(opts.clahe_grid_size, opts.clahe_grid_size));
        clahe->apply(gray, gray);
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(7, 7), 0);
    cv::Mat otsu_dst;
    double otsu = cv::threshold(blurred, otsu_dst, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    double canny_low = std::max(10.0, otsu * opts.canny_low_multiplier);
    double canny_high = std::min(255.0, otsu * opts.canny_high_multiplier);

    double min_area = sw * sh * opts.min_area_ratio;

    cv::Mat bilateral;
    cv::bilateralFilter(small, bilateral, 9, 75, 75);
    cv::Mat bgray;
    cv::cvtColor(bilateral, bgray, cv::COLOR_BGR2GRAY);
    cv::Mat bblurred;
    cv::GaussianBlur(bgray, bblurred, cv::Size(5, 5), 0);
    cv::Mat bilateral_edges;
    cv::Canny(bblurred, bilateral_edges, canny_low, canny_high);

    cv::Mat canny_edges;
    cv::Canny(blurred, canny_edges, canny_low, canny_high);

    auto try_strategy = [&](int idx, const cv::Mat& edges,
                            const std::string& name) -> std::optional<DetectionResult> {
        quad result;
        bool ok = false;
        std::vector<cv::Vec2f> hough_lines;
        if (idx == 1 || idx == 2) {
            ok = find_quad_hough(edges, sw, sh, scale, orig_w, orig_h, opts,
                                 orig_gray, result, nullptr);
        } else {
            ok = find_best_quad(edges, min_area, result, scale, orig_w, orig_h,
                                opts, orig_gray);
        }
        if (!ok) return std::nullopt;

        std::vector<cv::Point2f> pts = {
            {result.tl.x, result.tl.y},
            {result.tr.x, result.tr.y},
            {result.br.x, result.br.y},
            {result.bl.x, result.bl.y}
        };
        double sc = score_quad(pts, orig_w, orig_h, opts, orig_gray);
        if (sc <= 0.0) return std::nullopt;



        return DetectionResult{normalize_quad(result, orig_w, orig_h), sc, idx};
    };

    std::vector<DetectionResult> results;

    // Strategy 1: Hough on bilateral edges.
    {
        cv::Mat edges = bilateral_edges.clone();
        cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
        auto r = try_strategy(1, edges, "hough_bilateral");
        if (r) results.push_back(*r);
    }

    // Strategy 2: Hough on standard Canny edges.
    {
        cv::Mat edges;
        cv::Canny(blurred, edges, canny_low, canny_high);
        cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
        auto r = try_strategy(2, edges, "hough_canny");
        if (r) results.push_back(*r);
    }

    // Strategy 3: Bilateral filter + Canny contours.
    {
        cv::Mat edges = bilateral_edges.clone();
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
        cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);
        cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
        auto r = try_strategy(3, edges, "contour_bilateral");
        if (r) results.push_back(*r);
    }

    // Strategy 4: Standard Canny + morphological closing.
    {
        cv::Mat edges;
        cv::Canny(blurred, edges, canny_low, canny_high);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
        cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);
        cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
        auto r = try_strategy(4, edges, "contour_canny");
        if (r) results.push_back(*r);
    }

    // Strategy 5: Stronger blur + relaxed thresholds.
    {
        cv::Mat blurred2;
        cv::GaussianBlur(gray, blurred2, cv::Size(15, 15), 0);
        cv::Mat edges;
        cv::Canny(blurred2, edges, canny_low * 0.4, canny_high * 0.7);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7));
        cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);
        cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
        auto r = try_strategy(5, edges, "contour_blur");
        if (r) results.push_back(*r);
    }

    // Strategy 6: Color-distance segmentation.
    {
        cv::Mat bf;
        cv::bilateralFilter(small, bf, 15, 80, 80);
        cv::Mat lab;
        cv::cvtColor(bf, lab, cv::COLOR_BGR2Lab);
        cv::Vec3b center_col = lab.at<cv::Vec3b>(sh / 2, sw / 2);
        cv::Mat mask = cv::Mat::zeros(sh, sw, CV_8UC1);
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                cv::Vec3b px = lab.at<cv::Vec3b>(y, x);
                double dist = std::sqrt(
                    double(px[0] - center_col[0]) * (px[0] - center_col[0]) +
                    double(px[1] - center_col[1]) * (px[1] - center_col[1]) +
                    double(px[2] - center_col[2]) * (px[2] - center_col[2]));
                if (dist < opts.color_dist_thresh) mask.at<uint8_t>(y, x) = 255;
            }
        }
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9));
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                         cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
        auto r = try_strategy(6, mask, "color_segment");
        if (r) results.push_back(*r);
    }

    // Strategy 7: Adaptive threshold.
    {
        cv::Mat thresh;
        cv::adaptiveThreshold(blurred, thresh, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY_INV, 15, 5);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(11, 11));
        cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);
        auto r = try_strategy(7, thresh, "adaptive_threshold");
        if (r) results.push_back(*r);
    }

    if (results.empty()) return std::nullopt;

    auto best = std::max_element(results.begin(), results.end(),
        [](const DetectionResult& a, const DetectionResult& b) {
            return a.score < b.score;
        });
    return *best;
}

// ============================================================================
// Public entry points
// ============================================================================

quad detect_corners_impl(const page& pg) {
    if (pg.width <= 0 || pg.height <= 0 || pg.rgba.empty()) {
        return {{0,0},{1,0},{1,1},{0,1}};
    }

    cv::Mat bgr(pg.height, pg.width, CV_8UC3);
    for (int y = 0; y < pg.height; ++y) {
        for (int x = 0; x < pg.width; ++x) {
            const uint8_t* p = pg.rgba.data() + (y * pg.width + x) * 4;
            bgr.at<cv::Vec3b>(y, x) = cv::Vec3b(p[2], p[1], p[0]);
        }
    }

    cv::Mat orig_gray;
    cv::cvtColor(bgr, orig_gray, cv::COLOR_BGR2GRAY);

    const detection_options& opts = pg.det_opts;
    std::optional<DetectionResult> best;
    
    for (int target : opts.scales) {
        auto r = detect_at_scale(bgr, pg.width, pg.height, orig_gray, target, opts);
        if (!r) continue;
        if (!best || r->score > best->score) {
            best = r;
        }
    }

    quad result;
    if (best) {
        result = best->q;
    } else {
        const float margin = 0.05f;
        return quad{{margin, margin},
                    {1.0f - margin, margin},
                    {1.0f - margin, 1.0f - margin},
                    {margin, 1.0f - margin}};
    }

    quad pixel_quad = denormalize_quad(result, pg.width, pg.height);
    quad refined_pixel = refine_quad(pixel_quad, orig_gray, opts.corner_refinement_radius);
    return normalize_quad(refined_pixel, pg.width, pg.height);
}

} // namespace scanwise
