// d3d11_renderer.cpp — D3D11 + DirectComposition (fondo del escritorio).
#include "d3d11_renderer.h"

#include <wincodec.h>
#include <vector>

#include "../media/image_load.h"

#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dcomp.lib")

const wchar_t* kShaderGaussianPath = L"res/shaders/dx11playlistgaussian.hlsl";
const wchar_t* kShaderTransitionPath = L"res/shaders/dx11playlisttransition.hlsl";

namespace {

std::string ReadFile(const std::wstring& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void DiagLog(const char* s) {
    FILE* f = nullptr;
    fopen_s(&f, "res\\dnxwallpaper.log", "a");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}

void DiagCode(const char* s, HRESULT hr) {
    char b[192];
    sprintf_s(b, "%s (0x%08X)", s, (unsigned)hr);
    DiagLog(b);
}

// Hay alguna salida (monitor) en modo HDR (ST.2084 / Rec.2020)?
bool IsHdrOutputPresent() {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))) || !f) return false;
    bool hdr = false;
    for (UINT ai = 0; !hdr; ++ai) {
        IDXGIAdapter1* ad = nullptr;
        if (f->EnumAdapters1(ai, &ad) == DXGI_ERROR_NOT_FOUND || !ad) break;
        for (UINT oi = 0; !hdr; ++oi) {
            IDXGIOutput* out = nullptr;
            if (ad->EnumOutputs(oi, &out) == DXGI_ERROR_NOT_FOUND || !out) break;
            IDXGIOutput6* out6 = nullptr;
            if (SUCCEEDED(out->QueryInterface(IID_PPV_ARGS(&out6))) && out6) {
                DXGI_OUTPUT_DESC1 d1{};
                if (SUCCEEDED(out6->GetDesc1(&d1))) {
                    hdr = d1.ColorSpace ==
                              DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                          d1.ColorSpace ==
                              DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
                }
                out6->Release();
            }
            out->Release();
        }
        ad->Release();
    }
    f->Release();
    return hdr;
}

