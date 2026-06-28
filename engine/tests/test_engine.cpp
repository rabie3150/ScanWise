#include "scanwise/engine.hpp"
#include "scanwise/types.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

using namespace scanwise;

static std::vector<uint8_t> make_test_image(int w, int h) {
    std::vector<uint8_t> rgba(w * h * 4, 255);
    // Draw a dark document on white background
    for (int y = h / 4; y < 3 * h / 4; ++y) {
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            uint8_t* p = rgba.data() + (y * w + x) * 4;
            p[0] = 80; p[1] = 80; p[2] = 80; p[3] = 255;
        }
    }
    return rgba;
}

// Convert OpenCV BGRA Mat to RGBA vector for the engine.
static std::vector<uint8_t> bgra_to_rgba(const cv::Mat& bgra) {
    std::vector<uint8_t> rgba(bgra.total() * 4);
    for (int y = 0; y < bgra.rows; ++y) {
        for (int x = 0; x < bgra.cols; ++x) {
            const cv::Vec4b& p = bgra.at<cv::Vec4b>(y, x);
            uint8_t* out = rgba.data() + (y * bgra.cols + x) * 4;
            out[0] = p[2]; out[1] = p[1]; out[2] = p[0]; out[3] = p[3];
        }
    }
    return rgba;
}

// Generate a synthetic image: a perspective-warped document on a noisy
// background. Returns the normalized expected corner quad.
static quad make_document_image(int img_w, int img_h,
                                const std::vector<cv::Point2f>& dst_corners,
                                cv::Mat& out_bgra,
                                bool grid = false) {
    // Background: dark textured surface similar to a woven mat.
    cv::Mat img(img_h, img_w, CV_8UC4, cv::Scalar(90, 90, 90, 255));
    for (int y = 0; y < img_h; y += 25) {
        cv::Scalar c1 = ((y / 25) % 2 == 0) ? cv::Scalar(70, 70, 70, 255)
                                            : cv::Scalar(110, 110, 110, 255);
        cv::rectangle(img, cv::Point(0, y), cv::Point(img_w, std::min(img_h, y + 25)),
                      c1, cv::FILLED);
    }
    cv::Mat noise(img_h, img_w, CV_8UC4);
    cv::randu(noise, cv::Scalar(0, 0, 0, 0), cv::Scalar(30, 30, 30, 0));
    cv::add(img, noise, img);

    // Document page content.
    int doc_w = 600;
    int doc_h = 850;
    cv::Mat doc(doc_h, doc_w, CV_8UC4, cv::Scalar(250, 250, 250, 255));
    if (grid) {
        // Strong internal grid that must not be mistaken for the page border.
        int gx0 = 60, gx1 = doc_w - 60;
        int gy0 = 80, gy1 = doc_h - 80;
        int cols = 8, rows = 12;
        cv::Scalar black(40, 40, 40, 255);
        for (int i = 0; i <= cols; ++i) {
            int x = gx0 + (gx1 - gx0) * i / cols;
            cv::line(doc, cv::Point(x, gy0), cv::Point(x, gy1), black, 2);
        }
        for (int i = 0; i <= rows; ++i) {
            int y = gy0 + (gy1 - gy0) * i / rows;
            cv::line(doc, cv::Point(gx0, y), cv::Point(gx1, y), black, 2);
        }
    } else {
        // Fake text lines.
        for (int y = 100; y < doc_h - 100; y += 45) {
            cv::line(doc, cv::Point(60, y), cv::Point(doc_w - 60, y),
                     cv::Scalar(60, 60, 60, 255), 2);
        }
        // Faint vertical margin marks to strengthen left/right edges.
        cv::line(doc, cv::Point(45, 80), cv::Point(45, doc_h - 80),
                 cv::Scalar(80, 80, 80, 255), 1);
        cv::line(doc, cv::Point(doc_w - 45, 80), cv::Point(doc_w - 45, doc_h - 80),
                 cv::Scalar(80, 80, 80, 255), 1);
    }

    std::vector<cv::Point2f> src = {
        {0, 0}, {(float)doc_w, 0}, {(float)doc_w, (float)doc_h}, {0, (float)doc_h}
    };
    cv::Mat H = cv::getPerspectiveTransform(src, dst_corners);
    cv::warpPerspective(doc, img, H, cv::Size(img_w, img_h),
                        cv::INTER_LINEAR, cv::BORDER_TRANSPARENT);

    out_bgra = img;

    // Normalize expected corners.
    return quad{
        {dst_corners[0].x / img_w, dst_corners[0].y / img_h},
        {dst_corners[1].x / img_w, dst_corners[1].y / img_h},
        {dst_corners[2].x / img_w, dst_corners[2].y / img_h},
        {dst_corners[3].x / img_w, dst_corners[3].y / img_h}
    };
}

