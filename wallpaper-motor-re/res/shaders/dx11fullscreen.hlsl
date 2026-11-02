// dx11fullscreen.hlsl — Quad a pantalla completa (fondo de imagen/vídeo).
//
// Recurso EXTERNO (res/). Vertex shader procedural (SV_VertexID, sin buffers).
// Modos de ajuste:
//   0 = fill (cover, recorta y centra)  [por defecto]
//   1 = fit  (contain, barras negras)
//   2 = stretch (deforma para llenar)
//
// HDR: si la swapchain es 16F (g_Hdr = 1) la señal se entrega en ST.2084 (PQ)
// con espacio Rec.2020, tomando 203 nits como blanco de referencia SDR (el
// mismo criterio que usa Windows para contenido SDR en un escritorio HDR).

Texture2D g_Image : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer FrameParams : register(b0) {
    float2 g_TexSize;   // tamaño de la imagen
    float2 g_ViewSize;  // tamaño del viewport
    float  g_Mode;      // 0 fill, 1 fit, 2 stretch
    float  g_Hdr;       // 1 = salida HDR (PQ), 0 = SDR
    float2 g_Pad;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
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

// EOTF inversa ST.2084 (PQ). L normalizada: 1.0 = 10000 nits.
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

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = p;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    float viewAspect = g_ViewSize.x / g_ViewSize.y;
    float texAspect = g_TexSize.x / g_TexSize.y;
    float2 uv = i.uv;

    if (g_Mode < 0.5) {
        // cover
        if (viewAspect > texAspect) {
            float h = texAspect / viewAspect;
            uv.y = (uv.y - 0.5) * h + 0.5;
        } else {
            float w = viewAspect / texAspect;
            uv.x = (uv.x - 0.5) * w + 0.5;
        }
    } else if (g_Mode < 1.5) {
        // contain: barras negras
        if (viewAspect > texAspect) {
            float w = texAspect / viewAspect;
            float minx = (1.0 - w) * 0.5;
            if (uv.x < minx || uv.x > 1.0 - minx) return float4(0, 0, 0, 1);
            uv.x = (uv.x - minx) / w;
        } else {
            float h = viewAspect / texAspect;
            float miny = (1.0 - h) * 0.5;
            if (uv.y < miny || uv.y > 1.0 - miny) return float4(0, 0, 0, 1);
            uv.y = (uv.y - miny) / h;
        }
    }
    // stretch: sin cambios
    float4 c = g_Image.Sample(g_Sampler, saturate(uv));
    if (g_Hdr > 0.5) c.rgb = EncodeHdr(c.rgb);
    return float4(c.rgb, 1.0);
}
