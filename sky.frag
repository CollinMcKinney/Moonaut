#version 430 core

// =============================================================================
// sky.frag — procedural sky: atmospheric gradient, sun, moon, clouds
// =============================================================================
//
// Compiled in two modes:
//
//   Default (env cube): renders into one cubemap face as a full-screen
//   triangle. Direction is reconstructed from the cube-face basis and NDC.
//   The face basis must match cube_face_view() in rasterizer_GL.h with
//   right = cross(forward, up).
//
//   SKYBOX_MODE: renders into the main HDR target at z=1 (far plane).
//   Direction is reconstructed from the screen UV through the inverse
//   view-projection and the camera position. Fills only pixels where the
//   depth buffer is still at the far value.
//
// All look-and-feel parameters live in the SKY_* block below as #define
// macros. Edit a value, recompile, restart. When the artists need runtime
// control, promote each define to a uniform — the C++ side already has
// slots prepared for them (shader_variant_t's u_sky_* fields and the
// corresponding set_uniforms_for_variant uploads), they are simply
// unused while the shader declares no matching uniform.
//
// Frame convention for the astronomy: +X East, +Y Up, +Z North,
// right-handed.

// -----------------------------------------------------------------------------
// Sky configuration — edit these and recompile
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Sky configuration — edit these and recompile
// -----------------------------------------------------------------------------
#define SKY_LATITUDE             40.0     // degrees, +N / -S
#define SKY_DAY_OF_YEAR          172      // 1..365 (172 ≈ June 21)
#define SKY_TIME_OF_DAY          8.0     // hours, 12.0 = solar noon
#define SKY_LUNAR_PHASE          0.5      // 0 = new, 0.5 = full, 1 = new
#define SKY_RAIN                 0.0      // 0..1

#define SKY_SUN_TINT             vec3(1.00, 0.92, 0.78)
#define SKY_MOON_TINT            vec3(0.85, 0.85, 0.90)
#define SKY_INTENSITY            0.5     // overall brightness multiplier
#define SKY_SUN_ANGULAR_RADIUS   0.00465  // radians, ~0.266 deg (real sun)

// Gradient colors and shaping
#define SKY_ZENITH               vec3(0.08, 0.24, 0.84)
#define SKY_HORIZON              vec3(0.9, 0.7, 0.6)
#define SKY_GROUND               vec3(0.05, 0.5, 0.03)
#define SKY_EXPONENT             0.4
#define SKY_CLOUD_COLOR          vec3(1.15, 1.12, 1.05)
#define SKY_CLOUD_COVERAGE       0.4

// -----------------------------------------------------------------------------
// Uniforms
// -----------------------------------------------------------------------------
uniform float uTime;

#ifdef SKYBOX_MODE
uniform mat4  uInvViewProj;
uniform vec3  uCamEye;
uniform vec2  uScreenSize;
#else
uniform int   uFaceIndex;
uniform float uProbeSize;
#endif

out vec4 FragColor;

// -----------------------------------------------------------------------------
// Astronomy
// -----------------------------------------------------------------------------
#define SKY_PI 3.141592653589793

// Solar declination (Cooper, 1969). Peaks at +23.44° around the June
// solstice, -23.44° around December. This single term produces seasons;
// the sun's orbital distance is effectively constant and has no visible
// effect on the sky.
float sky_solar_declination(int dayOfYear) {
    return 23.44 * (SKY_PI / 180.0)
         * sin((2.0 * SKY_PI / 365.0) * (284.0 + float(dayOfYear)));
}

// Sun direction from geographic coordinates and local solar time.
// Frame: +X East, +Y Up, +Z North.
vec3 sky_sun_direction(float latitude, int dayOfYear, float solarTime) {
    float lat  = latitude * (SKY_PI / 180.0);
    float decl = sky_solar_declination(dayOfYear);
    float H    = (solarTime - 12.0) * 15.0 * (SKY_PI / 180.0);

    float sinAlt = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(H);
    sinAlt = clamp(sinAlt, -1.0, 1.0);
    float altitude = asin(sinAlt);
    float azimuth  = atan(-sin(H) * cos(decl),
                           cos(lat) * sin(decl) - sin(lat) * cos(decl) * cos(H));

    float ca = cos(altitude);
    return vec3(sin(azimuth) * ca, sin(altitude), cos(azimuth) * ca);
}

// Moon direction: crude. Offsets the sun's hour angle by 12h plus a
// phase-driven drift. Ignores inclination and eccentricity; good enough
// to place a moon disk and drive the phase terminator.
vec3 sky_moon_direction(float latitude, int dayOfYear, float solarTime,
                        float lunarPhase) {
    float moonTime = solarTime + 12.0 + lunarPhase * 24.0;
    moonTime = moonTime - 24.0 * floor(moonTime / 24.0);
    return sky_sun_direction(latitude, dayOfYear, moonTime);
}

