// HLSL sources, compiled at runtime with D3DCompile (d3dcompiler_47.dll ships
// with Windows 10+), so the build needs no fxc and works with MinGW too.
// Every primitive is an instanced 4-vertex triangle strip generated from
// SV_VertexID. All outputs are premultiplied alpha.
#pragma once

namespace shaders {

inline constexpr char kSource[] = R"hlsl(
cbuffer Frame : register(b0) {
    float2 viewSize;
    float2 padding;
};

float4 toClip(float2 p) {
    return float4(p.x / viewSize.x * 2.0 - 1.0, 1.0 - p.y / viewSize.y * 2.0, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Discs (flakes and snowballs): instance = (centre.x, centre.y, radius, alpha)
// ---------------------------------------------------------------------------
struct DiscIn {
    float4 inst : INSTANCE;
    uint vid : SV_VertexID;
};

struct DiscOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;   // px offset from the centre
    float radius : TEXCOORD1;
    float alpha : TEXCOORD2;
};

DiscOut vs_disc(DiscIn i) {
    float2 corner = float2((i.vid & 1) ? 1.0 : -1.0, (i.vid & 2) ? 1.0 : -1.0);
    float extent = i.inst.z + 1.0;  // room for the anti-aliased edge
    DiscOut o;
    o.pos = toClip(i.inst.xy + corner * extent);
    o.local = corner * extent;
    o.radius = i.inst.z;
    o.alpha = i.inst.w;
    return o;
}

float4 ps_flake(DiscOut i) : SV_Target {
    float d = length(i.local);
    float coverage = saturate(i.radius + 0.5 - d);
    // Slightly grey rim so flakes stay visible on white windows.
    float core = saturate(1.0 - d / max(i.radius, 0.001));
    float shade = lerp(0.72, 1.0, sqrt(core));
    float a = coverage * i.alpha;
    return float4(shade.xxx * a, a);
}

float4 ps_ball(DiscOut i) : SV_Target {
    float d = length(i.local);
    float coverage = saturate(i.radius + 0.5 - d);
    float2 uv = i.local / max(i.radius, 0.001);
    float3 n = float3(uv, sqrt(saturate(1.0 - dot(uv, uv))));
    float3 L = normalize(float3(-0.45, -0.65, 0.6));   // light from the upper left
    float diffuse = saturate(dot(n, L));
    float3 shadow = float3(0.70, 0.76, 0.88);
    float3 col = lerp(shadow, float3(1.0, 1.0, 1.0), 0.3 + 0.7 * diffuse);
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
};

PileOut vs_pile(PileIn i) {
    bool right = (i.vid & 1) != 0;
    bool top = (i.vid & 2) == 0;
    float x = right ? i.a.y : i.a.x;
    float h = right ? i.b.y : i.b.x;
    PileOut o;
    o.pos = toClip(float2(x, top ? i.a.z - h - 0.5 : i.a.z));
    o.depth = top ? -0.5 : h;
    return o;
}

float4 ps_pile(PileOut i) : SV_Target {
    float coverage = saturate(i.depth + 0.5);
    float3 body = lerp(float3(1.0, 1.0, 1.0), float3(0.84, 0.88, 0.96), saturate(i.depth / 40.0));
    float rim = 1.0 - saturate(i.depth - 0.6);         // ~1 px crust on top
    float3 col = lerp(body, float3(0.78, 0.82, 0.90), rim * 0.55);
    return float4(col * coverage, coverage);
}
)hlsl";

}  // namespace shaders
