// dx11playlisttransition.hlsl — Fundido entre wallpapers de playlist.
//
// Recurso EXTERNO (res/). Nombre interno observado:
//   "assets/shaders/HLSL/dx11playlisttransition"
// FADEEFFECT: mezcla A->B con progreso 0..1.
// HDR: misma conversión que dx11fullscreen (sRGB -> Rec.2020 -> PQ).

Texture2D g_Texture0 : register(t0); // wallpaper saliente
Texture2D g_Texture1 : register(t1); // wallpaper entrante
SamplerState g_Sampler : register(s0);

cbuffer FadeParams : register(b0) {
    float g_Progress; // 0..1 (FADEEFFECT)
    float g_Hdr;      // 1 = salida HDR (PQ)
    float2 g_Pad;
};

static const float3x3 kRec709To2020 = float3x3(
    0.6274, 0.3293, 0.0433,
    0.0691, 0.9195, 0.0114,
    0.0164, 0.0880, 0.8956);

float3 SrgbToLinear(float3 c) {
    return float3(
        c.r <= 0.04045 ? c.r / 12.92 : pow(abs((c.r + 0.055) / 1.055), 2.4),
        c.g <= 0.04045 ? c.g / 12.92 : pow(abs((c.g + 0.055) / 1.055), 2.4),
        c.b <= 0.04045 ? c.b / 12.92 : pow(abs((c.b + 0.055) / 1.055), 2.4));
}

float3 LinearToPq(float3 L) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    float3 n = pow(max(L, 0.0), m1);
    return pow((c1 + c2 * n) / (1.0 + c3 * n), m2);
}

float3 EncodeHdr(float3 srgb) {
    float3 lin = mul(kRec709To2020, SrgbToLinear(saturate(srgb)));
    return LinearToPq(lin * (203.0 / 10000.0));
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 a = g_Texture0.Sample(g_Sampler, uv);
    float4 b = g_Texture1.Sample(g_Sampler, uv);
    float3 c = lerp(a.rgb, b.rgb, saturate(g_Progress));
    if (g_Hdr > 0.5) c = EncodeHdr(c);
    return float4(c, 1.0);
}
