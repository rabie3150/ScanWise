#pragma once

#include "scanwise/types.hpp"
#include "scanwise/filters.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace scanwise {

// OS-agnostic document scanning pipeline.
class engine {
public:
    engine();
    ~engine();

    // Load a decoded RGBA image as a new page.
    void load_image(int width, int height, const uint8_t* rgba);

    // Page management.
    std::size_t page_count() const;
    void set_current_page(std::size_t index);
    std::size_t current_page_index() const;
    void remove_page(std::size_t index);
    void move_page(std::size_t from, std::size_t to);

    // Corner detection.
    // Returns normalized quad (0..1 in image space) for the current page.
    quad detect_document_corners();

    // Crop editing.
    void set_crop_quad(const quad& q);
    quad get_crop_quad() const;

    // Perspective workflow.
    bool is_warp_applied() const;
    void set_warp_applied(bool applied);

    // Geometric transforms on the current page.
    void rotate_current_page(int steps); // 90-degree clockwise steps
    void flip_current_page(bool horizontal);

    // Filter settings.
    void set_filter(filter_preset f);
    filter_preset get_filter() const;
    void set_brightness(float v); // -1 .. +1
    void set_contrast(float v);   // -1 .. +1
    void set_saturation(float v); // -1 .. +1
    float get_brightness() const;
    float get_contrast() const;
    float get_saturation() const;

    // Magic Color tunable parameters (per-page).
    void set_magic_color_params(const magic_color_params& p);
    magic_color_params get_magic_color_params() const;

    // Magic Perspective (corner detection) tunable parameters (per-page).
    void set_detection_options(const detection_options& opts);
    detection_options get_detection_options() const;

    // Rendering pipeline: applies crop + filter to current page.
    // Returns a freshly allocated RGBA buffer of the processed image.
    std::vector<uint8_t> render_current_page(int& out_width, int& out_height);

    // Encode current page to PNG/JPEG bytes.
    // Actual encoding happens via WIC in the Windows app; this is a placeholder
    // for non-Windows builds/tests (uses OpenCV imgcodecs if available).
    std::vector<uint8_t> encode_current_page(export_format fmt, int quality = 90);

    // Export a multi-page PDF from the current page list.
    // Each page is rendered and embedded as a JPEG.
    std::vector<uint8_t> export_pdf(int dpi = 300, int jpeg_quality = 90);

private:
    struct impl;
    std::unique_ptr<impl> p;
};

} // namespace scanwise