static float quad_error(const quad& a, const quad& b) {
    auto aa = a.as_array();
    auto ba = b.as_array();
    float err = 0.0f;
    for (size_t i = 0; i < 4; ++i) {
        float dx = aa[i].x - ba[i].x;
        float dy = aa[i].y - ba[i].y;
        err = std::max(err, std::sqrt(dx * dx + dy * dy));
    }
    return err;
}

static int test_case(const char* name, int w, int h,
                     const std::vector<cv::Point2f>& corners,
                     float tolerance = 0.03f,
                     bool add_shadow = false,
                     float low_contrast = 0.0f,
                     bool grid = false) {
    cv::Mat img;
    quad expected = make_document_image(w, h, corners, img, grid);

    if (add_shadow) {
        // Add a soft shadow just outside the document bottom/right edges,
        // simulating the paper casting a shadow on the mat.
        float max_x = 0.0f, max_y = 0.0f;
        for (const auto& c : corners) {
            max_x = std::max(max_x, c.x);
            max_y = std::max(max_y, c.y);
        }
        cv::Mat shadow(img.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dy = static_cast<float>(y) - max_y;
                float dx = static_cast<float>(x) - max_x;
                if (dy > 0 && dx > dy * 0.3f) {
                    float alpha = std::max(0.0f, 1.0f - (dy / (h * 0.25f))) * 90.0f;
                    shadow.at<cv::Vec4b>(y, x) = cv::Vec4b(0, 0, 0, static_cast<uint8_t>(alpha));
                }
            }
        }
        cv::add(img, shadow, img);
    }

    if (low_contrast > 0.0f) {
        // Blend document and background together to reduce contrast.
        cv::Mat bg(img.size(), CV_8UC4, cv::Scalar(200, 200, 200, 255));
        cv::addWeighted(img, 1.0f - low_contrast, bg, low_contrast, 0.0, img);
    }

    engine e;
    auto rgba = bgra_to_rgba(img);
    e.load_image(w, h, rgba.data());
    quad detected = e.detect_document_corners();

    float err = quad_error(detected, expected);
    if (err > tolerance) {
        std::cerr << "FAIL: " << name << " error=" << err
                  << " (tolerance=" << tolerance << ")\n";
        std::cerr << "Expected: tl(" << expected.tl.x << "," << expected.tl.y << ") "
                  << "tr(" << expected.tr.x << "," << expected.tr.y << ") "
                  << "br(" << expected.br.x << "," << expected.br.y << ") "
                  << "bl(" << expected.bl.x << "," << expected.bl.y << ")\n";
        std::cerr << "Detected: tl(" << detected.tl.x << "," << detected.tl.y << ") "
                  << "tr(" << detected.tr.x << "," << detected.tr.y << ") "
                  << "br(" << detected.br.x << "," << detected.br.y << ") "
                  << "bl(" << detected.bl.x << "," << detected.bl.y << ")\n";
        return 1;
    }
    std::cout << "PASS: " << name << " error=" << err << "\n";
    return 0;
}

