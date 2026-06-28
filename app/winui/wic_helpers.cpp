#include <Windows.h>
#undef GetCurrentTime

#include "wic_helpers.h"

#include <winrt/base.h>
#include <wincodec.h>
#include <robuffer.h>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")

namespace scanwise {

namespace {

winrt::com_ptr<IWICImagingFactory> wic_factory() {
    static winrt::com_ptr<IWICImagingFactory> factory;
    if (!factory) {
        winrt::check_hresult(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.put())));
    }
    return factory;
}

std::vector<uint8_t> decode_source(IWICBitmapSource* source, int& width, int& height) {
    UINT w = 0, h = 0;
    winrt::check_hresult(source->GetSize(&w, &h));
    width = static_cast<int>(w);
    height = static_cast<int>(h);

    winrt::com_ptr<IWICFormatConverter> converter;
    winrt::check_hresult(wic_factory()->CreateFormatConverter(converter.put()));
    winrt::check_hresult(converter->Initialize(
        source,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom));

    const UINT stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    winrt::check_hresult(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()));
    return pixels;
}

} // namespace

std::vector<uint8_t> wic_decode_file_to_bgra(const std::wstring& path, int& width, int& height) {
    width = 0;
    height = 0;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(wic_factory()->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        decoder.put()));

    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    return decode_source(frame.get(), width, height);
}

std::vector<uint8_t> wic_decode_stream_to_bgra(IStream* stream, int& width, int& height) {
    width = 0;
    height = 0;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(wic_factory()->CreateDecoderFromStream(
        stream,
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        decoder.put()));

    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    return decode_source(frame.get(), width, height);
}

void bgra_to_rgba(std::vector<uint8_t>& bgra) {
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        std::swap(bgra[i], bgra[i + 2]);
    }
}

void rgba_to_bgra(std::vector<uint8_t>& rgba) {
    // Same channel swap.
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }
}

winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap
make_writeable_bitmap(int width, int height, const uint8_t* bgra) {
    using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
    WriteableBitmap bitmap(width, height);
    auto buffer = bitmap.PixelBuffer();
    winrt::com_ptr<Windows::Storage::Streams::IBufferByteAccess> byteAccess;
    winrt::check_hresult(buffer.as(IID_PPV_ARGS(byteAccess.put())));
    uint8_t* dest = nullptr;
    winrt::check_hresult(byteAccess->Buffer(&dest));
    std::memcpy(dest, bgra, static_cast<size_t>(width) * height * 4);
    bitmap.Invalidate();
    return bitmap;
}

bool wic_encode_file(const std::wstring& path, const uint8_t* bgra, int width, int height, bool jpeg, int quality) {
    try {
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(wic_factory()->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));

        winrt::com_ptr<IWICBitmapEncoder> encoder;
        GUID clsid = jpeg ? CLSID_WICJpegEncoder : CLSID_WICPngEncoder;
        winrt::check_hresult(wic_factory()->CreateEncoder(clsid, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

        winrt::com_ptr<IWICBitmap> bitmap;
        const UINT stride = static_cast<UINT>(width * 4);
        const UINT size = stride * static_cast<UINT>(height);
        winrt::check_hresult(wic_factory()->CreateBitmapFromMemory(
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            GUID_WICPixelFormat32bppBGRA,
            stride,
            size,
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bgra)),
            bitmap.put()));

        winrt::com_ptr<IWICBitmapFrameEncode> frame;
        winrt::com_ptr<IPropertyBag2> props;
        winrt::check_hresult(encoder->CreateNewFrame(frame.put(), props.put()));
        if (jpeg && props) {
            PROPBAG2 option{};
            option.pstrName = const_cast<LPWSTR>(L"ImageQuality");
            VARIANT var{};
            var.vt = VT_R4;
            var.fltVal = std::clamp(quality, 0, 100) / 100.0f;
            props->Write(1, &option, &var);
        }
        winrt::check_hresult(frame->Initialize(nullptr));
        winrt::check_hresult(frame->WriteSource(bitmap.get(), nullptr));
        winrt::check_hresult(frame->Commit());
        winrt::check_hresult(encoder->Commit());
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace scanwise
