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
    EFFECT_RESERVED_29 = (1 << 29),
    EFFECT_RESERVED_30 = (1 << 30),
    EFFECT_RESERVED_31 = (1 << 31)
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

    real         specular_exponent;
    vec3         specular_color;
    real         specular_threshold;

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
    real         metallic;          /* 0.0 = dielectric, 1.0 = metal */
    real         ior;               /* index of refraction (>0) */

    vec3         clearcoat_color;
    real         clearcoat_exponent;
    real         clearcoat_strength;
    vec3         sheen_color;
    real         sheen_exponent;
    real         sheen_strength;

    /* ---- Anisotropy ---- */
    real         anisotropic;       /* 0.0 = isotropic, 1.0 = full anisotropy */

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
    FIELD_REAL("specular_exponent"),
    FIELD_VEC3("specular_color"),
    FIELD_REAL("specular_threshold"),
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
    FIELD_VEC3("clearcoat_color"),
    FIELD_REAL("clearcoat_exponent"),
    FIELD_REAL("clearcoat_strength"),
    FIELD_VEC3("sheen_color"),
    FIELD_REAL("sheen_exponent"),
    FIELD_REAL("sheen_strength"),
    FIELD_REAL("anisotropic"),      /* new */
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
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
    .specular_exponent      = 4.0f,
    .specular_color         = {0.5f, 0.5f, 0.5f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 7. WATER – roughness 0.02, IOR 1.33
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WATER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_BACK_GLOW | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE,
    .color                  = {0.05f, 0.30f, 0.50f},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 0.75f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.30f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.4f,
    .bump_frequency         = 32.0f,
    .bump_speed             = 4.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.20f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 384.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 8. GRASS – roughness 0.65
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GRASS = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_BACK_GLOW | EFFECT_RIM | EFFECT_FRESNEL |
                              EFFECT_SPECULAR | EFFECT_IRIDESCENCE | EFFECT_GLITCH |
                              EFFECT_FRINGE | EFFECT_CEL_SHADING | EFFECT_SHEEN,
    .color                  = {0.0f, 0.24f, 0.08f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.2f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 4,
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
    .specular_exponent      = 4.0f,
    .specular_color         = {0.3f, 0.4f, 0.15f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.65f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.3f, 0.6f, 0.1f},
    .sheen_exponent         = 3.0f,
    .sheen_strength         = 1.5f,
    .anisotropic            = -0.25f
};

/* ------------------------------------------------------------------------
 * 9. CLOTH – roughness 0.85
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CLOTH = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_SPECULAR | EFFECT_SHEEN,
    .color                  = {0.6f, 0.2f, 0.3f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.50f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.01f,
    .bump_frequency         = 30.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 2.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.85f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0f, 1.0f, 1.0f},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 2.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 10. WOOD – roughness 0.40, IOR 1.55, anisotropic 0.6
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WOOD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_DIFFUSE_WRAP |
                              EFFECT_OREN_NAYAR | EFFECT_RIM | EFFECT_SPECULAR |
                              EFFECT_ROUGHNESS | EFFECT_CLEARCOAT | EFFECT_ANISOTROPIC,
    .color                  = {0.60f, 0.35f, 0.15f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.40f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 48.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.15f, 0.08f, 0.03f},
    .rim_exponent           = 2.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 64.0f,
    .specular_color         = {0.5f, 0.35f, 0.20f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.40f,
    .metallic               = 0.0f,
    .ior                    = 1.55f,
    .clearcoat_color        = {0.9f, 0.8f, 0.5f},
    .clearcoat_exponent     = 32.0f,
    .clearcoat_strength     = 1.5f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.75f
};

/* ------------------------------------------------------------------------
 * 11. METAL – roughness 0.02, anisotropic 0.5
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_METAL = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_IRIDESCENCE | EFFECT_ANISOTROPIC,
    .color                  = {0.80f, 0.80f, 0.85f},
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
    .specular_exponent      = 128.0f,
    .specular_color         = {0.80f, 0.80f, 0.85f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.30f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 1.0f,
    .ior                    = 0.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.8f
};

/* ------------------------------------------------------------------------
 * 12. GLASS – roughness 0.02
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GLASS = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE | EFFECT_IRIDESCENCE | EFFECT_FRINGE,
    .color                  = {0.85f, 0.95f, 1.0f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.3f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.03f,
    .bump_frequency         = 80.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.05f, 0.10f, 0.20f},
    .rim_color              = {0.30f, 0.50f, 0.80f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 512.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 13. SKIN – roughness 0.50, IOR 1.4
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_SKIN = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_SHEEN,
    .color                  = {0.87f, 0.8f, 0.64f},
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
    .gooch_cool             = {0.25f, 0.15f, 0.22f},
    .gooch_warm             = {1.00f, 0.80f, 0.65f},
    .back_glow_color        = {0.15f, 0.05f, 0.05f},
    .rim_color              = {0.45f, 0.20f, 0.15f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 16.0f,
    .specular_color         = {0.50f, 0.40f, 0.35f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0f, 0.8f, 0.7f},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 0.8f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 14. RUBBER – roughness 0.50
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_RUBBER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_SPECULAR,
    .color                  = {0.10f, 0.10f, 0.10f},
    .ambient_light_factor   = 0.5f,
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
    .specular_exponent      = 2.0f,
    .specular_color         = {0.25f, 0.25f, 0.25f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 15. ICE – roughness 0.02, IOR 1.31
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_ICE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_EMISSIVE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE,
    .color                  = {0.65f, 0.85f, 0.95f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.85f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.08f,
    .bump_frequency         = 64.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.10f, 0.25f, 0.40f},
    .rim_color              = {0.70f, 0.85f, 1.0f},
    .rim_exponent           = 4.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 256.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.31f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 16. STONE – roughness 0.35
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_STONE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_RIM | EFFECT_SPECULAR | EFFECT_ROUGHNESS,
    .color                  = {0.48f, 0.43f, 0.38f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 0.9f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 1.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.10f,
    .bump_frequency         = 24.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.12f, 0.11f, 0.09f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 16.0f,
    .specular_color         = {0.40f, 0.38f, 0.35f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.35f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 17. LAVA – roughness 0.50
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
    .bump_frequency         = 32.0f,
    .bump_speed             = 2.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {1.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.50f, 0.25f, 0.0f},
    .rim_color              = {0.80f, 0.15f, 0.0f},
    .rim_exponent           = 1.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.50f, 0.33f, 0.0f},
    .emissive_pulse_frequency = 1.0471975512f,
    .emissive_pulse_phase   = 1.57f,
    .emissive_pulse_amplitude = 0.25f,
    .strobe_color           = {0.10f, 0.04f, 0.0f},
    .strobe_frequency       = 0.47140452079f,
    .strobe_phase           = 2.10f,
    .skip_fog               = false,
    .iridescence_strength   = 0.0f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.50f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.50f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 18. TOON – stylized
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_TOON = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_GOOCH |
                              EFFECT_RIM | EFFECT_SPECULAR | EFFECT_SPECULAR_THRESH |
                              EFFECT_CEL_SHADING,
    .color                  = {0.90f, 0.70f, 0.40f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.5f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 3,
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
    .specular_exponent      = 100.0f,
    .specular_color         = {1.0f, 1.0f, 0.8f},
    .specular_threshold     = 0.90f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 19. HOLOGRAM – stylized
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_HOLOGRAM = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BACK_GLOW | EFFECT_RIM | EFFECT_FRESNEL |
                              EFFECT_EMISSIVE | EFFECT_EMISSIVE_PULSE | EFFECT_STROBE |
                              EFFECT_IRIDESCENCE | EFFECT_GLITCH | EFFECT_FRINGE,
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
    .rim_color              = {0.40f, 0.80f, 1.0f},
    .rim_exponent           = 64.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.60f, 0.85f, 1.0f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.05f, 0.15f, 0.25f},
    .emissive_pulse_frequency = 0.8f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.15f,
    .strobe_color           = {0.10f, 0.35f, 0.55f},
    .strobe_frequency       = 1.0f,
    .strobe_phase           = 0.5f,
    .skip_fog               = true,
    .iridescence_strength   = 0.30f,
    .glitch_intensity       = 0.50f,
    .fringe_intensity       = 0.25f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.7f
};

/* ------------------------------------------------------------------------
 * 20. IRIDESCENT – stylized
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_IRIDESCENT = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_EMISSIVE |
                              EFFECT_EMISSIVE_PULSE | EFFECT_STROBE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_SATURATION,
    .color                  = {1.0f, 1.0f, 1.0f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 0.9f,
    .saturation             = 2.5f,
    .tint                   = {2.5f, 1.5f, 1.0f},
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
    .rim_color              = {1.00f, 0.50f, 0.00f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 16.0f,
    .specular_color         = {1.0f, 1.0f, 0.8f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 21. PLASTIC – roughness 0.10
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_PLASTIC = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_GOOCH | EFFECT_RIM |
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
    .gooch_cool             = {0.10f, 0.25f, 0.40f},
    .gooch_warm             = {0.50f, 0.80f, 1.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.40f, 0.70f, 0.90f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 32.0f,
    .specular_color         = {0.80f, 0.90f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.10f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 1.0f},
    .clearcoat_exponent     = 32.0f,
    .clearcoat_strength     = 2.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 22. BRICK – roughness 0.25
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_BRICK = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_ROUGHNESS,
    .color                  = {0.61f, 0.27f, 0.23f},
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
    .gooch_cool             = {0.35f, 0.15f, 0.12f},
    .gooch_warm             = {0.85f, 0.40f, 0.30f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 23. LEATHER – roughness 0.15
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_LEATHER = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_DIFFUSE_WRAP | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_SPECULAR | EFFECT_ROUGHNESS | EFFECT_CLEARCOAT,
    .color                  = {0.30f, 0.15f, 0.09f},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.50f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.15f,
    .bump_frequency         = 32.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.18f, 0.09f, 0.05f},
    .gooch_warm             = {0.42f, 0.21f, 0.13f},
    .back_glow_color        = {0.08f, 0.04f, 0.02f},
    .rim_color              = {0.36f, 0.18f, 0.11f},
    .rim_exponent           = 1.5f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 4.0f,
    .specular_color         = {0.45f, 0.23f, 0.14f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 16.0f,
    .clearcoat_strength     = 2.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 24. GOLD – roughness 0.02, anisotropic 0.5
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_GOLD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | 
                              EFFECT_OREN_NAYAR | EFFECT_GOOCH | EFFECT_BACK_GLOW |
                              EFFECT_RIM | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE | EFFECT_IRIDESCENCE | EFFECT_ANISOTROPIC,
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
    .specular_exponent      = 64.0f,
    .specular_color         = {1.0f, 0.75f, 0.3f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.20f, 0.15f, 0.05f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.12f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 1.0f,
    .ior                    = 0.0f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.8f
};

/* ------------------------------------------------------------------------
 * 25. SNOW – roughness 0.05, IOR 1.3
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_SNOW = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_BACK_GLOW | EFFECT_RIM |
                              EFFECT_FRESNEL | EFFECT_SPECULAR | EFFECT_EMISSIVE |
                              EFFECT_IRIDESCENCE | EFFECT_FRINGE | EFFECT_ROUGHNESS |
                              EFFECT_SHEEN,
    .color                  = {0.95f, 0.95f, 1.00f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 0.9f,
    .tint                   = {1.0f, 1.0f, 1.2f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.60f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.70f, 0.70f, 0.90f},
    .gooch_warm             = {1.00f, 1.00f, 1.00f},
    .back_glow_color        = {0.20f, 0.20f, 0.40f},
    .rim_color              = {0.90f, 0.90f, 1.00f},
    .rim_exponent           = 2.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 64.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.05f,
    .metallic               = 0.0f,
    .ior                    = 1.3f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0f, 1.0f, 1.0f},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 1.5f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 26. DIRT – roughness 0.25
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_DIRT = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_GOOCH | EFFECT_ROUGHNESS,
    .color                  = {0.35f, 0.20f, 0.10f},
    .ambient_light_factor   = 0.45f,
    .alpha                  = 1.0f,
    .saturation             = 2.5f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 1,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.0f,
    .bump_frequency         = 0.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.15f, 0.12f, 0.08f},
    .gooch_warm             = {0.40f, 0.35f, 0.30f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.20f, 0.18f, 0.12f},
    .rim_exponent           = 1.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 27. NEON – stylized
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_NEON = {
    .render_method          = MODE_PHONG | EFFECT_GOOCH | EFFECT_BACK_GLOW |
                              EFFECT_RIM | EFFECT_EMISSIVE | EFFECT_EMISSIVE_PULSE |
                              EFFECT_STROBE | EFFECT_IRIDESCENCE | EFFECT_GLITCH |
                              EFFECT_FRINGE | EFFECT_SATURATION,
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
    .rim_color              = {0.00f, 1.00f, 1.00f},
    .rim_exponent           = 16.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.00f, 4.00f, 4.00f},
    .emissive_pulse_frequency = 4.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.50f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 8.0f,
    .strobe_phase           = 0.25f,
    .skip_fog               = true,
    .iridescence_strength   = 0.05f,
    .glitch_intensity       = 0.2f,
    .fringe_intensity       = 0.1f,
    .posterize_levels       = 0,
    .double_sided           = true,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 28. VELVET – roughness 0.40
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_VELVET = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_OREN_NAYAR |
                              EFFECT_SHEEN | EFFECT_ROUGHNESS | EFFECT_BACK_GLOW,
    .color                  = {0.40f, 0.05f, 0.10f},
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
    .back_glow_color        = {0.10f, 0.02f, 0.04f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.40f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {1.0f, 0.6f, 0.7f},
    .sheen_exponent         = 2.0f,
    .sheen_strength         = 1.8f,
    .anisotropic            = -0.6f
};

/* ------------------------------------------------------------------------
 * 29. MARBLE – roughness 0.05, IOR 1.6
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_MARBLE = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_OREN_NAYAR | EFFECT_SPECULAR | EFFECT_FRESNEL,
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
    .specular_exponent      = 64.0f,
    .specular_color         = {0.9f, 0.9f, 0.9f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 30. WAX – roughness 0.08
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_WAX = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_SPECULAR |
                              EFFECT_EMISSIVE,
    .color                  = {0.85f, 0.75f, 0.50f},
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
    .specular_exponent      = 32.0f,
    .specular_color         = {0.9f, 0.8f, 0.6f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 31. PEARL – roughness 0.08
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_PEARL = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_SATURATION,
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
    .specular_exponent      = 128.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.40f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.08f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 32. CERAMIC – roughness 0.05
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CERAMIC = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_CLEARCOAT |
                              EFFECT_SPECULAR | EFFECT_FRESNEL | EFFECT_SATURATION,
    .color                  = {0.95f, 0.92f, 0.88f},
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
    .specular_exponent      = 128.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 64.0f,
    .clearcoat_strength     = 3.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 33. CHALK – roughness 0.60
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
    .minnaert_k             = 0.7f,
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.60f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 34. POSTERIZED – stylized
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_POSTERIZED = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_POSTERIZE |
                              EFFECT_SATURATION | EFFECT_SPECULAR_THRESH,
    .color                  = {0.90f, 0.50f, 0.20f},
    .ambient_light_factor   = 1.0f,
    .alpha                  = 1.0f,
    .saturation             = 1.8f,
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
    .specular_exponent      = 2.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.5f,
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
    .posterize_levels       = 6,
    .double_sided           = false,
    .roughness              = 0.0f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 35. FROST – roughness 0.50
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_FROST = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_ALPHA |
                              EFFECT_BUMP | EFFECT_FRESNEL | EFFECT_ROUGHNESS,
    .color                  = {0.90f, 0.95f, 1.0f},
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 36. RUST – roughness 0.50
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
    .bump_amplitude         = 0.15f,
    .bump_frequency         = 16.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.05f, 0.01f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 37. CARBON – roughness 0.20, anisotropic 0.3
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_CARBON = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_BUMP |
                              EFFECT_SPECULAR | EFFECT_ROUGHNESS | EFFECT_FRINGE |
                              EFFECT_ANISOTROPIC,
    .color                  = {0.10f, 0.10f, 0.10f},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 1.0f,
    .tint                   = {1.0f, 1.0f, 1.0f},
    .cel_bands              = 0,
    .diffuse_wrap           = 0,
    .oren_nayar_sigma       = 0.0f,
    .minnaert_k             = 0.0f,
    .bump_amplitude         = 0.05f,
    .bump_frequency         = 128.0f,
    .bump_speed             = 0.0f,
    .gooch_cool             = {0.0f, 0.0f, 0.0f},
    .gooch_warm             = {0.0f, 0.0f, 0.0f},
    .back_glow_color        = {0.0f, 0.0f, 0.0f},
    .rim_color              = {0.0f, 0.0f, 0.0f},
    .rim_exponent           = 0.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 16.0f,
    .specular_color         = {0.20f, 0.20f, 0.20f},
    .specular_threshold     = 0.0f,
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
    .roughness              = 0.20f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.9f
};

/* ------------------------------------------------------------------------
 * 38. CHROME – roughness 0.02, anisotropic 0.2
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
    .rim_color              = {1.0f, 1.0f, 1.0f},
    .rim_exponent           = 3.0f,
    .fresnel_color          = {0.0f, 0.0f, 0.0f},
    .fresnel_exponent       = 5.0f,
    .specular_exponent      = 1024.0f,
    .specular_color         = {1.0f, 1.0f, 1.0f},
    .specular_threshold     = 0.0f,
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
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = -0.5f
};

/* ------------------------------------------------------------------------
 * 39. EMERALD – roughness 0.02, IOR 1.57
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_EMERALD = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_SPECULAR | EFFECT_FRESNEL,
    .color                  = {0.10f, 0.70f, 0.30f},
    .ambient_light_factor   = 0.80f,
    .alpha                  = 1.0f,
    .saturation             = 1.5f,
    .tint                   = {1.0f, 1.2f, 0.8f},
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
    .specular_exponent      = 256.0f,
    .specular_color         = {0.8f, 1.0f, 0.8f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.50f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.57f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

/* ------------------------------------------------------------------------
 * 40. OIL SLICK – roughness 0.02
 * ------------------------------------------------------------------------ */
const struct material_definition DEFAULT_MATERIAL_OILSLICK = {
    .render_method          = MODE_PHONG | EFFECT_AMBIENT_LIGHT | EFFECT_IRIDESCENCE |
                              EFFECT_FRESNEL | EFFECT_SATURATION,
    .color                  = {0.05f, 0.05f, 0.08f},
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
    .specular_exponent      = 0.0f,
    .specular_color         = {0.0f, 0.0f, 0.0f},
    .specular_threshold     = 0.0f,
    .emissive_color         = {0.0f, 0.0f, 0.0f},
    .emissive_pulse_frequency = 0.0f,
    .emissive_pulse_phase   = 0.0f,
    .emissive_pulse_amplitude = 0.0f,
    .strobe_color           = {0.0f, 0.0f, 0.0f},
    .strobe_frequency       = 0.0f,
    .strobe_phase           = 0.0f,
    .skip_fog               = false,
    .iridescence_strength   = 0.90f,
    .glitch_intensity       = 0.0f,
    .fringe_intensity       = 0.0f,
    .posterize_levels       = 0,
    .double_sided           = false,
    .roughness              = 0.02f,
    .metallic               = 0.0f,
    .ior                    = 1.5f,
    .clearcoat_color        = {0.0f, 0.0f, 0.0f},
    .clearcoat_exponent     = 0.0f,
    .clearcoat_strength     = 0.0f,
    .sheen_color            = {0.0f, 0.0f, 0.0f},
    .sheen_exponent         = 0.0f,
    .sheen_strength         = 0.0f,
    .anisotropic            = 0.0f
};

#ifdef __cplusplus
}
#endif

#endif /* MATERIAL_DEFINITION_H */