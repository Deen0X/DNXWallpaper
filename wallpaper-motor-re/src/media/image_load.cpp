// image_load.cpp — Decodificación WIC.
#include "image_load.h"

#include <windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

bool LoadImageWIC(const std::wstring& path, ImageRGBA& out) {
    out = ImageRGBA{};
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        factory->Release();
        return false;
    }
    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(factory->CreateFormatConverter(&conv))) {
            if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                            WICBitmapDitherTypeNone, nullptr,
                                            0.0, WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                if (SUCCEEDED(conv->GetSize(&w, &h)) && w > 0 && h > 0 &&
                    w <= 16384 && h <= 16384) {
                    out.width = w;
                    out.height = h;
                    out.rgba.assign((size_t)w * h * 4, 0);
                    hr = conv->CopyPixels(nullptr, w * 4,
                                          (UINT)out.rgba.size(),
                                          out.rgba.data());
                    if (FAILED(hr)) {
                        out = ImageRGBA{};
                    }
                }
                conv->Release();
            }
        }
        frame->Release();
    }
    decoder->Release();
    factory->Release();
    return out.valid();
}
