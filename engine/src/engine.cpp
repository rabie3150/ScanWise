#include "scanwise/engine.hpp"
#include "scanwise/filters.hpp"
#include <algorithm>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace scanwise {

// Forward declarations for internal engine helpers defined in other .cpp files.
quad detect_corners_impl(const page& pg);
cv::Mat warp_perspective_impl(const cv::Mat& src, const quad& crop);
std::vector<uint8_t> export_pdf_impl(const std::vector<page>& pages, int dpi, int jpeg_quality);

struct engine::impl {
    std::vector<page> pages;
    std::size_t current = 0;
};

engine::engine() : p(std::make_unique<impl>()) {}
engine::~engine() = default;

void engine::load_image(int width, int height, const uint8_t* rgba) {
    page pg;
    pg.width = width;
    pg.height = height;
    pg.rgba.assign(rgba, rgba + width * height * 4);
    pg.crop = {{0,0},{1,0},{1,1},{0,1}};
    p->pages.push_back(std::move(pg));
    p->current = p->pages.size() - 1;
}

std::size_t engine::page_count() const { return p->pages.size(); }

void engine::set_current_page(std::size_t index) {
    if (index >= p->pages.size()) throw std::out_of_range("page index");
    p->current = index;
}

std::size_t engine::current_page_index() const { return p->current; }

void engine::remove_page(std::size_t index) {
    if (index >= p->pages.size()) return;
    p->pages.erase(p->pages.begin() + index);
    if (p->current >= p->pages.size() && !p->pages.empty())
        p->current = p->pages.size() - 1;
}

void engine::move_page(std::size_t from, std::size_t to) {
    if (from >= p->pages.size() || to >= p->pages.size()) return;
    if (from == to) return;
    auto pg = std::move(p->pages[from]);
    p->pages.erase(p->pages.begin() + from);
    p->pages.insert(p->pages.begin() + to, std::move(pg));
}

quad engine::detect_document_corners() {
    // Implemented in edge_detect.cpp
    if (p->pages.empty()) return {{0,0},{1,0},{1,1},{0,1}};
    return detect_corners_impl(p->pages[p->current]);
}



void engine::set_crop_quad(const quad& q) {
    if (p->pages.empty()) return;
    p->pages[p->current].crop = q;
}

quad engine::get_crop_quad() const {
    if (p->pages.empty()) return {{0,0},{1,0},{1,1},{0,1}};
    return p->pages[p->current].crop;
}

bool engine::is_warp_applied() const {
    if (p->pages.empty()) return false;
    return p->pages[p->current].warp_applied;
}

void engine::set_warp_applied(bool applied) {
    if (p->pages.empty()) return;
    p->pages[p->current].warp_applied = applied;
}



