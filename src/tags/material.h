#ifndef MATERIAL_DEFINITION_H
#define MATERIAL_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Unified render method key
 * ========================================================================= */
typedef enum render_method {
    MODE_WIREFRAME = 0,
    MODE_FLAT      = 1,
    MODE_GOURAUD   = 2,
    MODE_QUADRATIC = 3,
    MODE_CUBIC     = 4,
    MODE_PHONG     = 5,
    MODE_RESERVED_6 = 6,
    MODE_RESERVED_7 = 7,

    EFFECT_BUMP            = (1 << 3),
    EFFECT_DIFFUSE_WRAP    = (1 << 4),
    EFFECT_CEL_SHADING     = (1 << 5),
    EFFECT_MINNAERT        = (1 << 6),
    EFFECT_OREN_NAYAR      = (1 << 7),
    EFFECT_AMBIENT_LIGHT   = (1 << 8),
    EFFECT_GOOCH           = (1 << 9),
    EFFECT_BACK_GLOW       = (1 << 10),
    EFFECT_RIM             = (1 << 11),
    EFFECT_FRESNEL         = (1 << 12),
    EFFECT_EMISSIVE        = (1 << 13),
    EFFECT_EMISSIVE_PULSE  = (1 << 14),
    EFFECT_STROBE          = (1 << 15),
    EFFECT_SPECULAR        = (1 << 16),
    EFFECT_SPECULAR_THRESH = (1 << 17),
    EFFECT_SATURATION      = (1 << 18),
    EFFECT_IRIDESCENCE     = (1 << 19),
    EFFECT_GLITCH          = (1 << 20),
    EFFECT_ROUGHNESS       = (1 << 21),
    EFFECT_FRINGE          = (1 << 22),
    EFFECT_POSTERIZE       = (1 << 23),
    EFFECT_FOG             = (1 << 24),
    EFFECT_ALPHA           = (1 << 25),
    EFFECT_CLEARCOAT       = (1 << 26),
    EFFECT_SHEEN           = (1 << 27),
    EFFECT_ANISOTROPIC     = (1 << 28),
    EFFECT_SUBSURFACE      = (1 << 29),
    EFFECT_TRANSMISSION    = (1 << 30),
    EFFECT_RESERVED_31     = (1 << 31)
} render_method;

/* ---- Bitfield metadata ---- */
TAG_BITFIELD32_BEGIN(render_method, 3)
    TAG_BITFIELD32_ENTRY(MODE_WIREFRAME, "Wireframe")
    TAG_BITFIELD32_ENTRY(MODE_FLAT,      "Flat")
    TAG_BITFIELD32_ENTRY(MODE_GOURAUD,   "Gouraud")
    TAG_BITFIELD32_ENTRY(MODE_QUADRATIC, "Quadratic")
    TAG_BITFIELD32_ENTRY(MODE_CUBIC,     "Cubic")
    TAG_BITFIELD32_ENTRY(MODE_PHONG,     "Phong")
    TAG_BITFIELD32_ENTRY(EFFECT_BUMP,            "Bump")
    TAG_BITFIELD32_ENTRY(EFFECT_DIFFUSE_WRAP,    "Diffuse Wrap")
    TAG_BITFIELD32_ENTRY(EFFECT_CEL_SHADING,     "Cel Shading")
    TAG_BITFIELD32_ENTRY(EFFECT_MINNAERT,        "Minnaert")
    TAG_BITFIELD32_ENTRY(EFFECT_OREN_NAYAR,      "Oren‑Nayar")
    TAG_BITFIELD32_ENTRY(EFFECT_AMBIENT_LIGHT,   "Ambient Light")
    TAG_BITFIELD32_ENTRY(EFFECT_GOOCH,           "Gooch")
    TAG_BITFIELD32_ENTRY(EFFECT_BACK_GLOW,       "Back Glow")
    TAG_BITFIELD32_ENTRY(EFFECT_RIM,             "Rim")
    TAG_BITFIELD32_ENTRY(EFFECT_FRESNEL,         "Fresnel")
    TAG_BITFIELD32_ENTRY(EFFECT_EMISSIVE,        "Emissive")
    TAG_BITFIELD32_ENTRY(EFFECT_EMISSIVE_PULSE,  "Emissive Pulse")
    TAG_BITFIELD32_ENTRY(EFFECT_STROBE,          "Strobe")
    TAG_BITFIELD32_ENTRY(EFFECT_SPECULAR,        "Specular")
    TAG_BITFIELD32_ENTRY(EFFECT_SPECULAR_THRESH, "Specular Threshold")
    TAG_BITFIELD32_ENTRY(EFFECT_SATURATION,      "Saturation")
    TAG_BITFIELD32_ENTRY(EFFECT_IRIDESCENCE,     "Iridescence")
    TAG_BITFIELD32_ENTRY(EFFECT_GLITCH,          "Glitch")
    TAG_BITFIELD32_ENTRY(EFFECT_ROUGHNESS,       "Roughness")
    TAG_BITFIELD32_ENTRY(EFFECT_FRINGE,          "Fringe")
    TAG_BITFIELD32_ENTRY(EFFECT_POSTERIZE,       "Posterize")
    TAG_BITFIELD32_ENTRY(EFFECT_FOG,             "Fog")
    TAG_BITFIELD32_ENTRY(EFFECT_ALPHA,           "Alpha")
    TAG_BITFIELD32_ENTRY(EFFECT_CLEARCOAT,       "Clearcoat")
    TAG_BITFIELD32_ENTRY(EFFECT_SHEEN,           "Sheen")
    TAG_BITFIELD32_ENTRY(EFFECT_ANISOTROPIC,     "Anisotropic")
    TAG_BITFIELD32_ENTRY(EFFECT_SUBSURFACE,      "Subsurface")
