#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut amiga_vertex(uint vid [[vertex_id]]) {
    constexpr float2 pos[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };
    constexpr float2 uvs[4] = {
        float2(0.0, 1.0),
        float2(1.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 0.0)
    };
    VertexOut out;
    out.position = float4(pos[vid], 0.0, 1.0);
    out.uv = uvs[vid];
    return out;
}

fragment float4 amiga_fragment(VertexOut in [[stage_in]],
                               texture2d<float> screenTex [[texture(0)]]) {
    float2 texSize = float2(screenTex.get_width(), screenTex.get_height());
    float2 texel = in.uv * texSize + 0.5;
    float2 i = floor(texel);
    float2 f = fract(texel);
    float2 fw = max(fwidth(texel), float2(1e-4));
    float2 t = clamp((f - 0.5) / fw + 0.5, 0.0, 1.0);
    float2 uv = (i - 0.5 + t) / texSize;
    constexpr sampler s(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    return screenTex.sample(s, uv);
}