float SrgbToLinear1(float v) {
    return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

// EOTF inversa ST.2084 (PQ). L normalizada: 1.0 = 10000 nits.
float LinearToPq1(float L) {
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    float n = powf(L > 0.0f ? L : 0.0f, m1);
    return powf((c1 + c2 * n) / (1.0f + c3 * n), m2);
}

// Blanco de referencia SDR en HDR: 203 nits (criterio de Windows).
void SrgbToPq(float r, float g, float b, float out[3]) {
    float lr = SrgbToLinear1(r), lg = SrgbToLinear1(g), lb = SrgbToLinear1(b);
    float r2 = 0.6274f * lr + 0.3293f * lg + 0.0433f * lb;
    float g2 = 0.0691f * lr + 0.9195f * lg + 0.0114f * lb;
    float b2 = 0.0164f * lr + 0.0880f * lg + 0.8956f * lb;
    const float kWhite = 203.0f / 10000.0f;
    out[0] = LinearToPq1(r2 * kWhite);
    out[1] = LinearToPq1(g2 * kWhite);
    out[2] = LinearToPq1(b2 * kWhite);
}

// PQ -> lineal (para capturar pantalla en HDR).
float PqToLinear1(float v) {
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    float p = powf(v > 0.0f ? v : 0.0f, 1.0f / m2);
    float n = (p - c1) > 0.0f ? (p - c1) / (c2 - c3 * p) : 0.0f;
    return n > 0.0f ? powf(n, 1.0f / m1) : 0.0f;
}

float LinearToSrgb1(float v) {
    return v <= 0.0031308f ? v * 12.92f
                           : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

float HalfToFloat(unsigned short h) {
    unsigned sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    float v;
    if (exp == 0) {
        v = (float)mant / 1024.0f * powf(2.0f, -14.0f);
    } else if (exp == 31) {
        v = 65504.0f;
    } else {
        v = (1.0f + (float)mant / 1024.0f) * powf(2.0f, (float)exp - 15.0f);
    }
    return sign ? -v : v;
}

} // namespace

bool D3D11Renderer::Init(HWND hwnd, int width, int height) {
    viewW_ = width;
    viewH_ = height;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, &level, 1, D3D11_SDK_VERSION,
                                   &device_, nullptr, &ctx_);
    if (FAILED(hr)) {
        DiagCode("d3d11: HARDWARE failed, WARP", hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               &level, 1, D3D11_SDK_VERSION, &device_, nullptr,
                               &ctx_);
    }
    if (FAILED(hr)) {
        DiagCode("d3d11: device failed", hr);
        return false;
    }

    // Factory2 + swapchain de composición (NO atado a OutputWindow).
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    hr = device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr)) {
        hr = dxgiDevice->GetAdapter(&adapter);
    }
    if (SUCCEEDED(hr)) {
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    }
    if (FAILED(hr)) {
        DiagCode("dcomp: factory2 failed", hr);
        if (dxgiDevice) dxgiDevice->Release();
        if (adapter) adapter->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    // HDR: swapchain 16F + PQ (Rec.2020). Si el formato no esta soportado
    // se cae a SDR sin romper nada.
    const bool wantHdr = hdrAllowed_ && IsHdrOutputPresent();
    if (wantHdr) sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.Stereo = FALSE;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForComposition(device_, &sd, nullptr, &swap_);
    if (FAILED(hr) && wantHdr) {
        DiagLog("render: HDR 16F no soportado, se usa SDR");
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        hr = factory->CreateSwapChainForComposition(device_, &sd, nullptr,
                                                    &swap_);
    }
    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    if (FAILED(hr)) {
        DiagCode("dcomp: CreateSwapChainForComposition failed", hr);
        return false;
    }

    if (wantHdr && SUCCEEDED(hr)) {
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(swap_->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3) {
            if (SUCCEEDED(sc3->SetColorSpace1(
                    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))) {
                hdrActive_ = true;
            }
            sc3->Release();
        }
    }
    DiagLog(hdrActive_ ? "render: HDR activo (16F + PQ Rec.2020)"
                       : "render: SDR (8-bit)");

    // DirectComposition sobre el HWND del wallpaper.
    hr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp_));
    if (FAILED(hr)) {
        DiagCode("dcomp: CreateDevice failed", hr);
        return false;
    }
    hr = dcomp_->CreateTargetForHwnd(hwnd, FALSE, &target_);
    if (FAILED(hr)) {
        DiagCode("dcomp: CreateTargetForHwnd failed", hr);
        return false;
    }
    hr = dcomp_->CreateVisual(&visual_);
    if (FAILED(hr)) {
        DiagCode("dcomp: CreateVisual failed", hr);
        return false;
    }
    visual_->SetContent(swap_);
    target_->SetRoot(visual_);
    hr = dcomp_->Commit();
    if (FAILED(hr)) {
        DiagCode("dcomp: Commit failed", hr);
        return false;
    }
    DiagLog("dcomp: composition swapchain ready");
    return true;
}

