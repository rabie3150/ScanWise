#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scanwise {

struct point {
    float x = 0.0f;
    float y = 0.0f;
};

struct quad {
    point tl; // top-left
    point tr; // top-right
    point br; // bottom-right
    point bl; // bottom-left

    std::array<point, 4> as_array() const { return {tl, tr, br, bl}; }
};

inline quad image_corners(float width, float height) {
    return quad{
        {0.0f, 0.0f},
        {width, 0.0f},
        {width, height},
        {0.0f, height}
    };
}

enum class filter_preset { original, magic_color, black_white, document, photo };

enum class export_format { png, jpeg, pdf };

// Tunable parameters for the Magic Color filter.
struct magic_color_params {
    float denoise_sigma = 1.0f;        // pre-normalization Gaussian blur sigma
    int bg_proxy_size = 256;           // max dimension of background-estimate proxy
    int bg_kernel_divisor = 16;        // morphological kernel size = proxy_max / divisor
    float norm_clamp_min = 0.5f;       // lower clamp after background division
    float norm_clamp_max = 2.0f;       // upper clamp after background division
    float sharpen_sigma = 2.0f;        // unsharp mask blur sigma
    float sharpen_alpha = 1.5f;        // unsharp mask original weight
    float sharpen_beta = -0.5f;        // unsharp mask blur weight
    float saturation_boost = 1.4f;     // final saturation multiplier
};

// Tunable parameters for document-corner detection.
struct detection_options {
    // Downscale targets tried by multi-scale detection.
    std::vector<int> scales = {500, 700, 900};

    // Canny thresholds are computed as otsu * multiplier.
    double canny_low_multiplier = 0.5;
    double canny_high_multiplier = 1.5;

    // Hough threshold = max(diagonal / hough_threshold_divisor, hough_threshold_min).
    int hough_threshold_divisor = 8;
    int hough_threshold_min = 40;

    // Discard Hough lines whose clipped segment is shorter than this ratio of image size.
    float hough_min_segment_ratio = 0.15f;

    // Angle/rho clustering thresholds for Hough line grouping.
    float hough_angle_cluster_thresh = 0.25f;
    float hough_rho_cluster_thresh = 20.0f;

    // Contour strategy: minimum contour area as ratio of image area.
    double min_area_ratio = 0.30;

    // Corner refinement search radius in original-resolution pixels.
    int corner_refinement_radius = 40;

    // Color segmentation distance threshold (Lab color space).
    double color_dist_thresh = 15.0;

    // CLAHE preprocessing
    bool use_clahe = false;
    double clahe_clip_limit = 2.0;
    int clahe_grid_size = 8;

    // Quad scoring knobs.
    double score_area_min = 0.05;
    double score_area_max = 0.90;
    double score_aspect_start = 1.6;
    double score_aspect_mid = 2.0;
    double score_aspect_extreme = 3.0;
};

struct page {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // decoded image data
    quad crop = {{0,0},{1,0},{1,1},{0,1}}; // normalized corners
    filter_preset filter = filter_preset::original;
    float brightness = 0.0f;
    float contrast = 0.0f;
    float saturation = 0.0f;
    bool warp_applied = false;
    magic_color_params mc_params;      // per-page tunable Magic Color params
    detection_options det_opts;        // per-page tunable Magic Perspective params
};

struct export_options {
    export_format format = export_format::png;
    int jpeg_quality = 90;   // 0..100
    int dpi = 300;
};

} // namespace scanwise