TAG_BITFIELD32_END(render_method)

static INLINE u32 render_method_get_mode(render_method key) {
    return (u32)key & 0x7;
}
static INLINE u32 render_method_get_effects(render_method key) {
    return (u32)key & ~0x7;
}

/* =========================================================================
 * Material definition
 * ========================================================================= */
typedef struct material_definition {
    u32          render_method;

    vec3         color;
    real         ambient_light_factor;
    real         alpha;
    real         saturation;
    vec3         tint;

    i32          cel_bands;
    i32          diffuse_wrap;
    real         oren_nayar_sigma;
    real         minnaert_k;

    real         bump_amplitude;
    real         bump_frequency;
    real         bump_speed;

    vec3         gooch_cool;
    vec3         gooch_warm;
    vec3         back_glow_color;

    vec3         rim_color;
    real         rim_exponent;
    vec3         fresnel_color;     /* dummy, kept for compatibility – ignored */
    real         fresnel_exponent;  /* Schlick exponent, still used */

    real         transmission_strength;
    vec3         specular_tint;
    real         surface_roughness;

    vec3         emissive_color;
    real         emissive_pulse_frequency;
    real         emissive_pulse_phase;
    real         emissive_pulse_amplitude;
    vec3         strobe_color;
    real         strobe_frequency;
    real         strobe_phase;

    bool         skip_fog;

    real         iridescence_strength;
    real         glitch_intensity;
    real         fringe_intensity;
    i32          posterize_levels;
    bool         double_sided;
    real         roughness;

    /* ---- PBR fields ---- */
    real         metallic;              /* 0.0 = dielectric, 1.0 = metal */
    real         ior;                   /* index of refraction (>0) */
    real         subsurface_strength;   /* 0 = none, 1 = strong, 2 = max */

    vec3         clearcoat_color;
    real         clearcoat_roughness;   /* renamed from clearcoat_exponent */
    real         clearcoat_strength;
    vec3         sheen_color;
    real         sheen_exponent;
    real         sheen_strength;

    /* ---- Anisotropy ---- */
    real         anisotropic;           /* 0.0 = isotropic, 1.0 = full anisotropy */
    vec3         transmission_tint;     /* colour of transmitted light (e.g., (1,1,1) for glass) */

} material_definition;

/* =========================================================================
 * Tag group definition
 * ========================================================================= */