void engine::rotate_current_page(int steps) {
    if (p->pages.empty()) return;
    page& pg = p->pages[p->current];
    cv::Mat bgr = rgba_to_mat(pg.width, pg.height, pg.rgba.data());
    cv::Mat rotated;
    quad new_crop;
    switch (steps % 4) {
        case 1:
        case -3:
            cv::rotate(bgr, rotated, cv::ROTATE_90_CLOCKWISE);
            new_crop.tl = {1.0f - pg.crop.bl.y, pg.crop.bl.x};
            new_crop.tr = {1.0f - pg.crop.tl.y, pg.crop.tl.x};
            new_crop.br = {1.0f - pg.crop.tr.y, pg.crop.tr.x};
            new_crop.bl = {1.0f - pg.crop.br.y, pg.crop.br.x};
            break;
        case 2:
        case -2:
            cv::rotate(bgr, rotated, cv::ROTATE_180);
            new_crop.tl = {1.0f - pg.crop.br.x, 1.0f - pg.crop.br.y};
            new_crop.tr = {1.0f - pg.crop.bl.x, 1.0f - pg.crop.bl.y};
            new_crop.br = {1.0f - pg.crop.tl.x, 1.0f - pg.crop.tl.y};
            new_crop.bl = {1.0f - pg.crop.tr.x, 1.0f - pg.crop.tr.y};
            break;
        case 3:
        case -1:
            cv::rotate(bgr, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
            new_crop.tl = {pg.crop.tr.y, 1.0f - pg.crop.tr.x};
            new_crop.tr = {pg.crop.br.y, 1.0f - pg.crop.br.x};
            new_crop.br = {pg.crop.bl.y, 1.0f - pg.crop.bl.x};
            new_crop.bl = {pg.crop.tl.y, 1.0f - pg.crop.tl.x};
            break;
        default: return;
    }
    pg.width = rotated.cols;
    pg.height = rotated.rows;
    pg.rgba = mat_to_rgba(rotated);
    pg.crop = new_crop;
}

void engine::flip_current_page(bool horizontal) {
    if (p->pages.empty()) return;
    page& pg = p->pages[p->current];
    cv::Mat bgr = rgba_to_mat(pg.width, pg.height, pg.rgba.data());
    cv::Mat flipped;
    cv::flip(bgr, flipped, horizontal ? 1 : 0);
    quad new_crop;
    if (horizontal) {
        new_crop.tl = {1.0f - pg.crop.tr.x, pg.crop.tr.y};
        new_crop.tr = {1.0f - pg.crop.tl.x, pg.crop.tl.y};
        new_crop.br = {1.0f - pg.crop.bl.x, pg.crop.bl.y};
        new_crop.bl = {1.0f - pg.crop.br.x, pg.crop.br.y};
    } else {
        new_crop.tl = {pg.crop.bl.x, 1.0f - pg.crop.bl.y};
        new_crop.tr = {pg.crop.br.x, 1.0f - pg.crop.br.y};
        new_crop.br = {pg.crop.tr.x, 1.0f - pg.crop.tr.y};
        new_crop.bl = {pg.crop.tl.x, 1.0f - pg.crop.tl.y};
    }
    pg.width = flipped.cols;
    pg.height = flipped.rows;
    pg.rgba = mat_to_rgba(flipped);
    pg.crop = new_crop;
}

void engine::set_filter(filter_preset f) {
    if (p->pages.empty()) return;
    p->pages[p->current].filter = f;
}
filter_preset engine::get_filter() const {
    if (p->pages.empty()) return filter_preset::original;
    return p->pages[p->current].filter;
}
void engine::set_brightness(float v) {
    if (p->pages.empty()) return;
    p->pages[p->current].brightness = std::clamp(v, -1.0f, 1.0f);
}
void engine::set_contrast(float v) {
    if (p->pages.empty()) return;
    p->pages[p->current].contrast = std::clamp(v, -1.0f, 1.0f);
}
void engine::set_saturation(float v) {
    if (p->pages.empty()) return;
    p->pages[p->current].saturation = std::clamp(v, -1.0f, 1.0f);
}
float engine::get_brightness() const {
    if (p->pages.empty()) return 0.0f;
    return p->pages[p->current].brightness;
}
float engine::get_contrast() const {
    if (p->pages.empty()) return 0.0f;
    return p->pages[p->current].contrast;
}
float engine::get_saturation() const {
    if (p->pages.empty()) return 0.0f;
    return p->pages[p->current].saturation;
}

void engine::set_magic_color_params(const magic_color_params& params) {
    if (p->pages.empty()) return;
    p->pages[p->current].mc_params = params;
}

magic_color_params engine::get_magic_color_params() const {
    if (p->pages.empty()) return {};
    return p->pages[p->current].mc_params;
}

void engine::set_detection_options(const detection_options& opts) {
    if (p->pages.empty()) return;
    p->pages[p->current].det_opts = opts;
}

detection_options engine::get_detection_options() const {
    if (p->pages.empty()) return {};
    return p->pages[p->current].det_opts;
}

std::vector<uint8_t> engine::render_current_page(int& out_width, int& out_height) {
    if (p->pages.empty()) {
        out_width = 0;
        out_height = 0;
        return {};
    }
    const page& pg = p->pages[p->current];

    // 1. Convert to BGR
    cv::Mat bgr = rgba_to_mat(pg.width, pg.height, pg.rgba.data());

    if (!pg.warp_applied) {
        // Show the original, un-warped image while the user adjusts corners.
        out_width = pg.width;
        out_height = pg.height;
        return pg.rgba;
    }

    // 2. Perspective warp from crop quad
    cv::Mat warped = warp_perspective_impl(bgr, pg.crop);

    // 3. Apply filter
    cv::Mat filtered = apply_filter(warped, pg.filter, pg.brightness, pg.contrast, pg.saturation, pg.mc_params);

    out_width = filtered.cols;
    out_height = filtered.rows;
    return mat_to_rgba(filtered);
}

std::vector<uint8_t> engine::encode_current_page(export_format fmt, int quality) {
    int w = 0, h = 0;
    auto rgba = render_current_page(w, h);
    if (rgba.empty()) return {};

    // For cross-platform CLI tests, fallback to OpenCV imgcodecs if compiled.
    // The Windows app uses WIC instead to avoid linking imgcodecs.
#if __has_include(<opencv2/imgcodecs.hpp>)
    cv::Mat bgr(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgba.data() + (y * w + x) * 4;
            bgr.at<cv::Vec3b>(y, x) = cv::Vec3b(p[0], p[1], p[2]);
        }
    }
    std::vector<int> params;
    if (fmt == export_format::jpeg) {
        params = {cv::IMWRITE_JPEG_QUALITY, std::clamp(quality, 0, 100)};
    } else {
        params = {cv::IMWRITE_PNG_COMPRESSION, 6};
    }
    std::vector<uint8_t> buf;
    cv::imencode(fmt == export_format::jpeg ? ".jpg" : ".png", bgr, buf, params);
    return buf;
#else
    (void)fmt; (void)quality;
    return rgba; // Not a real encode without imgcodecs; tests should use WIC path
#endif
}

std::vector<uint8_t> engine::export_pdf(int dpi, int jpeg_quality) {
    return export_pdf_impl(p->pages, dpi, jpeg_quality);
}

} // namespace scanwise