void D3D11Renderer::Shutdown() {
    if (backRTV_) { backRTV_->Release(); backRTV_ = nullptr; }
    if (gaussian_) { gaussian_->Release(); gaussian_ = nullptr; }
    if (transition_) { transition_->Release(); transition_ = nullptr; }
    if (fullscreenPS_) { fullscreenPS_->Release(); fullscreenPS_ = nullptr; }
    if (transitionPS_) { transitionPS_->Release(); transitionPS_ = nullptr; }
    if (fadeParams_) { fadeParams_->Release(); fadeParams_ = nullptr; }
    if (snapSRV_) { snapSRV_->Release(); snapSRV_ = nullptr; }
    if (snapTex_) { snapTex_->Release(); snapTex_ = nullptr; }
    if (fullscreenVS_) { fullscreenVS_->Release(); fullscreenVS_ = nullptr; }
    if (bgSampler_) { bgSampler_->Release(); bgSampler_ = nullptr; }
    if (bgParams_) { bgParams_->Release(); bgParams_ = nullptr; }
    if (bgSRV_) { bgSRV_->Release(); bgSRV_ = nullptr; }
    if (vidSRV_) { vidSRV_->Release(); vidSRV_ = nullptr; }
    if (vidTex_) { vidTex_->Release(); vidTex_ = nullptr; }
    for (auto& kv : slots_) {
        if (kv.second.srv) kv.second.srv->Release();
        if (kv.second.tex) kv.second.tex->Release();
    }
    slots_.clear();
    bgW_ = bgH_ = vidW_ = vidH_ = 0;
    if (visual_) { visual_->Release(); visual_ = nullptr; }
    if (target_) { target_->Release(); target_ = nullptr; }
    if (dcomp_) { dcomp_->Release(); dcomp_ = nullptr; }
    if (swap_) { swap_->Release(); swap_ = nullptr; }
    if (ctx_) { ctx_->Release(); ctx_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

bool D3D11Renderer::LoadPlaylistShaders() {
    auto compile = [&](const std::wstring& path, ID3D11PixelShader** out) {
        std::string src = ReadFile(path);
        if (src.empty()) {
            return false;
        }
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
                                "main", "ps_5_0", 0, 0, &blob, &err);
        if (err) { err->Release(); }
        if (FAILED(hr)) {
            return false;
        }
        hr = device_->CreatePixelShader(blob->GetBufferPointer(),
                                        blob->GetBufferSize(), nullptr, out);
        blob->Release();
        return SUCCEEDED(hr);
    };
    return compile(kShaderGaussianPath, &gaussian_) &&
           compile(kShaderTransitionPath, &transition_);
}

bool D3D11Renderer::SetBackgroundImage(const std::wstring& path) {
    if (bgSRV_) {
        bgSRV_->Release();
        bgSRV_ = nullptr;
    }
    bgW_ = bgH_ = 0;
    if (path.empty()) {
        return true;
    }
    ImageRGBA img;
    if (!LoadImageWIC(path, img)) {
        DiagLog("bg: WIC decode FAILED");
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = img.width;
    td.Height = img.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = img.rgba.data();
    init.SysMemPitch = img.width * 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device_->CreateTexture2D(&td, &init, &tex))) {
        return false;
    }
    HRESULT hr = device_->CreateShaderResourceView(tex, nullptr, &bgSRV_);
    tex->Release();
    if (FAILED(hr)) {
        bgSRV_ = nullptr;
        return false;
    }
    bgW_ = img.width;
    bgH_ = img.height;
    DiagLog("bg: texture ready");
    return true;
}

bool D3D11Renderer::EnsureFullscreenPipeline() {
    if (fullscreenPS_) {
        return true;
    }
    std::string src = ReadFile(L"res/shaders/dx11fullscreen.hlsl");
    if (src.empty()) {
        return false;
    }
    auto compile = [&](const char* profile, const char* entry, ID3DBlob** out) {
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(src.data(), src.size(), "dx11fullscreen.hlsl",
                                nullptr, nullptr, entry, profile, 0, 0, out,
                                &err);
        if (err) { err->Release(); }
        return SUCCEEDED(hr);
    };
    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!compile("vs_5_0", "VSMain", &vs) || !compile("ps_5_0", "PSMain", &ps)) {
        DiagLog("bg: fullscreen shader COMPILE FAILED");
        if (vs) vs->Release();
        if (ps) ps->Release();
        return false;
    }
    device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                nullptr, &fullscreenVS_);
    device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                               nullptr, &fullscreenPS_);
    vs->Release();
    ps->Release();
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, &bgSampler_);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 48;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&bd, nullptr, &bgParams_);
    D3D11_BUFFER_DESC fb{};
    fb.ByteWidth = 16;
    fb.Usage = D3D11_USAGE_DYNAMIC;
    fb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    fb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&fb, nullptr, &fadeParams_);
    DiagLog("bg: fullscreen shader compiled");
    return fullscreenPS_ != nullptr;
}

