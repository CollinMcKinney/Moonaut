#version 430 core

// =============================================================================
// vbao.frag — Visibility Bitmask Ambient Occlusion (fragment stage, half-res)
//
// Screen-space AO computed from the opaque depth + view-normal buffers at
// half the render resolution. For each pixel and each of N slice directions,
// marches along the depth buffer and records the angular sectors (bits) in
// which an occluder was found. The AO factor is 1 - popcount(mask) / 32.
//
// Optimisations relative to the naive version:
//
//   1. Half-resolution target. AO is a low-frequency signal; the bilateral
//      blur then upsamples with GL_LINEAR. Cuts fetch traffic by 4x with
//      no visible quality loss.
//
//   2. Cheap view-position reconstruction. Uses four precomputed scalars
//      extracted from the projection matrix on the CPU. Replaces a full
//      mat4*vec4 + divide with a handful of FMAs.
//
//   3. Folded length/normalize. Uses dot(diff,diff) plus a single sqrt and
//      one divide instead of length() followed by diff/dist.
//
//   4. Hardcoded slice directions. Replaces 8 sin/cos pairs per pixel with
//      one sin/cos pair for the per-pixel rotation and a table lookup.
//
//   5. bitCount instead of the HLSL name popCount.
// =============================================================================

// --- Configuration ---
#define AO_NUM_DIRECTIONS   8      // Number of slice directions per pixel
#define AO_NUM_STEPS        6      // Steps per slice, per side
#define AO_BITMASK_LEN      32     // Sectors in the visibility bitmask

// --- Inputs ---
layout(binding = 0) uniform sampler2D uDepthTex;   // full-res
layout(binding = 1) uniform sampler2D uNormalTex;  // full-res

layout(std140, row_major, binding = 0) uniform VBAOUniforms {
    mat4  uInvProj;
    mat4  uProj;
    mat4  uView;
    vec2  uScreenSize;      // HALF-RES resolution (the AO target size)
    float uNear;
    float uFar;
    // Precomputed on CPU from uProj:
    float uProjA;           // proj[2][2]
    float uProjB;           // -proj[2][3]
    float uInvProj00;       // 1.0 / proj[0][0]
    float uInvProj11;       // 1.0 / proj[1][1]
};

out float FragAO;

// --- Cheap view-position reconstruction ---
//
// Solves ndc_z = -A - B/view_z for view_z, then scales NDC xy back into view
// space. For a standard OpenGL perspective projection view_z is negative in
// front of the camera, matching the sign of the old mat4 reconstruction.
vec3 reconstruct_view_pos(float depth, vec2 uv) {
    float ndc_z  = depth * 2.0 - 1.0;
    float view_z = uProjB / (ndc_z + uProjA);
    vec2  ndc_xy = uv * 2.0 - 1.0;
    return vec3(-ndc_xy.x * view_z * uInvProj00,
                -ndc_xy.y * view_z * uInvProj11,
                 view_z);
}

// Robust depth: min of the 2x2 full-res block under this half-res pixel.
// Using the closest sample avoids picking up background behind a thin
// foreground object that straddles the block. Called once per pixel.
float robust_depth(ivec2 hp) {
    ivec2 fp = hp * 2;
    float d00 = texelFetch(uDepthTex, fp + ivec2(0, 0), 0).r;
    float d10 = texelFetch(uDepthTex, fp + ivec2(1, 0), 0).r;
    float d01 = texelFetch(uDepthTex, fp + ivec2(0, 1), 0).r;
    float d11 = texelFetch(uDepthTex, fp + ivec2(1, 1), 0).r;
    return min(min(d00, d10), min(d01, d11));
}

vec3 get_view_pos_center(ivec2 hp) {
    float d  = robust_depth(hp);
    vec2  uv = (vec2(hp) + 0.5) / uScreenSize;
    return reconstruct_view_pos(d, uv);
}

// Single-corner fetch for march samples. Slight loss of robustness vs
// robust_depth, but acceptable because the blur smooths the result and the
// march just needs a representative depth.
vec3 get_view_pos_march(ivec2 hp) {
    float d  = texelFetch(uDepthTex, hp * 2, 0).r;
    vec2  uv = (vec2(hp) + 0.5) / uScreenSize;
    return reconstruct_view_pos(d, uv);
}

vec3 get_view_normal(ivec2 hp) {
    // Corner fetch from the full-res normal buffer.
    vec3 n = texelFetch(uNormalTex, hp * 2, 0).rgb * 2.0 - 1.0;
    return normalize(n);
}

uint set_bits_in_range(uint mask, float start_angle, float end_angle) {
    float sector_size = 3.14159265 / float(AO_BITMASK_LEN);
    int start_bit = int(floor(start_angle / sector_size));
    int end_bit   = int(ceil(end_angle / sector_size)) - 1;
    start_bit = clamp(start_bit, 0, AO_BITMASK_LEN - 1);
    end_bit   = clamp(end_bit,   0, AO_BITMASK_LEN - 1);
    if (start_bit > end_bit) return mask;

    uint range_mask;
    if (end_bit - start_bit + 1 >= 32) {
        range_mask = 0xFFFFFFFFu;
    } else {
        range_mask = ((1u << (end_bit - start_bit + 1)) - 1u) << start_bit;
    }
    return mask | range_mask;
}

