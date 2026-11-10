// HLSL sources, compiled at runtime with D3DCompile (d3dcompiler_47.dll ships
// with Windows 10+), so the build needs no fxc and works with MinGW too.
// Every primitive is an instanced 4-vertex triangle strip generated from
// SV_VertexID. All outputs are premultiplied alpha.
#pragma once

namespace shaders {

inline constexpr char kSource[] = R"hlsl(
cbuffer Frame : register(b0) {
    float2 viewSize;
    float time;
    float padding;
};

Texture2D flakeAtlas : register(t0);
SamplerState atlasSampler : register(s0);

float4 toClip(float2 p) {
    return float4(p.x / viewSize.x * 2.0 - 1.0, 1.0 - p.y / viewSize.y * 2.0, 0.0, 1.0);
}

float hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float valueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// ---------------------------------------------------------------------------
// Flakes: sprite from the procedural atlas, rotated and squashed (tumbling).
// a = (centre.x, centre.y, radius, alpha), b = (angle, squash, sprite, gain)
// ---------------------------------------------------------------------------
struct FlakeIn {
    float4 a : FLAKE0;
    float4 b : FLAKE1;
    uint vid : SV_VertexID;
};

struct FlakeOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float2 cellUv : TEXCOORD1;  // 0..1 inside the sprite cell
    float alpha : TEXCOORD2;
    float gain : TEXCOORD3;
};

static const float kGrid = 8.0;

FlakeOut vs_flake(FlakeIn i) {
    float2 corner = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    // Sprites fill 88% of their cell: scale the quad so the flake's outer
    // radius matches `radius`.
    float extent = i.a.z / 0.88;
    float2 local = float2(corner.x * extent * i.b.y, corner.y * extent);
    float s, c;
    sincos(i.b.x, s, c);
    float2 p = i.a.xy + float2(local.x * c - local.y * s, local.x * s + local.y * c);

    uint idx = (uint)(i.b.z + 0.5);
    float2 cell = float2(idx % 8, idx / 8);
    FlakeOut o;
    o.pos = toClip(p);
    o.cellUv = corner * 0.5 + 0.5;
    o.uv = (cell + o.cellUv) / kGrid;
    o.alpha = i.a.w;
    o.gain = i.b.w;
    return o;
}

float4 ps_flake(FlakeOut i) : SV_Target {
    float a = saturate(flakeAtlas.Sample(atlasSampler, i.uv).r * i.gain) * i.alpha;
    // A faint offset shadow keeps flakes readable over white windows.
    float2 shadowUv = i.uv - float2(0.035, 0.035) / kGrid;
    float sh = saturate(flakeAtlas.Sample(atlasSampler, shadowUv).r * i.gain) * i.alpha * 0.3 * (1.0 - a);
    float3 col = float3(0.97, 0.98, 1.0) * a + float3(0.35, 0.40, 0.50) * sh;
    return float4(col, a + sh);
}

// ---------------------------------------------------------------------------
// Snowballs: instance = (centre.x, centre.y, radius, rollAngle)
// ---------------------------------------------------------------------------
struct BallIn {
    float4 inst : INSTANCE;
    uint vid : SV_VertexID;
};

struct BallOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;   // px offset from the centre
    float radius : TEXCOORD1;
    float angle : TEXCOORD2;
};

BallOut vs_ball(BallIn i) {
    float2 corner = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    float extent = i.inst.z + 1.0;  // room for the anti-aliased edge
    BallOut o;
    o.pos = toClip(i.inst.xy + corner * extent);
    o.local = corner * extent;
    o.radius = i.inst.z;
    o.angle = i.inst.w;
    return o;
}

float4 ps_ball(BallOut i) : SV_Target {
    float d = length(i.local);
    float coverage = saturate(i.radius + 0.5 - d);
    float2 uv = i.local / max(i.radius, 0.001);
    float3 n = float3(uv, sqrt(saturate(1.0 - dot(uv, uv))));
    float3 L = normalize(float3(-0.45, -0.65, 0.6));   // light from the upper left
    float diffuse = saturate(dot(n, L));
    float3 shadow = float3(0.70, 0.76, 0.88);
    float3 col = lerp(shadow, float3(1.0, 1.0, 1.0), 0.3 + 0.7 * diffuse);
    // Lumpy packed-snow texture that turns with the ball as it rolls.
    float s, c;
    sincos(i.angle, s, c);
    float2 ruv = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c) / max(n.z, 0.35);
    float lumps = valueNoise(ruv * 3.0 + 7.0) * 0.6 + valueNoise(ruv * 8.0 + 3.0) * 0.4;
    col *= lerp(0.88, 1.04, lumps);
    col *= lerp(0.82, 1.0, saturate(n.z * 3.0));      // darker rim for contrast
    return float4(col * coverage, coverage);
}