ID3D11RenderTargetView* D3D11Renderer::AcquireBackBufferRTV() {
    if (!swap_) {
        return nullptr;
    }
    if (!backRTV_) {
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) {
            return nullptr;
        }
        device_->CreateRenderTargetView(back, nullptr, &backRTV_);
        back->Release();
    }
    return backRTV_;
}

void D3D11Renderer::DrawBackground() {
    if (!ctx_ || !bgSRV_ || !EnsureFullscreenPipeline()) {
        return;
    }
    ID3D11RenderTargetView* rtv = AcquireBackBufferRTV();
    if (!rtv) {
        return;
    }
    D3D11_VIEWPORT vp{};
    vp.Width = (float)viewW_;
    vp.Height = (float)viewH_;
    vp.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx_->Map(bgParams_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* f = (float*)map.pData;
        f[0] = (float)bgW_;
        f[1] = (float)bgH_;
        f[2] = (float)viewW_;
        f[3] = (float)viewH_;
        f[4] = (float)mode_;
        f[5] = hdrActive_ ? 1.0f : 0.0f;
        f[6] = f[7] = 0.0f;
        // Sin zoom: se ve el medio completo.
        f[8] = 0.0f; f[9] = 0.0f; f[10] = 1.0f; f[11] = 1.0f;
        ctx_->Unmap(bgParams_, 0);
    }
    ctx_->VSSetShader(fullscreenVS_, nullptr, 0);
    ctx_->PSSetShader(fullscreenPS_, nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, &bgSRV_);
    ctx_->PSSetSamplers(0, 1, &bgSampler_);
    ctx_->VSSetConstantBuffers(0, 1, &bgParams_);
    ctx_->PSSetConstantBuffers(0, 1, &bgParams_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
}

void D3D11Renderer::ClearVideoFrame() {
    if (vidSRV_) {
        vidSRV_->Release();
        vidSRV_ = nullptr;
    }
    if (vidTex_) {
        vidTex_->Release();
        vidTex_ = nullptr;
    }
    vidW_ = vidH_ = 0;
}

bool D3D11Renderer::UpdateVideoFrame(const unsigned char* rgba, unsigned w,
                                     unsigned h) {
    if (!rgba || !w || !h) {
        return false;
    }
    if (!vidTex_ || w != vidW_ || h != vidH_) {
        if (vidSRV_) { vidSRV_->Release(); vidSRV_ = nullptr; }
        if (vidTex_) { vidTex_->Release(); vidTex_ = nullptr; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &vidTex_))) {
            return false;
        }
        if (FAILED(device_->CreateShaderResourceView(vidTex_, nullptr, &vidSRV_))) {
            return false;
        }
        vidW_ = w;
        vidH_ = h;
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx_->Map(vidTex_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        return false;
    }
    const unsigned char* src = rgba;
    unsigned char* dst = (unsigned char*)map.pData;
    for (unsigned y = 0; y < h; ++y) {
        memcpy(dst, src, (size_t)w * 4);
        src += (size_t)w * 4;
        dst += map.RowPitch;
    }
    ctx_->Unmap(vidTex_, 0);
    return true;
}

void D3D11Renderer::DrawVideoFrame() {
    if (!ctx_ || !vidSRV_ || !EnsureFullscreenPipeline()) {
        return;
    }
    ID3D11ShaderResourceView* keep = bgSRV_;
    bgSRV_ = vidSRV_;
    unsigned kw = bgW_, kh = bgH_;
    bgW_ = vidW_;
    bgH_ = vidH_;
    DrawBackground();
    bgSRV_ = keep;
    bgW_ = kw;
    bgH_ = kh;
}

