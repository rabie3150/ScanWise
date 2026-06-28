#include "scanwise/filters.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace scanwise {

cv::Mat rgba_to_mat(int width, int height, const uint8_t* rgba) {
    cv::Mat bgr(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = rgba + (y * width + x) * 4;
            // RGBA -> BGR
            bgr.at<cv::Vec3b>(y, x) = cv::Vec3b(p[2], p[1], p[0]);
        }
    }
    return bgr;
}

std::vector<uint8_t> mat_to_rgba(const cv::Mat& src) {
    if (src.empty()) return {};
    int w = src.cols;
    int h = src.rows;
    std::vector<uint8_t> rgba(w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            cv::Vec3b bgr = src.at<cv::Vec3b>(y, x);
            uint8_t* p = rgba.data() + (y * w + x) * 4;
            p[0] = bgr[2]; // R
            p[1] = bgr[1]; // G
            p[2] = bgr[0]; // B
            p[3] = 255;    // A
        }
    }
    return rgba;
}

// Estimate local paper background using morphological closing on a downscaled
// proxy. Closing preserves large dark regions (photos, graphics) better than a
// plain Gaussian blur, which smears them into the background estimate.
static cv::Mat estimate_background(const cv::Mat& src, const magic_color_params& p) {
    // Downscale to a max dimension of bg_proxy_size px for speed.
    double scale = static_cast<double>(p.bg_proxy_size) / std::max(src.cols, src.rows);
    cv::Mat small;
    if (scale >= 1.0) {
        small = src;
    } else {
        cv::Size new_size(
            std::max(1, static_cast<int>(src.cols * scale)),
            std::max(1, static_cast<int>(src.rows * scale))
        );
        cv::resize(src, small, new_size, 0, 0, cv::INTER_LINEAR);
    }

    // Kernel sized proportionally to the downscaled image, odd, at least 5 px.
    int k = std::max(5, std::max(small.cols, small.rows) / std::max(1, p.bg_kernel_divisor));
    if (k % 2 == 0) ++k;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));

    cv::Mat bg_small;
    cv::morphologyEx(small, bg_small, cv::MORPH_CLOSE, kernel);

    cv::Mat bg;
    if (bg_small.size() == src.size()) {
        bg = bg_small;
    } else {
        cv::resize(bg_small, bg, src.size(), 0, 0, cv::INTER_LINEAR);
    }
    return bg;
}

// Normalize uneven illumination:
//   1. Slight Gaussian denoise so paper grain is not amplified.
//   2. Estimate local background with morphological closing.
//   3. Divide by the background estimate.
//   4. Clamp extreme amplification factors.
// Input/output are CV_32F, output is scaled to [0, 255].
static cv::Mat normalize_illumination_float(const cv::Mat& src_float, const magic_color_params& p) {
    cv::Mat denoised;
    if (p.denoise_sigma > 0.001f) {
        cv::GaussianBlur(src_float, denoised, cv::Size(0, 0), p.denoise_sigma);
    } else {
        denoised = src_float;
    }

    cv::Mat bg = estimate_background(denoised, p);
    cv::add(bg, cv::Scalar::all(1e-5), bg);

    cv::Mat normalized;
    cv::divide(denoised, bg, normalized);

    // Clamp to avoid blowing out shadows or amplifying noise.
    cv::Mat clamped_low;
    cv::max(normalized, p.norm_clamp_min, clamped_low);
    cv::min(clamped_low, p.norm_clamp_max, normalized);
    cv::multiply(normalized, 255.0, normalized);
    return normalized;
}

// Perceptual brightness/contrast adjustment.
// Brightness is applied as a gamma curve; contrast is applied around the
// perceptual midpoint (0.5). This avoids the harsh clipping of a purely linear
// transform at the slider extremes.
static cv::Mat apply_brightness_contrast(const cv::Mat& src, float brightness, float contrast) {
    if (std::abs(brightness) < 0.001f && std::abs(contrast) < 0.001f) return src.clone();

    cv::Mat f;
    src.convertTo(f, CV_32F, 1.0 / 255.0);

    if (std::abs(brightness) > 0.001f) {
        double gamma = std::pow(2.0, -static_cast<double>(brightness));
        cv::pow(f, gamma, f);
    }

    if (std::abs(contrast) > 0.001f) {
        double alpha = 1.0 + static_cast<double>(contrast);
        cv::Mat shifted, scaled;
        cv::subtract(f, 0.5f, shifted);
        cv::multiply(shifted, alpha, scaled);
        cv::add(scaled, 0.5f, f);
    }

    cv::Mat clamped_low;
    cv::max(f, 0.0f, clamped_low);
    cv::min(clamped_low, 1.0f, f);

    cv::Mat out;
    f.convertTo(out, CV_8U, 255.0);
    return out;
}

static cv::Mat apply_saturation(const cv::Mat& src, float saturation) {
    if (std::abs(saturation) < 0.001f) return src.clone();

    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);

    float factor = 1.0f + saturation;
    channels[1].convertTo(channels[1], CV_32F);
    channels[1] *= factor;
    channels[1] = cv::min(channels[1], 255.0f);
    channels[1].convertTo(channels[1], CV_8U);

    cv::merge(channels, hsv);
    cv::Mat out;
    cv::cvtColor(hsv, out, cv::COLOR_HSV2BGR);
    return out;
}