uint hash(uint x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

// Precomputed slice directions for the golden-angle spiral:
//   k * 2.39996322972865332 rad, k = 0..7
// Replaces 8 sin/cos pairs per pixel with a table lookup. The per-pixel
// rotation applied below still uses runtime sin/cos, but only once per
// pixel rather than once per slice.
const vec2 SLICE_DIRS[AO_NUM_DIRECTIONS] = vec2[AO_NUM_DIRECTIONS](
    vec2( 1.00000,  0.00000),
    vec2(-0.73728,  0.67559),
    vec2( 0.08739, -0.99617),
    vec2( 0.60858,  0.79350),
    vec2(-0.98473, -0.17416),
    vec2( 0.84359, -0.53700),
    vec2(-0.25953,  0.96573),
    vec2(-0.46055, -0.88763)
);

void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);   // half-res coord

    float depth = robust_depth(pix);
    if (depth >= 1.0) {
        FragAO = 1.0;
        return;
    }

    vec3 P = get_view_pos_center(pix);
    vec3 N = get_view_normal(pix);

    vec3 V = normalize(-P);
    if (dot(N, V) <= 0.0) {
        FragAO = 1.0;
        return;
    }

    vec3 T = normalize(cross(vec3(0.0, 1.0, 0.0), V));
    vec3 B = cross(V, T);

    // MARCH_PIXELS is measured in HALF-RES pixels. At half-res, 20 half-res
    // pixels = 40 full-res pixels of world radius. If you want to preserve
    // the exact radius of a full-res version at 20, use 10.0 here.
    const float MARCH_PIXELS = 20.0;
    float step_px = MARCH_PIXELS / float(AO_NUM_STEPS);
    vec2  px_to_uv = 1.0 / uScreenSize;

    const float AO_THICKNESS = 0.05;

    // One hash and one sin/cos pair per pixel; the resulting rotation is
    // applied to the precomputed slice directions.
    uint h = hash(uint(pix.x) * 1973u ^ uint(pix.y) * 9277u);
    float pixel_rotation = float(h & 0xFFFFu) * (6.2831853 / 65536.0);
    float cp = cos(pixel_rotation);
    float sp = sin(pixel_rotation);

    float occlusion = 0.0;
    float total_weight = 0.0;

    for (int dir = 0; dir < AO_NUM_DIRECTIONS; ++dir) {
        vec2 d = SLICE_DIRS[dir];
        vec2 dir2D = vec2(d.x * cp - d.y * sp, d.x * sp + d.y * cp);
        vec3 slice_dir = normalize(T * dir2D.x + B * dir2D.y);

        uint bitmask = 0u;

        for (int side = 0; side < 2; ++side) {
            vec2 march_dir = (side == 0) ? dir2D : -dir2D;

            for (int step = 1; step <= AO_NUM_STEPS; ++step) {
                vec2 sample_uv = (vec2(pix) + 0.5) * px_to_uv
                               + march_dir * px_to_uv * (step_px * float(step));

                ivec2 sample_pix = ivec2(sample_uv * uScreenSize);
                if (sample_pix.x < 0 || sample_pix.x >= int(uScreenSize.x) ||
                    sample_pix.y < 0 || sample_pix.y >= int(uScreenSize.y)) break;
                if (sample_pix == pix) continue;

                float sample_depth = texelFetch(uDepthTex, sample_pix * 2, 0).r;
                if (sample_depth >= 1.0) continue;

                vec3 S = get_view_pos_march(sample_pix);
                vec3 diff = S - P;

                // Folded length + normalize: one dot for dist^2, one sqrt,
                // one dot for elevation, one dot for cos_angle, one divide.
                float dist_sq = dot(diff, diff);
                float dist    = sqrt(dist_sq);
                if (dist < 1e-5) continue;

                float elevation = dot(diff, N);
                if (elevation < AO_THICKNESS) continue;

                float cos_angle = clamp(dot(diff, V) / dist, -1.0, 1.0);
                float angle = acos(cos_angle);

                float angular_radius = atan(AO_THICKNESS / max(dist, AO_THICKNESS));
                angular_radius = min(angular_radius, 0.5);

                float start = max(0.0, angle - angular_radius);
                float end   = min(3.14159265, angle + angular_radius);
                if (start >= end) continue;

                bitmask = set_bits_in_range(bitmask, start, end);
            }
        }

        float slice_occ = float(bitCount(bitmask)) / float(AO_BITMASK_LEN);
        float weight    = max(0.0, dot(N, slice_dir));
        occlusion    += slice_occ * weight;
        total_weight += weight;
    }

    if (total_weight > 0.0) occlusion /= total_weight;
    FragAO = clamp(1.0 - occlusion, 0.0, 1.0);
}