// ---- Asignación por monitor ------------------------------------------------
bool D3D11Renderer::UpdateVideoFrameSlot(int slot, const unsigned char* rgba,
                                         unsigned w, unsigned h) {
    if (!device_ || !ctx_ || !rgba || !w || !h) return false;
    SlotTex& st = slots_[slot];
    if (!st.tex || w != st.w || h != st.h) {
        if (st.srv) { st.srv->Release(); st.srv = nullptr; }
        if (st.tex) { st.tex->Release(); st.tex = nullptr; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &st.tex))) return false;
        if (FAILED(device_->CreateShaderResourceView(st.tex, nullptr, &st.srv))) {
            st.tex->Release();
            st.tex = nullptr;
            return false;
        }
        st.w = w;
        st.h = h;
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx_->Map(st.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        return false;
    }
    const unsigned char* src = rgba;
    unsigned char* dst = (unsigned char*)map.pData;
    for (unsigned y = 0; y < h; ++y) {
        memcpy(dst, src, (size_t)w * 4);
        src += (size_t)w * 4;
        dst += map.RowPitch;
    }
    ctx_->Unmap(st.tex, 0);
    return true;
}

void D3D11Renderer::ClearVideoSlot(int slot) {
    auto it = slots_.find(slot);
    if (it == slots_.end()) return;
    if (it->second.srv) it->second.srv->Release();
    if (it->second.tex) it->second.tex->Release();
    slots_.erase(it);
}

void D3D11Renderer::ClearVideoSlots() {
    for (auto& kv : slots_) {
        if (kv.second.srv) kv.second.srv->Release();
        if (kv.second.tex) kv.second.tex->Release();
    }
    slots_.clear();
}

