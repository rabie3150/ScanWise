#include "scanwise/types.hpp"
#include "scanwise/filters.hpp"
#include <opencv2/imgproc.hpp>

// libharu headers
#include "hpdf.h"

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

namespace scanwise {

static cv::Mat render_page(const page& pg) {
    cv::Mat bgr = rgba_to_mat(pg.width, pg.height, pg.rgba.data());

    extern cv::Mat warp_perspective_impl(const cv::Mat& src, const quad& crop);
    cv::Mat warped = warp_perspective_impl(bgr, pg.crop);

    cv::Mat filtered = apply_filter(warped, pg.filter, pg.brightness, pg.contrast, pg.saturation, pg.mc_params);
    return filtered;
}

// Encode BGR mat to JPEG bytes (baseline implementation using libharu helpers not available,
// so we do a simple no-zlib approach: write raw DCT-free JPEG is hard. For P1 we embed
// uncompressed RGB data into PDF if libharu is built without zlib, or use libharu's
// HPDF_LoadPngImageFromMem if PNG. To keep things simple and dependency-free, we embed
// uncompressed 24-bit RGB raster pages. This is larger but avoids needing zlib/JPEG encoder.
// A later optimization can add stb_image_write or libjpeg-turbo.

static void error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
    (void)user_data;
    throw std::runtime_error("libharu error: " + std::to_string(error_no) +
                             " detail: " + std::to_string(detail_no));
}

std::vector<uint8_t> export_pdf_impl(const std::vector<page>& pages, int dpi, int jpeg_quality) {
    (void)jpeg_quality; // reserved for future JPEG encoding

    HPDF_Doc pdf = HPDF_New(error_handler, nullptr);
    if (!pdf) throw std::runtime_error("failed to create PDF");

    HPDF_SetCompressionMode(pdf, HPDF_COMP_NONE); // zlib disabled build

    for (const auto& pg : pages) {
        cv::Mat rendered = render_page(pg);
        if (rendered.empty()) continue;

        int w = rendered.cols;
        int h = rendered.rows;

        // Convert BGR to RGB
        cv::Mat rgb;
        cv::cvtColor(rendered, rgb, cv::COLOR_BGR2RGB);

        float inch_w = float(w) / float(dpi);
        float inch_h = float(h) / float(dpi);

        HPDF_Page page_pdf = HPDF_AddPage(pdf);
        HPDF_Page_SetWidth(page_pdf, inch_w * 72.0f);  // 1 inch = 72 PDF points
        HPDF_Page_SetHeight(page_pdf, inch_h * 72.0f);

        // Embed uncompressed 24-bit RGB raster.
        HPDF_Image img = HPDF_LoadRawImageFromMem(
            pdf,
            rgb.data,
            static_cast<HPDF_UINT>(w),
            static_cast<HPDF_UINT>(h),
            HPDF_CS_DEVICE_RGB,
            8);

        HPDF_Page_DrawImage(page_pdf, img, 0, 0, inch_w * 72.0f, inch_h * 72.0f);
    }

    HPDF_SaveToStream(pdf);
    HPDF_UINT32 size = HPDF_GetStreamSize(pdf);
    std::vector<uint8_t> result(size);
    HPDF_ReadFromStream(pdf, result.data(), &size);
    HPDF_Free(pdf);
    return result;
}

} // namespace scanwise