// ---------------------------------------------------------------------------
// Snow piles: one trapezoid per height-map cell.
// a = (x0, x1, baseY, unused), b = (heightLeft, heightRight)
// ---------------------------------------------------------------------------
struct PileIn {
    float4 a : PILE0;
    float2 b : PILE1;
    uint vid : SV_VertexID;
};

struct PileOut {
    float4 pos : SV_Position;
    float depth : TEXCOORD0;  // px below the snow surface
    float slope : TEXCOORD1;  // dh/dx of the surface (positive = rising to the right)
    float2 world : TEXCOORD2;
};

PileOut vs_pile(PileIn i) {
    bool right = (i.vid & 1) != 0;
    bool top = (i.vid & 2) == 0;
    float x = right ? i.a.y : i.a.x;
    float h = right ? i.b.y : i.b.x;
    float2 p = float2(x, top ? i.a.z - h - 0.5 : i.a.z);
    PileOut o;
    o.pos = toClip(p);
    o.depth = top ? -0.5 : h;
    o.slope = (i.b.y - i.b.x) / max(i.a.y - i.a.x, 0.001);
    o.world = p;
    return o;
}

float4 ps_pile(PileOut i) : SV_Target {
    float coverage = saturate(i.depth + 0.5);
    // Slopes facing the upper-left light are brighter.
    float lit = 1.0 + clamp(i.slope, -1.0, 1.0) * 0.07;
    float3 body = lerp(float3(0.97, 0.98, 1.0), float3(0.80, 0.85, 0.95), saturate(i.depth / 40.0)) * lit;
    body *= lerp(0.96, 1.02, valueNoise(i.world * 0.35));   // grainy texture
    float rim = 1.0 - saturate(i.depth - 0.6);              // ~1 px crust on top
    float3 col = lerp(body, float3(0.76, 0.80, 0.88), rim * 0.5);
    // Rare twinkling ice crystals near the surface.
    float h = hash21(floor(i.world));
    float glint = step(0.996, h) * pow(saturate(sin(time * (1.5 + h * 3.0) + h * 60.0)), 12.0)
                * (1.0 - saturate(i.depth / 6.0));
    col = lerp(col, float3(1.0, 1.0, 1.0), glint);
    return float4(saturate(col) * coverage, coverage);
}

// ---------------------------------------------------------------------------
// Solid capsules (the wiper): a = (centre.x, centre.y, halfLength, halfWidth),
// b = (angle, r, g, b), c = (alpha, shine, 0, 0). Rounded ends, AA edges.
// ---------------------------------------------------------------------------
struct ShapeIn {
    float4 a : SHAPE0;
    float4 b : SHAPE1;
    float4 c : SHAPE2;
    uint vid : SV_VertexID;
};

struct ShapeOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;  // px in the capsule's own frame
    float2 size : TEXCOORD1;   // (halfLength, halfWidth)
    float4 color : TEXCOORD2;
    float shine : TEXCOORD3;
};

ShapeOut vs_shape(ShapeIn i) {
    float2 corner = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    float2 local = corner * (i.a.zw + 1.0);
    float s, c;
    sincos(i.b.x, s, c);
    ShapeOut o;
    o.pos = toClip(i.a.xy + float2(local.x * c - local.y * s, local.x * s + local.y * c));
    o.local = local;
    o.size = i.a.zw;
    o.color = float4(i.b.yzw, i.c.x);
    o.shine = i.c.y;
    return o;
}

float4 ps_shape(ShapeOut i) : SV_Target {
    float r = i.size.y;
    float2 q = float2(max(abs(i.local.x) - (i.size.x - r), 0.0), i.local.y);
    float d = length(q) - r;
    float coverage = saturate(0.5 - d) * i.color.a;
    // Cylindrical highlight across the width (metal / rubber sheen).
    float across = i.local.y / max(r, 0.001);
    float sheen = pow(saturate(1.0 - abs(across + 0.35)), 3.0) * i.shine;
    float3 col = i.color.rgb * (1.0 - 0.35 * abs(across)) + sheen;
    return float4(saturate(col) * coverage, coverage);
}
)hlsl";

}  // namespace shaders