void D3D11Renderer::DrawCell(int slot, int x, int y, int w, int h, int mode,
                             float sx, float sy, float sw, float sh) {
    auto it = slots_.find(slot);
    if (it == slots_.end() || !it->second.srv || !ctx_ ||
        !EnsureFullscreenPipeline()) {
        return;
    }
    ID3D11RenderTargetView* rtv = AcquireBackBufferRTV();
    if (!rtv) return;
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = (float)x;
    vp.TopLeftY = (float)y;
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx_->Map(bgParams_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* f = (float*)map.pData;
        f[0] = (float)it->second.w;
        f[1] = (float)it->second.h;
        f[2] = (float)w;
        f[3] = (float)h;
        f[4] = (float)mode;
        f[5] = hdrActive_ ? 1.0f : 0.0f;
        f[6] = f[7] = 0.0f;
        f[8] = sx; f[9] = sy; f[10] = sw; f[11] = sh;
        ctx_->Unmap(bgParams_, 0);
    }
    ctx_->VSSetShader(fullscreenVS_, nullptr, 0);
    ctx_->PSSetShader(fullscreenPS_, nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, &it->second.srv);
    ctx_->PSSetSamplers(0, 1, &bgSampler_);
    ctx_->VSSetConstantBuffers(0, 1, &bgParams_);
    ctx_->PSSetConstantBuffers(0, 1, &bgParams_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
}

bool D3D11Renderer::SaveBackBufferPng(const std::wstring& path) {
    if (!device_ || !ctx_ || !swap_) return false;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(device_->CreateTexture2D(&d, nullptr, &staging)) && staging) {
        ctx_->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx_->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            std::vector<unsigned char> rgba((size_t)d.Width * d.Height * 4);
            const bool srcHdr = (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
            for (unsigned y = 0; y < d.Height; ++y) {
                const unsigned char* row =
                    (const unsigned char*)map.pData + (size_t)y * map.RowPitch;
                unsigned char* dst = rgba.data() + (size_t)y * d.Width * 4;
                for (unsigned x = 0; x < d.Width; ++x) {
                    if (srcHdr) {
                        // Media: 4 half-float en PQ. Se deshace PQ y se
                        // reescala por el blanco SDR (203 nits) -> sRGB.
                        const unsigned short* h4 =
                            (const unsigned short*)(row + (size_t)x * 8);
                        float rgb[3];
                        for (int k = 0; k < 3; ++k) {
                            float lin = PqToLinear1(HalfToFloat(h4[k])) *
                                        (10000.0f / 203.0f);
                            float sv = LinearToSrgb1(
                                lin < 0.0f ? 0.0f : (lin > 1.0f ? 1.0f : lin));
                            rgb[k] = sv;
                        }
                        dst[x * 4 + 0] = (unsigned char)(rgb[2] * 255.0f + 0.5f);
                        dst[x * 4 + 1] = (unsigned char)(rgb[1] * 255.0f + 0.5f);
                        dst[x * 4 + 2] = (unsigned char)(rgb[0] * 255.0f + 0.5f);
                    } else {
                        const unsigned char* src = row + (size_t)x * 4;
                        dst[x * 4 + 0] = src[2];
                        dst[x * 4 + 1] = src[1];
                        dst[x * 4 + 2] = src[0];
                    }
                    dst[x * 4 + 3] = 255;
                }
            }
            ctx_->Unmap(staging, 0);
            // WIC PNG
            IWICImagingFactory* wic = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&wic)))) {
                IWICStream* stream = nullptr;
                IWICBitmapEncoder* enc = nullptr;
                IWICBitmapFrameEncode* frame = nullptr;
                if (SUCCEEDED(wic->CreateStream(&stream)) &&
                    SUCCEEDED(stream->InitializeFromFilename(path.c_str(),
                                                             GENERIC_WRITE)) &&
                    SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,
                                                 nullptr, &enc)) &&
                    SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
                    SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
                    SUCCEEDED(frame->Initialize(nullptr))) {
                    frame->SetSize(d.Width, d.Height);
                    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
                    frame->SetPixelFormat(&fmt);
                    if (SUCCEEDED(frame->WritePixels(
                            d.Height, d.Width * 4, (UINT)rgba.size(),
                            rgba.data())) &&
                        SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
                        ok = true;
                    }
                }
                if (frame) frame->Release();
                if (enc) enc->Release();
                if (stream) stream->Release();
                wic->Release();
            }
        }
    }
    if (staging) staging->Release();
    back->Release();
    return ok;
}

void D3D11Renderer::BeginFrame(float r, float g, float b) {
    ID3D11RenderTargetView* rtv = AcquireBackBufferRTV();
    if (!rtv) {
        return;
    }
    float c[4] = {r, g, b, 1.0f};
    if (hdrActive_) {
        float pq[3];
        SrgbToPq(r, g, b, pq);
        c[0] = pq[0];
        c[1] = pq[1];
        c[2] = pq[2];
    }
    ctx_->ClearRenderTargetView(rtv, c);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
}

void D3D11Renderer::EndFrame(bool vsync) {
    if (!swap_) {
        return;
    }
    swap_->Present(vsync ? 1 : 0, 0);
    if (dcomp_) {
        dcomp_->Commit();
    }
}