// -----------------------------------------------------------------------------
// Sky color as a function of sun altitude
// -----------------------------------------------------------------------------
// SKY_ZENITH / SKY_HORIZON / SKY_GROUND are the day-reference colors.
// This function blends them toward night and toward a warm sunset horizon
// based on the sun's height. Hand-tuned; the natural upgrade is to
// replace these blends with Preetham or Hosek-Wilkie.
vec3 sky_zenith_color(float sunY) {
    vec3 night    = vec3(0.008, 0.014, 0.030);
    vec3 twilight = vec3(0.075, 0.085, 0.180);
    float tw = smoothstep(-0.18, -0.02, sunY);
    float dy = smoothstep(-0.02,  0.15, sunY);
    return mix(mix(night, twilight, tw), SKY_ZENITH, dy);
}

vec3 sky_horizon_color(float sunY) {
    vec3 night    = vec3(0.010, 0.016, 0.035);
    vec3 sunset   = vec3(0.95, 0.42, 0.18);
    vec3 twilight = vec3(0.30, 0.24, 0.36);
    float tw = smoothstep(-0.18, -0.02, sunY);
    float dy = smoothstep(-0.02,  0.20, sunY);
    float setF = 1.0 - smoothstep(0.0, 0.18, abs(sunY));
    vec3 dayCol = mix(SKY_HORIZON, sunset, setF * 0.65);
    return mix(mix(night, twilight, tw), dayCol, dy);
}

vec3 sky_ground_color(float sunY) {
    float day = smoothstep(-0.10, 0.10, sunY);
    return mix(vec3(0.010, 0.010, 0.014), SKY_GROUND, day);
}

// -----------------------------------------------------------------------------
// Clouds
// -----------------------------------------------------------------------------
uint sky_hash(uint x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

float sky_hash_f(vec2 p) {
    uint h = sky_hash(floatBitsToUint(p.x));
    h = sky_hash(h ^ floatBitsToUint(p.y));
    return float(h) / 4294967296.0;
}

float sky_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = sky_hash_f(i);
    float b = sky_hash_f(i + vec2(1.0, 0.0));
    float c = sky_hash_f(i + vec2(0.0, 1.0));
    float d = sky_hash_f(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Plane-projected cloud layer. SKY_RAIN tightens the noise threshold
// (more coverage) and thickens the layer near the horizon so rain reads
// as heavier overcast.
float sky_cloud_mask(vec3 dir, float coverage, float rain, float time) {
    if (dir.y < 0.01) return 0.0;
    float t = mix(0.6, 1.2, rain) / (dir.y + 0.05);
    vec2 uv = dir.xz * t;
    uv += vec2(time * 0.008, time * 0.003);
    float n = sky_noise(uv * 0.7);
    n += sky_noise(uv * 1.9) * 0.5;
    n += sky_noise(uv * 4.3) * 0.25;
    n /= 1.75;
    float c = smoothstep(coverage, coverage + 0.35, n);
    float fade = smoothstep(0.01, mix(0.20, 0.08, rain), dir.y);
    return c * fade;
}

vec3 sky_cloud_color(float sunY) {
    vec3 base  = SKY_CLOUD_COLOR;
    vec3 storm = vec3(0.20, 0.22, 0.25);
    vec3 lit   = mix(base, storm, SKY_RAIN);
    float day  = smoothstep(-0.10, 0.15, sunY);
    vec3 night = vec3(0.03, 0.03, 0.05);
    vec3 col   = mix(night, lit, day);
    float setF = 1.0 - smoothstep(0.0, 0.15, abs(sunY));
    col = mix(col, col * vec3(1.3, 0.7, 0.5), setF * (1.0 - SKY_RAIN) * 0.4);
    return col;
}

// -----------------------------------------------------------------------------
// Sun and moon disks
// -----------------------------------------------------------------------------
vec3 sky_sun_disk(vec3 dir, vec3 sunDir, float sunY) {
    float cosA = dot(dir, sunDir);
    float cosLimb = cos(SKY_SUN_ANGULAR_RADIUS);
    float edge = smoothstep(cosLimb - 0.0004, cosLimb + 0.0004, cosA);
    float low = smoothstep(0.0, 0.25, sunY);
    vec3 col = mix(vec3(1.0, 0.45, 0.20), vec3(1.0, 0.95, 0.85), low);
    col = mix(col, SKY_SUN_TINT, 0.5);
    return col * edge;
}

vec3 sky_sun_glow(vec3 dir, vec3 sunDir, float sunY) {
    float d = max(dot(dir, sunDir), 0.0);
    float glow = pow(d, 350.0) * 0.8 + pow(d, 25.0) * 0.05;
    float low = smoothstep(0.0, 0.25, sunY);
    vec3 tint = mix(vec3(1.0, 0.55, 0.30), vec3(1.0, 0.90, 0.75), low);
    return tint * glow;
}

// Moon disk with phase. Treats the moon as a sphere lit by the sun
// direction, so the terminator falls out of the geometry for free.
vec3 sky_moon_disk(vec3 dir, vec3 moonDir, vec3 sunDir, float moonY) {
    float cosA = dot(dir, moonDir);
    float angularRadius = 0.0045;
    float cosLimb = cos(angularRadius);
    if (cosA < cosLimb) return vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, moonDir));
    vec3 mup = cross(moonDir, right);

    float sinA = sqrt(max(0.0, 1.0 - cosA * cosA));
    vec3 offsetDir = normalize(dir - moonDir * cosA);
    vec2 diskUV = vec2(dot(offsetDir, right), dot(offsetDir, mup))
                * sinA / sin(angularRadius);

    float r2 = dot(diskUV, diskUV);
    if (r2 > 1.0) return vec3(0.0);
    float z = sqrt(max(0.0, 1.0 - r2));
    vec3 sphereN = right * diskUV.x + mup * diskUV.y + moonDir * z;

    float lit = max(dot(sphereN, sunDir), 0.0);
    float vis = smoothstep(-0.02, 0.05, moonY);
    return SKY_MOON_TINT * lit * vis;
}

