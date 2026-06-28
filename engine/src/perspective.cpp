#include "scanwise/types.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace scanwise {

static float distance(const point& a, const point& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

static double compute_true_aspect_ratio(const cv::Point2f pts[4], int img_w, int img_h) {
    // Move origin to center of image
    double cx = img_w / 2.0;
    double cy = img_h / 2.0;

    auto make_ray = [&](const cv::Point2f& p) -> cv::Vec3d {
        return cv::Vec3d(p.x - cx, p.y - cy, 1.0);
    };

    // Homogeneous lines for opposite edges
    cv::Vec3d m1 = make_ray(pts[0]).cross(make_ray(pts[1])); // Top
    cv::Vec3d m2 = make_ray(pts[3]).cross(make_ray(pts[2])); // Bottom
    cv::Vec3d m3 = make_ray(pts[0]).cross(make_ray(pts[3])); // Left
    cv::Vec3d m4 = make_ray(pts[1]).cross(make_ray(pts[2])); // Right

    // Vanishing points
    cv::Vec3d vp1 = m1.cross(m2);
    cv::Vec3d vp2 = m3.cross(m4);

    if (vp1[2] == 0 || vp2[2] == 0) return 0.0;
    
    vp1 /= vp1[2];
    vp2 /= vp2[2];

    // Compute focal length squared (assuming zero skew and principal point at center)
    double f_squared = -(vp1[0] * vp2[0] + vp1[1] * vp2[1]);
    if (f_squared <= 0) return 0.0;

    double f = std::sqrt(f_squared);

    // 3D rays from camera to corners
    cv::Vec3d r0(pts[0].x - cx, pts[0].y - cy, f);
    cv::Vec3d r1(pts[1].x - cx, pts[1].y - cy, f);
    cv::Vec3d r2(pts[2].x - cx, pts[2].y - cy, f);
    cv::Vec3d r3(pts[3].x - cx, pts[3].y - cy, f);

    // Enforce 3D rectangle (P0 - P1 + P2 - P3 = 0)
    // [r0, -r1, r2] * [k0, k1, k2]^T = r3
    cv::Matx33d R(
        r0[0], -r1[0], r2[0],
        r0[1], -r1[1], r2[1],
        r0[2], -r1[2], r2[2]
    );

    cv::Vec3d K;
    if (!cv::solve(R, r3, K)) return 0.0;

    cv::Vec3d P0 = r0 * K[0];
    cv::Vec3d P1 = r1 * K[1];
    cv::Vec3d P2 = r2 * K[2];
    cv::Vec3d P3 = r3 * 1.0;

    double width = cv::norm(P0 - P1);
    double height = cv::norm(P0 - P3);

    if (height == 0) return 0.0;
    return width / height;
}

cv::Mat warp_perspective_impl(const cv::Mat& src, const quad& crop) {
    if (src.empty()) return {};

    int W = src.cols;
    int H = src.rows;

    // Map normalized crop to pixel coordinates
    cv::Point2f src_pts[4] = {
        {crop.tl.x * W, crop.tl.y * H},
        {crop.tr.x * W, crop.tr.y * H},
        {crop.br.x * W, crop.br.y * H},
        {crop.bl.x * W, crop.bl.y * H}
    };

    // Compute naive dimensions from Euclidean edge lengths
    float top_w    = distance(crop.tl, crop.tr) * W;
    float bot_w    = distance(crop.bl, crop.br) * W;
    float left_h   = distance(crop.tl, crop.bl) * H;
    float right_h  = distance(crop.tr, crop.br) * H;

    int naive_w = std::max(1, int(std::max(top_w, bot_w)));
    int naive_h = std::max(1, int(std::max(left_h, right_h)));

    int out_w = naive_w;
    int out_h = naive_h;

    // Try to compute true mathematical aspect ratio
    double true_aspect = compute_true_aspect_ratio(src_pts, W, H);
    if (true_aspect > 0.0) {
        // Preserve overall pixel area to maintain resolution quality
        double area = static_cast<double>(naive_w) * naive_h;
        out_w = std::max(1, static_cast<int>(std::round(std::sqrt(area * true_aspect))));
        out_h = std::max(1, static_cast<int>(std::round(std::sqrt(area / true_aspect))));
    }

    cv::Point2f dst_pts[4] = {
        {0, 0},
        {float(out_w - 1), 0},
        {float(out_w - 1), float(out_h - 1)},
        {0, float(out_h - 1)}
    };

    cv::Mat m = cv::getPerspectiveTransform(src_pts, dst_pts);
    cv::Mat warped;
    cv::warpPerspective(src, warped, m, cv::Size(out_w, out_h), cv::INTER_LINEAR);
    return warped;
}

} // namespace scanwise
