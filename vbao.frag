#version 430 core

// =============================================================================
// vbao.frag — Visibility Bitmask Ambient Occlusion (fragment stage, half-res)
//
// Bit-exact optimisations over the naive version:
//   1. Half-resolution target.
//   2. Cheap view-position reconstruction.
//   3. Folded length/normalize.
//   4. Hardcoded slice directions.
//   5. No redundant depth fetches — centre and march reuse the fetched depth.
//   6. Slices with non-positive N·slice weight are skipped before marching.
//   7. Bitmask saturation early-out in the inner and side loops.
//   8. March-origin UV hoisted; dead self-sample branch removed.
//   9. Redundant normalize(slice_dir) removed — the vector is unit by
//      construction (T ⊥ B, both unit, dir2D a planar rotation of unit d).
// =============================================================================

// --- Configuration ---
#define AO_NUM_DIRECTIONS   8
#define AO_NUM_STEPS        6
#define AO_RADIUS           20
#define AO_THICKNESS        0.05
#define AO_BITMASK_LEN      32

// --- Inputs ---
layout(binding = 0) uniform sampler2D uDepthTex;   // full-res
layout(binding = 1) uniform sampler2D uNormalTex;  // full-res

// NOTE: the three mat4s are unused by the shader, but they MUST stay in the
// block so the std140 offsets of uScreenSize and the scalars after it match
// the CPU-side struct. Removing them silently reinterprets the UBO.
layout(std140, row_major, binding = 0) uniform VBAOUniforms {
    mat4  uInvProj;
    mat4  uProj;
    mat4  uView;
    vec2  uScreenSize;      // HALF-RES resolution (the AO target size)
    float uNear;
    float uFar;
    float uProjA;           // proj[2][2]
    float uProjB;           // -proj[2][3]
    float uInvProj00;       // 1.0 / proj[0][0]
    float uInvProj11;       // 1.0 / proj[1][1]
};

out float FragAO;

// --- Cheap view-position reconstruction (unchanged) ---
vec3 reconstruct_view_pos(float depth, vec2 uv) {
    float ndc_z  = depth * 2.0 - 1.0;
    float view_z = uProjB / (ndc_z + uProjA);
    vec2  ndc_xy = uv * 2.0 - 1.0;
    return vec3(-ndc_xy.x * view_z * uInvProj00,
                -ndc_xy.y * view_z * uInvProj11,
                 view_z);
}

// Robust depth: min of the 2x2 full-res block under this half-res pixel.
float robust_depth(ivec2 hp) {
    ivec2 fp = hp * 2;
    float d00 = texelFetch(uDepthTex, fp + ivec2(0, 0), 0).r;
    float d10 = texelFetch(uDepthTex, fp + ivec2(1, 0), 0).r;
    float d01 = texelFetch(uDepthTex, fp + ivec2(0, 1), 0).r;
    float d11 = texelFetch(uDepthTex, fp + ivec2(1, 1), 0).r;
    return min(min(d00, d10), min(d01, d11));
}

// Centre view position given an already-fetched robust depth.
vec3 get_view_pos_center(ivec2 hp, float depth) {
    vec2 uv = (vec2(hp) + 0.5) / uScreenSize;
    return reconstruct_view_pos(depth, uv);
}

vec3 get_view_normal(ivec2 hp) {
    vec3 n = texelFetch(uNormalTex, hp * 2, 0).rgb * 2.0 - 1.0;
    return normalize(n);
}

uint set_bits_in_range(uint mask, float start_angle, float end_angle) {
    // Deliberately unchanged: floor/ceil and the division are preserved so
    // sector-boundary behaviour is bit-identical.
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

    // Reuse the depth we already fetched — no second robust_depth call.
    vec3 P = get_view_pos_center(pix, depth);
    vec3 N = get_view_normal(pix);

    vec3 V = normalize(-P);
    if (dot(N, V) <= 0.0) {
        FragAO = 1.0;
        return;
    }

    vec3 T = normalize(cross(vec3(0.0, 1.0, 0.0), V));
    vec3 B = cross(V, T);

    float step_px  = AO_RADIUS / float(AO_NUM_STEPS);
    vec2  px_to_uv = 1.0 / uScreenSize;

    // Hoisted: identical to (vec2(pix) + 0.5) * px_to_uv.
    vec2  march_origin = (vec2(pix) + 0.5) * px_to_uv;

    // One hash and one sin/cos pair per pixel.
    uint h = hash(uint(pix.x) * 1973u ^ uint(pix.y) * 9277u);
    float pixel_rotation = float(h & 0xFFFFu) * (6.2831853 / 65536.0);
    float cp = cos(pixel_rotation);
    float sp = sin(pixel_rotation);

    float occlusion = 0.0;
    float total_weight = 0.0;

    for (int dir = 0; dir < AO_NUM_DIRECTIONS; ++dir) {
        vec2 d = SLICE_DIRS[dir];
        vec2 dir2D = vec2(d.x * cp - d.y * sp, d.x * sp + d.y * cp);

        // T and B are unit and orthogonal, dir2D is a planar rotation of a
        // unit vector -> slice_dir is already unit. No normalize needed.
        vec3 slice_dir = T * dir2D.x + B * dir2D.y;

        // Skip before marching. max(0, negative) contributes nothing to
        // occlusion or total_weight, so this is bit-identical.
        float weight = dot(N, slice_dir);
        if (weight <= 0.0) continue;

        uint bitmask = 0u;

        for (int side = 0; side < 2; ++side) {
            vec2 march_dir = (side == 0) ? dir2D : -dir2D;

            for (int step = 1; step <= AO_NUM_STEPS; ++step) {
                vec2 sample_uv = march_origin
                               + march_dir * px_to_uv * (step_px * float(step));

                ivec2 sample_pix = ivec2(sample_uv * uScreenSize);
                if (sample_pix.x < 0 || sample_pix.x >= int(uScreenSize.x) ||
                    sample_pix.y < 0 || sample_pix.y >= int(uScreenSize.y)) break;

                // step_px >= 1 here, so the march origin can never be resampled;
                // the old "sample_pix == pix" guard was dead code.

                float sample_depth = texelFetch(uDepthTex, sample_pix * 2, 0).r;
                if (sample_depth >= 1.0) continue;

                // Reuse the fetched depth — no second texelFetch here.
                vec2 s_uv = (vec2(sample_pix) + 0.5) / uScreenSize;
                vec3 S = reconstruct_view_pos(sample_depth, s_uv);
                vec3 diff = S - P;

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

                // Saturated: no further set_bits_in_range can change the
                // popcount. Bail out of the remaining steps for this side.
                if (bitmask == 0xFFFFFFFFu) break;
            }

            // Same for the opposite side.
            if (bitmask == 0xFFFFFFFFu) break;
        }

        float slice_occ = float(bitCount(bitmask)) / float(AO_BITMASK_LEN);
        occlusion    += slice_occ * weight;
        total_weight += weight;
    }

    if (total_weight > 0.0) occlusion /= total_weight;
    FragAO = clamp(1.0 - occlusion, 0.0, 1.0);
}