// -----------------------------------------------------------------------------
// Sky direction reconstruction
// -----------------------------------------------------------------------------
vec3 sky_direction_from_screen() {
#ifdef SKYBOX_MODE
    vec2 ndc = gl_FragCoord.xy / uScreenSize * 2.0 - 1.0;
    vec4 far = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 world = far.xyz / far.w;
    return normalize(world - uCamEye);
#else
    vec3 F, U, R;
    switch (uFaceIndex) {
        case 0: F = vec3( 1, 0, 0); U = vec3(0,-1, 0); R = vec3( 0, 0,-1); break;
        case 1: F = vec3(-1, 0, 0); U = vec3(0,-1, 0); R = vec3( 0, 0, 1); break;
        case 2: F = vec3( 0, 1, 0); U = vec3(0, 0, 1); R = vec3( 1, 0, 0); break;
        case 3: F = vec3( 0,-1, 0); U = vec3(0, 0,-1); R = vec3( 1, 0, 0); break;
        case 4: F = vec3( 0, 0, 1); U = vec3(0,-1, 0); R = vec3( 1, 0, 0); break;
        case 5: F = vec3( 0, 0,-1); U = vec3(0,-1, 0); R = vec3(-1, 0, 0); break;
        default: F = vec3(0, 0, 1); U = vec3(0,-1, 0); R = vec3(1, 0, 0); break;
    }
    vec2 ndc = gl_FragCoord.xy / uProbeSize * 2.0 - 1.0;
    return normalize(F + ndc.x * R + ndc.y * U);
#endif
}

// -----------------------------------------------------------------------------
// Sky evaluation
// -----------------------------------------------------------------------------
vec3 sky_evaluate(vec3 dir, vec3 sunDir) {
    float sunY = sunDir.y;

    float up = dir.y;
    float skyT    = pow(clamp(up, 0.0, 1.0), SKY_EXPONENT);
    float groundT = clamp(-up, 0.0, 1.0);

    vec3 zenith  = sky_zenith_color(sunY);
    vec3 horizon = sky_horizon_color(sunY);
    vec3 ground  = sky_ground_color(sunY);

    vec3 col = mix(horizon, zenith, skyT);
    col = mix(col, ground, groundT);

    float cloudCover = clamp(SKY_CLOUD_COVERAGE + SKY_RAIN * 0.6, 0.0, 1.0);
    float c = sky_cloud_mask(dir, cloudCover, SKY_RAIN, uTime);
    col = mix(col, sky_cloud_color(sunY), c);

    vec3 sunDisk = sky_sun_disk(dir, sunDir, sunY);
    vec3 sunGlow = sky_sun_glow(dir, sunDir, sunY);
    col += (sunDisk + sunGlow) * smoothstep(-0.05, 0.05, sunY);

    vec3 moonDir = sky_moon_direction(SKY_LATITUDE, SKY_DAY_OF_YEAR,
                                      SKY_TIME_OF_DAY, SKY_LUNAR_PHASE);
    vec3 moonDisk = sky_moon_disk(dir, moonDir, sunDir, moonDir.y);
    col += moonDisk;

    float haze = SKY_RAIN * (1.0 - smoothstep(0.0, 0.20, abs(dir.y)));
    col = mix(col, vec3(0.35, 0.36, 0.38), haze * 0.5);

    return col * SKY_INTENSITY;
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
void main() {
    vec3 dir = sky_direction_from_screen();
    vec3 sunDir = sky_sun_direction(SKY_LATITUDE, SKY_DAY_OF_YEAR,
                                    SKY_TIME_OF_DAY);
    vec3 col = sky_evaluate(dir, sunDir);
    FragColor = vec4(col, 1.0);
}