TAG_GROUP_BEGIN(material, TAG_MAGIC_PACK(mtrl), sizeof(struct material_definition))
    FIELD_BITFIELD32("render_method", render_method_bitfield, render_method_ENUM_BITS),
    FIELD_VEC3("color"),
    FIELD_REAL("ambient_light_factor"),
    FIELD_REAL("alpha"),
    FIELD_REAL("saturation"),
    FIELD_VEC3("tint"),
    FIELD_I32("cel_bands"),
    FIELD_I32("diffuse_wrap"),
    FIELD_REAL("oren_nayar_sigma"),
    FIELD_REAL("minnaert_k"),
    FIELD_REAL("bump_amplitude"),
    FIELD_REAL("bump_frequency"),
    FIELD_REAL("bump_speed"),
    FIELD_VEC3("gooch_cool"),
    FIELD_VEC3("gooch_warm"),
    FIELD_VEC3("back_glow_color"),
    FIELD_VEC3("rim_color"),
    FIELD_REAL("rim_exponent"),
    FIELD_VEC3("fresnel_color"),   /* dummy, kept for compatibility */
    FIELD_REAL("fresnel_exponent"),
    FIELD_REAL("transmission_strength"),
    FIELD_VEC3("specular_tint"),
    FIELD_REAL("surface_roughness"),
    FIELD_VEC3("emissive_color"),
    FIELD_REAL("emissive_pulse_frequency"),
    FIELD_REAL("emissive_pulse_phase"),
    FIELD_REAL("emissive_pulse_amplitude"),
    FIELD_VEC3("strobe_color"),
    FIELD_REAL("strobe_frequency"),
    FIELD_REAL("strobe_phase"),
    FIELD_BOOL("skip_fog"),
    FIELD_REAL("iridescence_strength"),
    FIELD_REAL("glitch_intensity"),
    FIELD_REAL("fringe_intensity"),
    FIELD_I32("posterize_levels"),
    FIELD_BOOL("double_sided"),
    FIELD_REAL("roughness"),
    FIELD_REAL("metallic"),
    FIELD_REAL("ior"),
    FIELD_REAL("subsurface_strength"),
    FIELD_VEC3("clearcoat_color"),
    FIELD_REAL("clearcoat_roughness"),   /* renamed */
    FIELD_REAL("clearcoat_strength"),
    FIELD_VEC3("sheen_color"),
    FIELD_REAL("sheen_exponent"),
    FIELD_REAL("sheen_strength"),
    FIELD_REAL("anisotropic"),
    FIELD_VEC3("transmission_tint"),
    FIELD_TERMINATOR
TAG_GROUP_END(material, sizeof(struct material_definition))

/* =========================================================================
 * Default materials – refined with anisotropy where applicable
 * ========================================================================= */

