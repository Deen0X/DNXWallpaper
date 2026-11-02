// dx11playlistgaussian.hlsl — Blur gaussiano para transiciones de playlist.
//
// Recurso EXTERNO (res/). Nombre interno observado en wallpaper64.exe:
//   "assets/shaders/HLSL/dx11playlistgaussian"
// Compilado en tiempo de ejecución por D3D11Renderer::LoadPlaylistShaders
// (perfil ps_5_0, entry "main"). Constantes g_Texture0..N (sampler2D).

Texture2D g_Texture0 : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 g_Direction; // (1,0) horizontal, (0,1) vertical
    float g_Resolution;
    float g_Radius;
};

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    static const float kW[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float2 step = g_Direction / g_Resolution * g_Radius;
    float4 c = g_Texture0.Sample(g_Sampler, uv) * kW[0];
    for (int i = 1; i < 5; ++i) {
        c += g_Texture0.Sample(g_Sampler, uv + step * i) * kW[i];
        c += g_Texture0.Sample(g_Sampler, uv - step * i) * kW[i];
    }
    return c;
}