void D3D11Renderer::SnapshotCurrent() {
    if (!device_ || !ctx_) {
        return;
    }
    // Textura origen: vídeo si hay frame, si no la imagen de fondo.
    ID3D11ShaderResourceView* src = vidSRV_ ? vidSRV_ : bgSRV_;
    unsigned w = vidSRV_ ? vidW_ : bgW_;
    unsigned h = vidSRV_ ? vidH_ : bgH_;
    if (!src || !w || !h) {
        return;
    }
    if (!snapTex_ || w != snapW_ || h != snapH_) {
        if (snapSRV_) { snapSRV_->Release(); snapSRV_ = nullptr; }
        if (snapTex_) { snapTex_->Release(); snapTex_ = nullptr; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &snapTex_))) {
            return;
        }
        if (FAILED(device_->CreateShaderResourceView(snapTex_, nullptr,
                                                     &snapSRV_))) {
            snapTex_->Release();
            snapTex_ = nullptr;
            return;
        }
        snapW_ = w;
        snapH_ = h;
    }
    // Copiar el recurso origen a la textura de snapshot.
    ID3D11Resource* dstRes = nullptr;
    snapSRV_->GetResource(&dstRes);
    ID3D11Resource* srcRes = nullptr;
    src->GetResource(&srcRes);
    if (dstRes && srcRes) {
        D3D11_TEXTURE2D_DESC sd{}, dd{};
        ((ID3D11Texture2D*)srcRes)->GetDesc(&sd);
        ((ID3D11Texture2D*)dstRes)->GetDesc(&dd);
        if (sd.Width == dd.Width && sd.Height == dd.Height &&
            sd.Format == dd.Format) {
            ctx_->CopyResource(dstRes, srcRes);
        }
    }
    if (dstRes) dstRes->Release();
    if (srcRes) srcRes->Release();
    DiagLog("playlist: snapshot taken");
}

void D3D11Renderer::ClearSnapshot() {
    if (snapSRV_) { snapSRV_->Release(); snapSRV_ = nullptr; }
    if (snapTex_) { snapTex_->Release(); snapTex_ = nullptr; }
    snapW_ = snapH_ = 0;
}

void D3D11Renderer::DrawWithTransition(float progress) {
    if (!snapSRV_ || progress >= 1.0f) {
        if (vidSRV_) {
            DrawVideoFrame();
        } else {
            DrawBackground();
        }
        if (progress >= 1.0f) {
            ClearSnapshot();
        }
        return;
    }
    // Compilar el shader de transición bajo demanda (externo).
    if (!transitionPS_) {
        std::string src = ReadFile(L"res/shaders/dx11playlisttransition.hlsl");
        if (src.empty()) {
            DrawWithTransition(1.0f);
            return;
        }
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(src.data(), src.size(),
                                "dx11playlisttransition.hlsl", nullptr, nullptr,
                                "main", "ps_5_0", 0, 0, &blob, &err);
        if (err) err->Release();
        if (FAILED(hr)) {
            DiagLog("playlist: transition shader failed");
            DrawWithTransition(1.0f);
            return;
        }
        device_->CreatePixelShader(blob->GetBufferPointer(),
                                   blob->GetBufferSize(), nullptr,
                                   &transitionPS_);
        blob->Release();
    }
    if (!transitionPS_ || !EnsureFullscreenPipeline()) {
        DrawWithTransition(1.0f);
        return;
    }
    ID3D11RenderTargetView* rtv = AcquireBackBufferRTV();
    if (!rtv) {
        return;
    }
    D3D11_VIEWPORT vp{};
    vp.Width = (float)viewW_;
    vp.Height = (float)viewH_;
    vp.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    // Progress
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx_->Map(fadeParams_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* f = (float*)map.pData;
        f[0] = progress;
        f[1] = hdrActive_ ? 1.0f : 0.0f;
        f[2] = f[3] = 0.0f;
        ctx_->Unmap(fadeParams_, 0);
    }
    ID3D11ShaderResourceView* srvs[2] = {
        snapSRV_, vidSRV_ ? vidSRV_ : bgSRV_,
    };
    ctx_->VSSetShader(fullscreenVS_, nullptr, 0);
    ctx_->PSSetShader(transitionPS_, nullptr, 0);
    ctx_->PSSetShaderResources(0, 2, srvs);
    ctx_->PSSetSamplers(0, 1, &bgSampler_);
    ctx_->PSSetConstantBuffers(0, 1, &fadeParams_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrv[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nullSrv);
}
