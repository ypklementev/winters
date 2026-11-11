// Metal Shading Language port of src/render/shaders.h (keep the two in sync).
// Compiled at runtime with newLibraryWithSource, so building needs only the
// Command Line Tools (no Xcode `metal` compiler).
//
// Differences from the HLSL version:
//  - instance data is read straight from buffer(0) by [[instance_id]]
//    (render/instances.h layouts), no input layouts;
//  - the view has an origin: every screen gets its own overlay window and
//    draws its part of the shared simulation space;
//  - `aa` = simulation units per device pixel. Anti-aliasing widths that the
//    HLSL code expresses as "1 px" are multiplied by it, so edges stay one
//    pixel wide when a 1x display sits next to a Retina one (aa = 2 there).
#pragma once

namespace metal_shaders {

inline constexpr char kSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct Frame {
    float2 viewOrigin;  // simulation units, top-left of this view
    float2 viewSize;    // simulation units
    float time;
    float aa;           // simulation units per pixel
    float2 padding;
};

float4 toClip(float2 p, constant Frame& f) {
    float2 q = (p - f.viewOrigin) / f.viewSize;
    return float4(q.x * 2.0 - 1.0, 1.0 - q.y * 2.0, 0.0, 1.0);
}

float2 cornerOf(uint vid) {
    return float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
}

float hash21(float2 p) {
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// ---------------------------------------------------------------------------
// Flakes
// ---------------------------------------------------------------------------
struct FlakeInstance {
    packed_float4 a;  // centre.x, centre.y, radius, alpha
    packed_float4 b;  // angle, squash, sprite, gain
};

struct FlakeOut {
    float4 pos [[position]];
    float2 uv;
    float alpha;
    float gain;
};

constant float kGrid = 8.0;

vertex FlakeOut vs_flake(uint vid [[vertex_id]], uint iid [[instance_id]],
                         const device FlakeInstance* inst [[buffer(0)]], constant Frame& f [[buffer(1)]]) {
    float4 a = float4(inst[iid].a);
    float4 b = float4(inst[iid].b);
    float2 corner = cornerOf(vid);
    // Sprites fill 88% of their cell: scale the quad so the flake's outer
    // radius matches `radius`.
    float extent = a.z / 0.88;
    float2 local = float2(corner.x * extent * b.y, corner.y * extent);
    float s = sin(b.x), c = cos(b.x);
    float2 p = a.xy + float2(local.x * c - local.y * s, local.x * s + local.y * c);

    uint idx = uint(b.z + 0.5);
    float2 cell = float2(idx % 8, idx / 8);
    FlakeOut o;
    o.pos = toClip(p, f);
    o.uv = (cell + (corner * 0.5 + 0.5)) / kGrid;
    o.alpha = a.w;
    o.gain = b.w;
    return o;
}

fragment float4 ps_flake(FlakeOut i [[stage_in]], texture2d<float> atlas [[texture(0)]]) {
    constexpr sampler smp(filter::linear, mip_filter::linear, address::clamp_to_edge);
    float a = saturate(atlas.sample(smp, i.uv).r * i.gain) * i.alpha;
    // A faint offset shadow keeps flakes readable over white windows.
    float2 shadowUv = i.uv - float2(0.035, 0.035) / kGrid;
    float sh = saturate(atlas.sample(smp, shadowUv).r * i.gain) * i.alpha * 0.3 * (1.0 - a);
    float3 col = float3(0.97, 0.98, 1.0) * a + float3(0.35, 0.40, 0.50) * sh;
    return float4(col, a + sh);
}

// ---------------------------------------------------------------------------
// Snowballs: (centre.x, centre.y, radius, rollAngle)
// ---------------------------------------------------------------------------
struct BallOut {
    float4 pos [[position]];
    float2 local;  // offset from the centre, simulation units
    float radius;
    float angle;
};

vertex BallOut vs_ball(uint vid [[vertex_id]], uint iid [[instance_id]],
                       const device packed_float4* inst [[buffer(0)]], constant Frame& f [[buffer(1)]]) {
    float4 b = float4(inst[iid]);
    float2 corner = cornerOf(vid);
    float extent = b.z + f.aa;  // room for the anti-aliased edge
    BallOut o;
    o.pos = toClip(b.xy + corner * extent, f);
    o.local = corner * extent;
    o.radius = b.z;
    o.angle = b.w;
    return o;
}

fragment float4 ps_ball(BallOut i [[stage_in]], constant Frame& f [[buffer(1)]]) {
    float d = length(i.local);
    float coverage = saturate((i.radius - d) / f.aa + 0.5);
    float2 uv = i.local / max(i.radius, 0.001);
    float3 n = float3(uv, sqrt(saturate(1.0 - dot(uv, uv))));
    float3 L = normalize(float3(-0.45, -0.65, 0.6));  // light from the upper left
    float diffuse = saturate(dot(n, L));
    float3 shadow = float3(0.70, 0.76, 0.88);
    float3 col = mix(shadow, float3(1.0, 1.0, 1.0), 0.3 + 0.7 * diffuse);
    // Lumpy packed-snow texture that turns with the ball as it rolls.
    float s = sin(i.angle), c = cos(i.angle);
    float2 ruv = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c) / max(n.z, 0.35);
    float lumps = valueNoise(ruv * 3.0 + 7.0) * 0.6 + valueNoise(ruv * 8.0 + 3.0) * 0.4;
    col *= mix(0.88, 1.04, lumps);
    col *= mix(0.82, 1.0, saturate(n.z * 3.0));  // darker rim for contrast
    return float4(col * coverage, coverage);
}

// ---------------------------------------------------------------------------
// Snow piles: one trapezoid per height-map cell.
// a = (x0, x1, baseY, unused), b = (heightLeft, heightRight)
// ---------------------------------------------------------------------------
struct PileInstance {
    packed_float4 a;
    packed_float2 b;
};

struct PileOut {
    float4 pos [[position]];
    float depth;   // below the snow surface, simulation units
    float slope;   // dh/dx of the surface (positive = rising to the right)
    float2 world;
};

vertex PileOut vs_pile(uint vid [[vertex_id]], uint iid [[instance_id]],
                       const device PileInstance* inst [[buffer(0)]], constant Frame& f [[buffer(1)]]) {
    float4 a = float4(inst[iid].a);
    float2 b = float2(inst[iid].b);
    bool right = (vid & 1) != 0;
    bool top = (vid & 2) == 0;
    float x = right ? a.y : a.x;
    float h = right ? b.y : b.x;
    float2 p = float2(x, top ? a.z - h - 0.5 * f.aa : a.z);
    PileOut o;
    o.pos = toClip(p, f);
    o.depth = top ? -0.5 * f.aa : h;
    o.slope = (b.y - b.x) / max(a.y - a.x, 0.001);
    o.world = p;
    return o;
}

fragment float4 ps_pile(PileOut i [[stage_in]], constant Frame& f [[buffer(1)]]) {
    float coverage = saturate(i.depth / f.aa + 0.5);
    // Slopes facing the upper-left light are brighter.
    float lit = 1.0 + clamp(i.slope, -1.0, 1.0) * 0.07;
    float3 body = mix(float3(0.97, 0.98, 1.0), float3(0.80, 0.85, 0.95), saturate(i.depth / 40.0)) * lit;
    body *= mix(0.96, 1.02, valueNoise(i.world * 0.35));  // grainy texture
    float rim = 1.0 - saturate(i.depth / f.aa - 0.6);     // ~1 px crust on top
    float3 col = mix(body, float3(0.76, 0.80, 0.88), rim * 0.5);
    // Rare twinkling ice crystals near the surface.
    float h = hash21(floor(i.world));
    float glint = step(0.996, h) * pow(saturate(sin(f.time * (1.5 + h * 3.0) + h * 60.0)), 12.0)
                * (1.0 - saturate(i.depth / 6.0));
    col = mix(col, float3(1.0, 1.0, 1.0), glint);
    return float4(saturate(col) * coverage, coverage);
}

// ---------------------------------------------------------------------------
// Solid shapes (wiper, snowman decorations):
// a = (centre.x, centre.y, halfLength, halfWidth), b = (angle, r, g, b),
// c = (alpha, shine, type, 0). type 0 = capsule, 1 = triangle pointing +x
// (carrot), 2 = trapezoid narrower at the top (bucket). AA via signed distance.
// ---------------------------------------------------------------------------
struct ShapeInstance {
    packed_float4 a;
    packed_float4 b;
    packed_float4 c;
};

struct ShapeOut {
    float4 pos [[position]];
    float2 local;  // in the shape's own frame
    float2 size;   // (halfLength, halfWidth)
    float4 color;
    float2 style;  // (shine, type)
};

vertex ShapeOut vs_shape(uint vid [[vertex_id]], uint iid [[instance_id]],
                         const device ShapeInstance* inst [[buffer(0)]], constant Frame& f [[buffer(1)]]) {
    float4 a = float4(inst[iid].a);
    float4 b = float4(inst[iid].b);
    float4 c4 = float4(inst[iid].c);
    float2 local = cornerOf(vid) * (a.zw + f.aa);
    float s = sin(b.x), c = cos(b.x);
    ShapeOut o;
    o.pos = toClip(a.xy + float2(local.x * c - local.y * s, local.x * s + local.y * c), f);
    o.local = local;
    o.size = a.zw;
    o.color = float4(b.yzw, c4.x);
    o.style = c4.yz;
    return o;
}

float edgeDist(float2 p, float2 a, float2 n) { return dot(p - a, normalize(n)); }

fragment float4 ps_shape(ShapeOut i [[stage_in]], constant Frame& f [[buffer(1)]]) {
    float2 p = i.local;
    float hl = i.size.x, hw = i.size.y;
    float d;
    float3 col = i.color.rgb;
    if (i.style.y < 0.5) {
        // Capsule with a cylindrical sheen across its width.
        float2 q = float2(max(abs(p.x) - (hl - hw), 0.0), p.y);
        d = length(q) - hw;
        float across = p.y / max(hw, 0.001);
        float sheen = pow(saturate(1.0 - abs(across + 0.35)), 3.0) * i.style.x;
        col = col * (1.0 - 0.35 * abs(across)) + sheen;
    } else if (i.style.y < 1.5) {
        // Triangle: base at x = -hl, apex at x = +hl. Carrot ridges.
        d = max(-hl - p.x, max(edgeDist(p, float2(-hl, -hw), float2(hw, -2.0 * hl)),
                               edgeDist(p, float2(-hl, hw), float2(hw, 2.0 * hl))));
        float ridge = step(0.55, fract((p.x + hl) / max(hl * 0.28, 1.0)));
        col *= (0.88 + 0.12 * ridge) * (1.0 - 0.25 * saturate(p.y / max(hw, 0.001)));
    } else {
        // Trapezoid: bottom half-width hl, top 0.78 hl, height 2 hw. Metal bucket.
        float t = 0.78 * hl;
        d = max(max(p.y - hw, -hw - p.y),
                max(edgeDist(p, float2(hl, hw), float2(2.0 * hw, -(hl - t))),
                    edgeDist(p, float2(-hl, hw), float2(-2.0 * hw, -(hl - t)))));
        float across = p.x / max(hl, 0.001);
        float sheen = pow(saturate(1.0 - abs(across + 0.4) * 2.2), 2.0) * i.style.x;
        col = col * (0.8 + 0.25 * (1.0 - abs(across))) + sheen;
    }
    float coverage = saturate(0.5 - d / f.aa) * i.color.a;
    return float4(saturate(col) * coverage, coverage);
}
)msl";

}  // namespace metal_shaders