int main() {
    std::cout << "ScanWise engine smoke test\n";

    engine e;

    // Test 1: load image
    int w = 640, h = 480;
    auto img = make_test_image(w, h);
    e.load_image(w, h, img.data());
    if (e.page_count() != 1) {
        std::cerr << "FAIL: page count\n";
        return 1;
    }
    std::cout << "PASS: load image\n";

    // Test 2: detect corners (fallback to image corners on synthetic image)
    auto q = e.detect_document_corners();
    std::cout << "Corners: tl(" << q.tl.x << "," << q.tl.y << ") "
              << "tr(" << q.tr.x << "," << q.tr.y << ") "
              << "br(" << q.br.x << "," << q.br.y << ") "
              << "bl(" << q.bl.x << "," << q.bl.y << ")\n";
    std::cout << "PASS: detect corners returned\n";

    // Test 3: render with default crop and filter
    int rw = 0, rh = 0;
    auto rendered = e.render_current_page(rw, rh);
    if (rendered.empty() || rw <= 0 || rh <= 0) {
        std::cerr << "FAIL: render empty\n";
        return 1;
    }
    std::cout << "PASS: render " << rw << "x" << rh << "\n";

    // Test 4: apply each filter preset
    for (auto fp : {filter_preset::original, filter_preset::black_white,
                    filter_preset::document, filter_preset::magic_color,
                    filter_preset::photo}) {
        e.set_filter(fp);
        rendered = e.render_current_page(rw, rh);
        if (rendered.empty()) {
            std::cerr << "FAIL: filter render empty\n";
            return 1;
        }
    }
    std::cout << "PASS: all filter presets render\n";

    // Test 5: manual adjustments
    e.set_brightness(0.1f);
    e.set_contrast(0.1f);
    e.set_saturation(0.1f);
    rendered = e.render_current_page(rw, rh);
    if (rendered.empty()) {
        std::cerr << "FAIL: adjusted render empty\n";
        return 1;
    }
    std::cout << "PASS: manual adjustments\n";

    // Test 6: PDF export (single page)
    auto pdf = e.export_pdf(300, 90);
    if (pdf.size() < 100) {
        std::cerr << "FAIL: PDF too small\n";
        return 1;
    }
    if (pdf[0] != '%' || pdf[1] != 'P') {
        std::cerr << "FAIL: PDF header bad\n";
        return 1;
    }
    std::cout << "PASS: PDF export " << pdf.size() << " bytes\n";

    // -------------------------------------------------------------------------
    // Regression tests for document corner detection.
    // -------------------------------------------------------------------------
    std::cout << "\nDocument detection regression tests\n";

    int W = 1600, H = 1200;
    int margin = 180;

    // Straight-on document.
    if (test_case("straight-on document", W, H,
                  {{(float)margin, (float)margin},
                   {(float)(W - margin), (float)margin},
                   {(float)(W - margin), (float)(H - margin)},
                   {(float)margin, (float)(H - margin)}})) return 1;

    // Perspective-rotated document.
    if (test_case("perspective rotated", W, H,
                  {{220.0f, 160.0f},
                   {(float)(W - 150), 220.0f},
                   {(float)(W - 220), (float)(H - 180)},
                   {180.0f, (float)(H - 140)}},
                  0.04f)) return 1;

    // Smaller document on busy background (must be > 30% of screen).
    if (test_case("small centered document", W, H,
                  {{300.0f, 200.0f},
                   {(float)(W - 300), 200.0f},
                   {(float)(W - 300), (float)(H - 200)},
                   {300.0f, (float)(H - 200)}})) return 1;

    // Document with strong corner shadow.
    if (test_case("shadow corner", W, H,
                  {{(float)margin, (float)margin},
                   {(float)(W - margin), (float)margin},
                   {(float)(W - margin), (float)(H - margin)},
                   {(float)margin, (float)(H - margin)}},
                  0.04f, true, 0.0f)) return 1;

    // Lower-contrast document.
    if (test_case("low contrast", W, H,
                  {{260.0f, 200.0f},
                   {(float)(W - 200), 260.0f},
                   {(float)(W - 260), (float)(H - 200)},
                   {200.0f, (float)(H - 260)}},
                  0.04f, false, 0.35f)) return 1;

    // Perspective document with a strong internal grid; the detector must find
    // the outer page border, not the grid cells.
    if (test_case("grid document", W, H,
                  {{220.0f, 160.0f},
                   {(float)(W - 150), 220.0f},
                   {(float)(W - 220), (float)(H - 180)},
                   {180.0f, (float)(H - 140)}},
                  0.04f, false, 0.0f, true)) return 1;

    std::cout << "All engine tests passed.\n";
    return 0;
}