cv::Mat apply_filter(const cv::Mat& src,
                     filter_preset preset,
                     float brightness,
                     float contrast,
                     float saturation,
                     const magic_color_params& mc_params) {
    if (src.empty()) return {};

    cv::Mat working;

    switch (preset) {
        case filter_preset::original: {
            working = src.clone();
            break;
        }
        case filter_preset::black_white: {
            cv::Mat gray;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

            cv::Mat gray_float;
            gray.convertTo(gray_float, CV_32F);
            cv::Mat normalized = normalize_illumination_float(gray_float, magic_color_params{});
            normalized.convertTo(gray, CV_8U);

            // Otsu computes a per-image threshold; clamp it to a sane range so
            // near-empty or extreme inputs do not collapse to all-white/black.
            cv::Mat dummy;
            double otsu = cv::threshold(gray, dummy, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            double thresh = std::clamp(otsu, 150.0, 230.0);

            cv::Mat bw;
            cv::threshold(gray, bw, thresh, 255, cv::THRESH_BINARY);

            // Clear border: erase edge-connected black blobs (e.g. desk slivers).
            cv::Mat mask = cv::Mat::zeros(bw.rows + 2, bw.cols + 2, CV_8U);
            for (int x = 0; x < bw.cols; x++) {
                if (bw.at<uchar>(0, x) == 0)
                    cv::floodFill(bw, mask, cv::Point(x, 0), cv::Scalar(255));
                if (bw.at<uchar>(bw.rows - 1, x) == 0)
                    cv::floodFill(bw, mask, cv::Point(x, bw.rows - 1), cv::Scalar(255));
            }
            for (int y = 0; y < bw.rows; y++) {
                if (bw.at<uchar>(y, 0) == 0)
                    cv::floodFill(bw, mask, cv::Point(0, y), cv::Scalar(255));
                if (bw.at<uchar>(y, bw.cols - 1) == 0)
                    cv::floodFill(bw, mask, cv::Point(bw.cols - 1, y), cv::Scalar(255));
            }

            cv::cvtColor(bw, working, cv::COLOR_GRAY2BGR);
            break;
        }
        case filter_preset::document: {
            cv::Mat gray;
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

            cv::Mat gray_float;
            gray.convertTo(gray_float, CV_32F);
            cv::Mat normalized = normalize_illumination_float(gray_float, magic_color_params{});

            // Unsharp masking for crisp text edges.
            cv::Mat blurred;
            cv::GaussianBlur(normalized, blurred, cv::Size(0, 0), 2.0);
            cv::Mat sharpened;
            cv::addWeighted(normalized, 1.5, blurred, -0.5, 0, sharpened);

            sharpened.convertTo(gray, CV_8U);
            cv::cvtColor(gray, working, cv::COLOR_GRAY2BGR);
            break;
        }
        case filter_preset::magic_color: {
            cv::Mat src_float;
            src.convertTo(src_float, CV_32FC3);

            std::vector<cv::Mat> channels;
            cv::split(src_float, channels);
            for (auto& c : channels) {
                c = normalize_illumination_float(c, mc_params);
            }
            cv::Mat normalized;
            cv::merge(channels, normalized);

            // Unsharp masking using tunable params.
            cv::Mat blurred;
            if (mc_params.sharpen_sigma > 0.001f) {
                cv::GaussianBlur(normalized, blurred, cv::Size(0, 0), mc_params.sharpen_sigma);
            } else {
                blurred = normalized.clone();
            }
            cv::Mat sharpened;
            cv::addWeighted(normalized, mc_params.sharpen_alpha, blurred, mc_params.sharpen_beta, 0, sharpened);

            sharpened.convertTo(working, CV_8UC3);

            // Boost saturation to make inks pop.
            cv::Mat hsv;
            cv::cvtColor(working, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> hsv_channels;
            cv::split(hsv, hsv_channels);
            hsv_channels[1].convertTo(hsv_channels[1], CV_32F);
            cv::multiply(hsv_channels[1], mc_params.saturation_boost, hsv_channels[1]);
            cv::min(hsv_channels[1], 255.0f, hsv_channels[1]);
            hsv_channels[1].convertTo(hsv_channels[1], CV_8U);
            cv::merge(hsv_channels, hsv);
            cv::cvtColor(hsv, working, cv::COLOR_HSV2BGR);
            break;
        }
        case filter_preset::photo: {
            // Slight saturation and value boost.
            cv::Mat hsv;
            cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> channels;
            cv::split(hsv, channels);
            channels[1] = cv::min(channels[1] * 1.15f, 255.0f);
            channels[2] = cv::min(channels[2] * 1.08f, 255.0f);
            cv::merge(channels, hsv);
            cv::cvtColor(hsv, working, cv::COLOR_HSV2BGR);
            break;
        }
    }

    working = apply_brightness_contrast(working, brightness, contrast);
    working = apply_saturation(working, saturation);
    return working;
}

} // namespace scanwise
