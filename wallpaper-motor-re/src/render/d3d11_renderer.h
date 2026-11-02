// d3d11_renderer.h — Renderizador D3D11 + DirectComposition del motor.
//
// RECONSTRUIDO de DNXWallpaper.Motor.exe (clase WPEDesktopDX11Window):
//   - Ventana: WS_CHILD del WorkerW, EXSTYLE NOACTIVATE|NOREDIRECTIONBITMAP|
//     LAYERED (0x8280000) + SetLayeredWindowAttributes(alpha=255).
//   - Contenido: swapchain de COMPOSICIÓN (CreateSwapChainForComposition)
//     presentado vía DirectComposition (IDCompositionTarget/Visual).
//   Un swapchain HWND clásico no compone en el fondo del escritorio.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>   // IDXGIOutput6 / DXGI_OUTPUT_DESC1 (detección HDR)
#include <dcomp.h>
#include <string>
#include <map>

// Rutas EXTERNAS (res/, no embebidas).
extern const wchar_t* kShaderGaussianPath;    // res/shaders/dx11playlistgaussian.hlsl
extern const wchar_t* kShaderTransitionPath;  // res/shaders/dx11playlisttransition.hlsl

class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer() { Shutdown(); }

    // Crea device + swapchain de composición + visual DirectComposition
    // sobre el HWND del wallpaper.
    bool Init(HWND hwnd, int width, int height);

    // HDR: usa swapchain 16F + PQ si el monitor lo soporta.
    // Debe llamarse ANTES de Init().
    void SetHdrAllowed(bool allowed) { hdrAllowed_ = allowed; }
    bool HdrActive() const { return hdrActive_; }
    void Shutdown();

    bool LoadPlaylistShaders();

    bool SetBackgroundImage(const std::wstring& path);
    void DrawBackground();

    bool UpdateVideoFrame(const unsigned char* rgba, unsigned w, unsigned h);
    void DrawVideoFrame();

    // --- Asignación por monitor -------------------------------------------
    // Una textura de vídeo por celda (monitor). El motor puede reproducir un
    // fichero distinto en cada pantalla sobre la misma superficie.
    bool UpdateVideoFrameSlot(int slot, const unsigned char* rgba, unsigned w,
                              unsigned h);
    void ClearVideoSlot(int slot);
    void ClearVideoSlots();
    // Dibuja la textura de `slot` en la caja (x,y,w,h) del escritorio virtual,
    // con su ajuste (0 cover, 1 contain, 2 stretch) y su recorte UV.
    void DrawCell(int slot, int x, int y, int w, int h, int mode, float sx,
                  float sy, float sw, float sh);
    // Suelta el ultimo frame de video: si no, DrawWithTransition sigue
    // pintandolo y una imagen aplicada despues nunca se ve.
    void ClearVideoFrame();

    // Modo de ajuste del fondo: 0=fill(cover), 1=fit(contain), 2=stretch.
    void SetPlaybackMode(int mode) { mode_ = mode; }

    // Guarda el frame actual del swapchain como PNG (captura de pantalla).
    bool SaveBackBufferPng(const std::wstring& path);

    void BeginFrame(float r, float g, float b);
    void EndFrame(bool vsync);

    // --- Playlist / crossfade ---
    // Congela el contenido actual (imagen o frame de vídeo) para poder
    // mezclarlo con el siguiente wallpaper.
    void SnapshotCurrent();
    void ClearSnapshot();
    bool HasSnapshot() const { return snapSRV_ != nullptr; }
    // Dibuja el fondo actual; si progress < 1 y hay snapshot, mezcla.
    void DrawWithTransition(float progress);

    ID3D11Device* Device() const { return device_; }

private:
    bool EnsureFullscreenPipeline();
    ID3D11RenderTargetView* AcquireBackBufferRTV();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    IDXGISwapChain1* swap_ = nullptr;
    IDCompositionDevice* dcomp_ = nullptr;
    IDCompositionTarget* target_ = nullptr;
    IDCompositionVisual* visual_ = nullptr;
    ID3D11RenderTargetView* backRTV_ = nullptr;

    ID3D11PixelShader* gaussian_ = nullptr;
    ID3D11PixelShader* transition_ = nullptr;
    ID3D11PixelShader* fullscreenPS_ = nullptr;
    ID3D11PixelShader* transitionPS_ = nullptr;
    ID3D11VertexShader* fullscreenVS_ = nullptr;
    ID3D11SamplerState* bgSampler_ = nullptr;
    ID3D11Buffer* bgParams_ = nullptr;
    ID3D11Buffer* fadeParams_ = nullptr;
    ID3D11ShaderResourceView* snapSRV_ = nullptr;
    ID3D11Texture2D* snapTex_ = nullptr;
    unsigned snapW_ = 0, snapH_ = 0;
    ID3D11ShaderResourceView* bgSRV_ = nullptr;
    ID3D11ShaderResourceView* vidSRV_ = nullptr;
    ID3D11Texture2D* vidTex_ = nullptr;
    unsigned bgW_ = 0, bgH_ = 0;
    unsigned vidW_ = 0, vidH_ = 0;
    // Texturas de vídeo por celda (un monitor cada una).
    struct SlotTex {
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        unsigned w = 0, h = 0;
    };
    std::map<int, SlotTex> slots_;
    int viewW_ = 0, viewH_ = 0;
    int mode_ = 0;
    bool hdrAllowed_ = false;
    bool hdrActive_ = false;
};
