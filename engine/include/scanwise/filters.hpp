#pragma once

#include "scanwise/types.hpp"
#include <opencv2/core.hpp>

namespace scanwise {

// Apply the selected preset + manual adjustments to a BGR mat.
// Input/output are CV_8UC3 BGR mats.
cv::Mat apply_filter(const cv::Mat& src,
                     filter_preset preset,
                     float brightness,
                     float contrast,
                     float saturation,
                     const magic_color_params& mc_params = {});

// Decode a WIC-style RGBA buffer into a BGR cv::Mat.
cv::Mat rgba_to_mat(int width, int height, const uint8_t* rgba);

// Encode a BGR cv::Mat back into an RGBA buffer.
std::vector<uint8_t> mat_to_rgba(const cv::Mat& src);

} // namespace scanwise
