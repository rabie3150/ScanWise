#pragma once

#include <objidl.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <string>
#include <vector>
#include <cstdint>

namespace scanwise {

// Decode an image file to 32-bit BGRA (top-down, row stride = width*4).
std::vector<uint8_t> wic_decode_file_to_bgra(const std::wstring& path, int& width, int& height);

// Decode an image from a COM IStream to 32-bit BGRA.
std::vector<uint8_t> wic_decode_stream_to_bgra(IStream* stream, int& width, int& height);

// Convert BGRA <-> RGBA in-place.
void bgra_to_rgba(std::vector<uint8_t>& bgra);
void rgba_to_bgra(std::vector<uint8_t>& rgba);

// Create a WinUI WriteableBitmap from 32-bit BGRA pixels.
winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap
make_writeable_bitmap(int width, int height, const uint8_t* bgra);

// Encode BGRA pixels to a PNG or JPEG file.
bool wic_encode_file(const std::wstring& path, const uint8_t* bgra, int width, int height, bool jpeg, int quality);

} // namespace scanwise