/* ------------------------------------------------------------------------
 * 1. WIREFRAME
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WIREFRAME = {
    .render_method          = MODE_WIREFRAME | EFFECT_AMBIENT_LIGHT,
    .color                  = {1.0f, 1.0f, 1.0f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint          = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 2. FLAT
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_FLAT = {
    .render_method          = MODE_FLAT | EFFECT_AMBIENT_LIGHT,
    .color                  = {0.5f, 0.5f, 0.5f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 3. GOURAUD
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GOURAUD = {
    .render_method          = MODE_GOURAUD | EFFECT_AMBIENT_LIGHT,
    .color                  = {0.5f, 0.5f, 0.5f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 4. QUADRATIC
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_QUADRATIC = {
    .render_method          = MODE_QUADRATIC | EFFECT_AMBIENT_LIGHT,
    .color                  = {0.5f, 0.5f, 0.5f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 5. CUBIC
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CUBIC = {
    .render_method          = MODE_CUBIC | EFFECT_AMBIENT_LIGHT,
    .color                  = {0.5f, 0.5f, 0.5f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 6. PHONG (debug)
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_PHONG = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT,
    .color                  = {0.5f, 0.5f, 0.5f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.5f, 0.5f, 0.5f},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 7. WATER
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WATER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_BACK_GLOW | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_TRANSMISSION,
    .color                  = {0.1, 0.5, 0.7},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.75f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.30f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.4f,
    .bump_frequency         = 32.0f,
    .bump_speed             = 8.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.20f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.8f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.02f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.05f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.01f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.33f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.9f, 0.95f, 1.0f}
};

/* ------------------------------------------------------------------------
 * 8. GRASS
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GRASS = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_BACK_GLOW | EFFECT_RIM | EFFECT_FRESNEL |
                              EFFECT_SPECULAR | EFFECT_IRIDESCENCE | EFFECT_GLITCH |
                              EFFECT_FRINGE | EFFECT_SHEEN | 
                              EFFECT_SUBSURFACE | EFFECT_ANISOTROPIC,
    .color                  = {0.2, 0.5, 0.1},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.2f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.70f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.15f,
    .bump_frequency         = 16.0f,
    .bump_speed             = 1.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.1f, 0.3f, 0.05f},
    .rim_color              = {0.4f, 0.8f, 0.15f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.4, 0.6, 0.2},
    .surface_roughness     = 0.6f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.05f,
    .glitch_intensity       = 0.02f,
    .fringe_intensity       = 0.01f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.6f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .subsurface_strength    = 1.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.5, 0.8, 0.2},
    .sheen_exponent         = 3.0f,
    .sheen_strength         = 1.0f,
    .anisotropic            = -0.1f
};

/* ------------------------------------------------------------------------
 * 9. CLOTH
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CLOTH = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_SPECULAR | EFFECT_SHEEN | EFFECT_SUBSURFACE | 
                              EFFECT_ANISOTROPIC,
    .color                  = {0.7, 0.25, 0.35},
    .ambient_light_factor   = 0.8f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.50f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.03f,
    .bump_frequency         = 60.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.7f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.7f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .subsurface_strength    = 0.2f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0, 0.9, 0.9},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 1.0f,
    .anisotropic            = -0.3f
};

/* ------------------------------------------------------------------------
 * 10. WOOD
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WOOD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_DIFFUSE_WRAP |
                              EFFECT_OREN_NAYAR | EFFECT_RIM | EFFECT_SPECULAR |
                              EFFECT_ROUGHNESS | EFFECT_CLEARCOAT | EFFECT_ANISOTROPIC |
                              EFFECT_BUMP,
    .color                  = {0.55, 0.3, 0.12},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.40f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.1f,
    .bump_frequency         = 64.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.15f, 0.08f, 0.03f},
    .rim_exponent           = 2.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.6, 0.4, 0.25},
    .surface_roughness     = 0.3f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.3f,
    .metallic               = 0.0f,
    .ior                    = 1.55f,
    .clearcoat_color        = {0.9f, 0.8f, 0.5f},
    .clearcoat_roughness    = 0.15f,
    .clearcoat_strength     = 0.7f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 11. METAL
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_METAL = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_IRIDESCENCE | EFFECT_ANISOTROPIC | EFFECT_ROUGHNESS,
    .color                  = {0.9, 0.9, 0.9},
    .ambient_light_factor   = 0.5f,
    .alpha                  = 1.0f,
    .saturation             = 1.2f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.10f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 512.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {1.0f, 1.0f, 1.0f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.80f, 0.80f, 0.85f},
    .surface_roughness     = 0.05f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.1f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.05f,
    .metallic               = 1.0f,
    .ior                    = 0.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.8f
};

/* ------------------------------------------------------------------------
 * 12. GLASS
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GLASS = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE | EFFECT_IRIDESCENCE | EFFECT_FRINGE | 
                              EFFECT_TRANSMISSION,
    .color                  = {0.85f, 0.95f, 1.0f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.2f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.01f,
    .bump_frequency         = 80.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.05f, 0.10f, 0.20f},
    .rim_color              = {0.30f, 0.50f, 0.80f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.8f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.02f,
    .emissive_color         = {0.00f, 0.02f, 0.05f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.12f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.02f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.9,0.95,1.0}
};

/* ------------------------------------------------------------------------
 * 13. SKIN
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_SKIN = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_SHEEN | EFFECT_SUBSURFACE,
    .color                  = {0.9, 0.75, 0.65},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.40f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.02f,
    .bump_frequency         = 128.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.3, 0.2, 0.25},
    .gooch_warm             = {1.0, 0.8, 0.7},
    .back_glow_color        = {0.2, 0.1, 0.1},
    .rim_color              = {0.5, 0.3, 0.2},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.5, 0.35, 0.3},
    .surface_roughness     = 0.35f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.50f,
    .metallic               = 0.0f,
    .ior                    = 1.4f,
    .subsurface_strength    = 0.9f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0, 0.8, 0.7},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 0.5f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 14. RUBBER
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_RUBBER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_SPECULAR,
    .color                  = {0.15, 0.15, 0.15},
    .ambient_light_factor   = 0.6f,
    .alpha                  = 1.0f,
    .saturation             = 0.8f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.90f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.10f,
    .bump_frequency         = 32.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.08f, 0.08f, 0.08f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.1,0.1,0.1},
    .surface_roughness     = 0.6f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.50f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 15. ICE
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_ICE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_EMISSIVE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_SUBSURFACE | 
                              EFFECT_TRANSMISSION,
    .color                  = {0.8, 0.9, 1.0},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.5f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.08f,
    .bump_frequency         = 128.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.10f, 0.25f, 0.40f},
    .rim_color              = {0.70f, 0.85f, 1.0f},
    .rim_exponent           = 4.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.7f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.01f,
    .emissive_color         = {0.02f, 0.05f, 0.12f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.10f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.03f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.01f,
    .metallic               = 0.0f,
    .ior                    = 1.31f,
    .subsurface_strength    = 0.8f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.8, 0.9, 1.0}
};

/* ------------------------------------------------------------------------
 * 16. STONE
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_STONE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_SPECULAR | EFFECT_ROUGHNESS,
    .color                  = {0.55, 0.5, 0.45},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 0.9f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 1.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.2f,
    .bump_frequency         = 24.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.12f, 0.11f, 0.09f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.1,0.1,0.1},
    .surface_roughness     = 0.7f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.7f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 17. LAVA
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_LAVA = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_EMISSIVE |
                              EFFECT_EMISSIVE_PULSE | EFFECT_STROBE | EFFECT_GLITCH |
                              EFFECT_FRINGE,
    .color                  = {0.15f, 0.05f, 0.00f},
    .ambient_light_factor   = 0.15f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 0.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.60f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.40f,
    .bump_frequency         = 24.0f,
    .bump_speed             = 4.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {1.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.8, 0.4, 0.0},
    .rim_color              = {0.80f, 0.15f, 0.0f},
    .rim_exponent           = 1.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.4f,
    .emissive_color         = {0.50f, 0.25f, 0.0f},
    .emissive_pulse_frequency = 1.0471975512f,
    .emissive_pulse_phase   = 1.57f,
    .emissive_pulse_amplitude = 0.3f,
    .strobe_color           = {0.10f, 0.04f, 0.0f},
    .strobe_frequency       = 0.47140452079f,
    .strobe_phase           = 2.10f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.05f,
    .fringe_intensity       = 0.3f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.4f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 18. TOON
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_TOON = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_GOOCH |
                              EFFECT_RIM | EFFECT_SPECULAR | EFFECT_SPECULAR_THRESH |
                              EFFECT_CEL_SHADING,
    .color                  = {0.90f, 0.70f, 0.40f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 2.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 4,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.10f, 0.15f, 0.40f},
    .gooch_warm             = {1.00f, 0.90f, 0.60f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 8.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {1.0f, 1.0f, 0.8f},
    .surface_roughness     = 0.2f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 19. HOLOGRAM
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_HOLOGRAM = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BACK_GLOW | EFFECT_RIM | EFFECT_FRESNEL |
                              EFFECT_EMISSIVE | EFFECT_EMISSIVE_PULSE | EFFECT_STROBE |
                              EFFECT_IRIDESCENCE | EFFECT_GLITCH | EFFECT_FRINGE | 
                              EFFECT_ANISOTROPIC,
    .color                  = {0.20f, 0.60f, 0.80f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.55f,
    .saturation             = 1.1f,
    .tint                   = {1.0f, 1.5f, 2.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.15f, 0.45f, 0.65f},
    .rim_color              = {0.5,0.9,1.0},
    .rim_exponent           = 64.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.60f, 0.85f, 1.0f},
    .surface_roughness     = 0.1f,
    .emissive_color         = {0.1,0.3,0.5},
    .emissive_pulse_frequency = 0.8f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.15f,
    .strobe_color           = {0.10f, 0.35f, 0.55f},
    .strobe_frequency       = 1.0f,
    .strobe_phase           = 0.5f,
    .skip_fog               = true,
    .iridescence_strength   = 0.30f,
    .glitch_intensity       = 0.7f,
    .fringe_intensity       = 0.25f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 20. IRIDESCENT
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_IRIDESCENT = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_EMISSIVE |
                              EFFECT_EMISSIVE_PULSE | EFFECT_STROBE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_SATURATION | 
                              EFFECT_ANISOTROPIC,
    .color                  = {1.0f, 1.0f, 1.0f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.9f,
    .saturation             = 1.5f,
    .tint                   = {1.2,1.0,0.8},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.10f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.50f, 0.00f, 0.50f},
    .gooch_warm             = {1.00f, 0.80f, 0.20f},
    .back_glow_color        = {0.80f, 0.60f, 1.0f},
    .rim_color              = {1.0, 0.5, 0.0},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {1.0f, 1.0f, 0.8f},
    .surface_roughness     = 0.05f,
    .emissive_color         = {0.30f, 0.20f, 0.40f},
    .emissive_pulse_frequency = 0.5f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.20f,
    .strobe_color           = {0.50f, 0.00f, 0.50f},
    .strobe_frequency       = 1.0f,
    .strobe_phase           = 1.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.90f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.20f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 21. PLASTIC
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_PLASTIC = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_RIM |
                              EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_FRINGE |
                              EFFECT_SATURATION | EFFECT_CLEARCOAT,
    .color                  = {0.20f, 0.50f, 0.80f},
    .ambient_light_factor   = 0.70f,
    .alpha                  = 1.0f,
    .saturation             = 1.2f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.01f,
    .bump_frequency         = 128.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.40f, 0.70f, 0.90f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.8,0.9,1.0},
    .surface_roughness     = 0.15f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.02f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.15f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 1.0f},
    .clearcoat_roughness    = 0.1f,
    .clearcoat_strength     = 0.5f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 22. BRICK
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_BRICK = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_ROUGHNESS,
    .color                  = {0.7, 0.35, 0.3},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.5f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.80f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.75f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.75f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 23. LEATHER
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_LEATHER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_ROUGHNESS | EFFECT_CLEARCOAT |
                              EFFECT_SHEEN,
    .color                  = {0.45, 0.25, 0.12},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.50f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.15f,
    .bump_frequency         = 8.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.18f, 0.09f, 0.05f},
    .gooch_warm             = {0.42f, 0.21f, 0.13f},
    .back_glow_color        = {0.08f, 0.04f, 0.02f},
    .rim_color              = {0.36f, 0.18f, 0.11f},
    .rim_exponent           = 1.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.2,0.1,0.05},
    .surface_roughness     = 0.5f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.15f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.1f, 0.05f, 0.02f},
    .clearcoat_roughness    = 0.5f,
    .clearcoat_strength     = 0.2f,
    .sheen_color            = {0.8, 0.6, 0.4},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.3f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 24. GOLD
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GOLD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | 
                              EFFECT_OREN_NAYAR | EFFECT_GOOCH | EFFECT_BACK_GLOW |
                              EFFECT_RIM | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE | EFFECT_IRIDESCENCE | EFFECT_ANISOTROPIC |
                              EFFECT_ROUGHNESS,
    .color                  = {0.80f, 0.60f, 0.20f},
    .ambient_light_factor   = 0.50f,
    .alpha                  = 1.0f,
    .saturation             = 1.5f,
    .tint                   = {1.5f, 1.2f, 0.8f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.10f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.05f,
    .bump_frequency         = 512.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.40f, 0.30f, 0.10f},
    .gooch_warm             = {1.00f, 0.80f, 0.30f},
    .back_glow_color        = {0.50f, 0.40f, 0.10f},
    .rim_color              = {1.00f, 0.70f, 0.20f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {1.0, 0.8, 0.3},
    .surface_roughness     = 0.03f,
    .emissive_color         = {0.1,0.08,0.02},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.05f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.03f,
    .metallic               = 1.0f,
    .ior                    = 0.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.7f
};

/* ------------------------------------------------------------------------
 * 25. SNOW
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_SNOW = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_EMISSIVE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_ROUGHNESS |
                              EFFECT_SHEEN | EFFECT_SUBSURFACE | EFFECT_BUMP,
    .color                  = {0.95f, 0.95f, 1.00f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 0.9f,
    .tint                   = {1.0f, 1.0f, 1.2f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.60f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.075f,
    .bump_frequency         = 7.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.70f, 0.70f, 0.90f},
    .gooch_warm             = {1.00f, 1.00f, 1.00f},
    .back_glow_color        = {0.20f, 0.20f, 0.40f},
    .rim_color              = {0.90f, 0.90f, 1.00f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.5,0.5,0.5},
    .surface_roughness     = 0.8f,
    .emissive_color         = {0.05f, 0.05f, 0.10f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.10f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.05f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.1f,
    .metallic               = 0.0f,
    .ior                    = 1.3f,
    .subsurface_strength    = 0.8f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0f, 1.0f, 1.0f},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 2.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 26. DIRT
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_DIRT = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_ROUGHNESS | EFFECT_BUMP,
    .color                  = {0.5, 0.35, 0.2},
    .ambient_light_factor   = 0.7f,
    .alpha                  = 1.0f,
    .saturation             = 2.5f,
    .tint                   = {1.2,1.0,0.8},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.1f,
    .bump_frequency         = 8.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.20f, 0.18f, 0.12f},
    .rim_exponent           = 1.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.9f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.25f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 27. NEON
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_NEON = {
    .render_method          = MODE_PHONG | EFFECT_GOOCH | EFFECT_BACK_GLOW |
                              EFFECT_RIM | EFFECT_EMISSIVE | EFFECT_EMISSIVE_PULSE |
                              EFFECT_STROBE | EFFECT_IRIDESCENCE | EFFECT_GLITCH |
                              EFFECT_FRINGE | EFFECT_SATURATION | EFFECT_ANISOTROPIC,
    .color                  = {0.00f, 1.00f, 1.00f},
    .ambient_light_factor   = 0.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {0.0f, 2.0f, 2.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 1.0f, 1.0f},
    .gooch_warm             = {0.0f, 1.0f, 1.0f},
    .back_glow_color        = {0.00f, 1.00f, 1.00f},
    .rim_color              = {0.0,1.0,1.0},
    .rim_exponent           = 16.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.1f,
    .emissive_color         = {0.0, 2.0, 2.0},
    .emissive_pulse_frequency = 4.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.50f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 8.0f,
    .strobe_phase           = 0.25f,
    .skip_fog               = true,
    .iridescence_strength   = 0.1f,
    .glitch_intensity       = 0.3f,
    .fringe_intensity       = 0.1f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 28. VELVET
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_VELVET = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_SHEEN | EFFECT_ROUGHNESS | EFFECT_BACK_GLOW | 
                              EFFECT_ANISOTROPIC,
    .color                  = {0.55, 0.1, 0.15},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 1.2f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.80f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.3, 0.05, 0.08},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.9f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.20f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0, 0.5, 0.6},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 2.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 29. MARBLE
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_MARBLE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_OREN_NAYAR | EFFECT_SPECULAR | EFFECT_FRESNEL | 
                              EFFECT_SUBSURFACE | EFFECT_TRANSMISSION,
    .color                  = {0.85f, 0.82f, 0.78f},
    .ambient_light_factor   = 0.90f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.15f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.02f,
    .bump_frequency         = 256.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.2f,
    .specular_tint          = {0.9,0.9,0.9},
    .surface_roughness      = 0.1f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.05f,
    .metallic               = 0.0f,
    .ior                    = 1.6f,
    .subsurface_strength    = 0.8f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.9f, 0.9f, 0.85f}
};

/* ------------------------------------------------------------------------
 * 30. WAX
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WAX = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE | EFFECT_SUBSURFACE | EFFECT_TRANSMISSION,
    .color                  = {0.9, 0.8, 0.6},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 0.87f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.05f,
    .bump_frequency         = 16.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.5f,
    .specular_tint          = {0.8,0.7,0.5},
    .surface_roughness      = 0.2f,
    .emissive_color         = {0.05f, 0.03f, 0.01f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.08f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .subsurface_strength    = 1.2f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {1.0f, 0.8f, 0.5f}
};

/* ------------------------------------------------------------------------
 * 31. PEARL
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_PEARL = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_SATURATION | 
                              EFFECT_SUBSURFACE | EFFECT_TRANSMISSION,
    .color                  = {0.95f, 0.90f, 0.85f},
    .ambient_light_factor   = 0.90f,
    .alpha                  = 1.0f,
    .saturation             = 1.5f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.1f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.1f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.6f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.08f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .subsurface_strength    = 0.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.9f, 0.9f, 1.0f}
};

/* ------------------------------------------------------------------------
 * 32. CERAMIC
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CERAMIC = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_CLEARCOAT |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_SATURATION,
    .color                  = {0.95,0.93,0.9},
    .ambient_light_factor   = 0.90f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.05f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.05f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.9f, 0.9f, 1.0f},
    .clearcoat_roughness    = 0.1f,
    .clearcoat_strength     = 0.5f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
};

/* ------------------------------------------------------------------------
 * 33. CHALK
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CHALK = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_MINNAERT |
                              EFFECT_DIFFUSE_WRAP | EFFECT_ROUGHNESS,
    .color                  = {0.80f, 0.80f, 0.85f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.3f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint          = {0.0f, 0.0f, 0.0f},
    .surface_roughness      = 0.95f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.45f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 34. POSTERIZED
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_POSTERIZED = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_POSTERIZE |
                              EFFECT_SATURATION | EFFECT_SPECULAR_THRESH,
    .color                  = {1.0, 0.6, 0.2},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 2.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint          = {1.0f, 1.0f, 1.0f},
    .surface_roughness      = 0.1f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 8,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 35. FROST
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_FROST = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_ROUGHNESS | EFFECT_SUBSURFACE | EFFECT_TRANSMISSION,
    .color                  = {0.85,0.9,1.0},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.40f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.02f,
    .bump_frequency         = 64.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.3f,
    .specular_tint          = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.6f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.50f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .subsurface_strength    = 0.8f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.9,0.95,1.0}
};

/* ------------------------------------------------------------------------
 * 36. RUST
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_RUST = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_BUMP | EFFECT_ROUGHNESS | EFFECT_BACK_GLOW,
    .color                  = {0.60f, 0.20f, 0.05f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.60f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.2f,
    .bump_frequency         = 16.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.1,0.02,0.0},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {0.0f, 0.0f, 0.0f},
    .surface_roughness     = 0.85f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.50f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 37. CARBON
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CARBON = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_SPECULAR | EFFECT_FRINGE |
                              EFFECT_ANISOTROPIC,
    .color                  = {0.10f, 0.10f, 0.11f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.1f,
    .bump_frequency         = 128.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint          = {0.75,0.75,0.75},
    .surface_roughness      = 0.3f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.025f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.9f
};

/* ------------------------------------------------------------------------
 * 38. CHROME
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CHROME = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_SPECULAR |
                              EFFECT_FRESNEL | EFFECT_RIM | EFFECT_ANISOTROPIC,
    .color                  = {1.0f, 1.0f, 1.0f},
    .ambient_light_factor   = 0.30f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.8f, 0.8f, 0.8f},
    .rim_exponent           = 6.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.0f,
    .specular_tint         = {1.0f, 1.0f, 1.0f},
    .surface_roughness     = 0.01f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 1.0f,
    .ior                    = 0.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.3f
};

/* ------------------------------------------------------------------------
 * 39. EMERALD
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_EMERALD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_SUBSURFACE | 
                              EFFECT_TRANSMISSION | EFFECT_ALPHA,
    .color                  = {0.1, 0.8, 0.5},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 0.95f,
    .saturation             = 1.5f,
    .tint                   = {1.0f, 1.2f, 0.85f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.3f,
    .specular_tint          = {0.8,1.0,0.85},
    .surface_roughness      = 0.02f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.2f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.58f,
    .subsurface_strength    = 0.6f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {0.6f, 1.0f, 0.6f}
};

/* ------------------------------------------------------------------------
 * 40. OIL SLICK
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_OILSLICK = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_FRESNEL | EFFECT_SATURATION | EFFECT_TRANSMISSION,
    .color                  = {0.1, 0.1, 0.15},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 2.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .transmission_strength  = 0.2f,
    .specular_tint          = {0.5,0.5,0.6},
    .surface_roughness      = 0.02f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 1.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_roughness    = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f,
    .transmission_tint      = {1.0f, 1.0f, 1.0f}
};

#ifdef __cplusplus
}
#endif

#endif /* MATERIAL_DEFINITION_H */