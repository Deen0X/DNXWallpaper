// video_wallpaper.cpp — Lector de vídeo Media Foundation (hilo propio).
//
// RECONSTRUIDO de DNXWallpaper.Motor.exe (VideoWallpaper::StartVideoWithNewPlayer):
//   IMFSourceReader + MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING
//   (convierte NV12 -> RGB32), lectura en bucle con rebobinado, entrega de
//   frames RGBA8 al renderizador (DirectComposition).
#include "video_wallpaper.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstdio>
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")

namespace {
constexpr DWORD kVideoStream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;

void VidLog(const char* msg, HRESULT hr) {
    char b[256];
    sprintf_s(b, "video: %s (0x%08X)", msg, (unsigned)hr);
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "%s\n", b);
        fclose(f);
    }
}
} // namespace

bool VideoWallpaper::StartVideoWithNewPlayer(const std::wstring& path) {
    Stop();
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        VidLog("MFStartup failed", hr);
        return false;
    }
    IMFAttributes* attrs = nullptr;
    if (SUCCEEDED(MFCreateAttributes(&attrs, 2))) {
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }
    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(path.c_str(), attrs, &reader);
    if (attrs) {
        attrs->Release();
    }
    if (FAILED(hr)) {
        VidLog("MFCreateSourceReaderFromURL failed", hr);
        MFShutdown();
        return false;
    }
    // Calidad: tamano nativo del flujo (para poder limitarlo).
    UINT32 nativeW = 0, nativeH = 0;
    {
        IMFMediaType* nativeType = nullptr;
        if (SUCCEEDED(reader->GetNativeMediaType(kVideoStream, 0, &nativeType)) &&
            nativeType) {
            MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &nativeW, &nativeH);
            nativeType->Release();
        }
    }

    IMFMediaType* rgb = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&rgb))) {
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        // Si la calidad limita la altura, se pide al conversor de MF que
        // reescale (MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING).
        if (maxHeight_ > 0 && nativeH > (UINT32)maxHeight_) {
            UINT32 th = (UINT32)maxHeight_;
            UINT32 tw = (UINT32)(((double)nativeW * th) / nativeH + 0.5);
            tw &= ~1u;  // par (NV12/RGB32)
            th &= ~1u;
            if (tw > 0 && th > 0) {
                MFSetAttributeSize(rgb, MF_MT_FRAME_SIZE, tw, th);
                char b[192];
                sprintf_s(b,
                          "video: calidad %dx%d -> %ux%u (tope %d p)",
                          nativeW, nativeH, tw, th, maxHeight_);
                FILE* f = nullptr;
                fopen_s(&f, "res\\dnxwallpaper.log", "a");
                if (f) {
                    fprintf(f, "%s\n", b);
                    fclose(f);
                }
            }
        }
        hr = reader->SetCurrentMediaType(kVideoStream, nullptr, rgb);
        rgb->Release();
        if (FAILED(hr)) {
            VidLog("SetCurrentMediaType RGB32 failed", hr);
            reader->Release();
            MFShutdown();
            return false;
        }
    }
    reader_ = reader;
    path_ = path;
    stopRequested_ = false;
    playing_ = true;
    thread_ = CreateThread(nullptr, 0, ReadThread, this, 0, nullptr);
    if (!thread_) {
        reader->Release();
        reader_ = nullptr;
        playing_ = false;
        MFShutdown();
        return false;
    }
    return true;
}

void VideoWallpaper::Stop() {
    if (thread_) {
        stopRequested_ = true;
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (reader_) {
        ((IMFSourceReader*)reader_)->Release();
        reader_ = nullptr;
    }
    if (playing_) {
        playing_ = false;
        MFShutdown();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    frameFresh_ = false;
    last_.valid = false;
}

bool VideoWallpaper::TakeFrame(VideoFrame& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frameFresh_ || !last_.valid) {
        return false;
    }
    out = last_;
    frameFresh_ = false;
    return true;
}

unsigned long __stdcall VideoWallpaper::ReadThread(void* self) {
    ((VideoWallpaper*)self)->ReadLoop();
    return 0;
}

void VideoWallpaper::ReadLoop() {
    auto* reader = (IMFSourceReader*)reader_;
    DWORD prevTicks = GetTickCount();
    while (!stopRequested_) {
        if (paused_) {
            Sleep(100);
            continue;
        }
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader->ReadSample(kVideoStream, 0, nullptr, &flags, &ts,
                                        &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (sample) {
                sample->Release();
            }
            PROPVARIANT pos{};
            pos.vt = VT_I8;
            pos.hVal.QuadPart = 0;
            if (FAILED(reader->SetCurrentPosition(GUID_NULL, pos))) {
                break;
            }
            PropVariantClear(&pos);
            continue;
        }
        if (!sample) {
            continue;
        }
        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
            UINT w = 0, h = 0;
            IMFMediaType* mt = nullptr;
            if (SUCCEEDED(reader->GetCurrentMediaType(kVideoStream, &mt))) {
                MFGetAttributeSize(mt, MF_MT_FRAME_SIZE, &w, &h);
                mt->Release();
            }
            BYTE* data = nullptr;
            DWORD len = 0;
            LONG pitch = 0;
            bool locked2d = false;
            IMF2DBuffer* b2d = nullptr;
            if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2d)))) {
                BYTE* scan0 = nullptr;
                if (SUCCEEDED(b2d->Lock2D(&scan0, &pitch))) {
                    data = scan0;
                    len = (DWORD)(pitch < 0 ? -pitch : pitch) * (h ? h : 1);
                    locked2d = true;
                }
            }
            if (!locked2d && SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                pitch = (LONG)(w * 4);
            }
            if (data && w > 0 && h > 0) {
                DWORD stride = (DWORD)(pitch < 0 ? -pitch : pitch);
                if (stride < w * 4) {
                    stride = w * 4;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                last_.width = w;
                last_.height = h;
                last_.rgba.assign((size_t)w * h * 4, 0);
                bool bottomUp = pitch < 0;
                for (UINT y = 0; y < h; ++y) {
                    const BYTE* src =
                        data + (size_t)(bottomUp ? (h - 1 - y) : y) * stride;
                    BYTE* dst = last_.rgba.data() + (size_t)y * w * 4;
                    for (UINT x = 0; x < w; ++x) {
                        dst[0] = src[2];
                        dst[1] = src[1];
                        dst[2] = src[0];
                        dst[3] = src[3];
                        src += 4;
                        dst += 4;
                    }
                }
                last_.timestamp100ns = ts;
                last_.valid = true;
                frameFresh_ = true;
                static int sFrames = 0;
                if (sFrames < 2) {
                    char b[160];
                    sprintf_s(b, "video: frame %ux%u pitch=%ld", w, h,
                              (long)pitch);
                    FILE* f = nullptr;
                    fopen_s(&f, "res\\dnxwallpaper.log", "a");
                    if (f) { fprintf(f, "%s\n", b); fclose(f); }
                    ++sFrames;
                }
            }
            if (b2d) {
                if (locked2d) {
                    b2d->Unlock2D();
                }
                b2d->Release();
            } else if (data) {
                buf->Unlock();
            }
            buf->Release();
        }
        sample->Release();
        DWORD now = GetTickCount();
        if (now - prevTicks < 33) {
            Sleep(33 - (now - prevTicks));
        }
        prevTicks = GetTickCount();
    }
}
