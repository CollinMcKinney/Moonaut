#ifndef MATERIAL_DEFINITION_H
#define MATERIAL_DEFINITION_H

#include "../common.h"
#include "../reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Shading mode enum
 * ========================================================================= */
typedef enum shading_mode {
    SHADE_WIREFRAME = 0,
    SHADE_FLAT,
    SHADE_GOURAUD,
    SHADE_PHONG
} shading_mode;

/* ----- Enum name table (linked to the field via FIELD_ENUM) ----- */
TAG_ENUM_BEGIN(shading_mode)
    TAG_ENUM_ENTRY(SHADE_WIREFRAME,     "wireframe")
    TAG_ENUM_ENTRY(SHADE_FLAT,          "flat")
    TAG_ENUM_ENTRY(SHADE_GOURAUD,       "gouraud")
    TAG_ENUM_ENTRY(SHADE_PHONG,         "phong")
TAG_ENUM_END(shading_mode)

/* =========================================================================
 * Material tag struct
 * ========================================================================= */
typedef struct material_definition {
    /* Render technique */
    enum32       mode;                  /* SHADE_WIREFRAME, SHADE_FLAT, SHADE_GOURAUD, SHADE_PHONG */

    /* Base appearance */
    vec3         color;                 /* Diffuse (base) colour of the material. */
    real         ambient_light_factor;  /* 0.0 = fully darkened, 1.0 = fully lit. */
    real         alpha;                 /* 0.0 = fully transparent, 1.0 = fully opaque. */
    real         saturation;            /* 0 = greyscale, 1 = normal, >1 = oversaturate. */
    vec3         tint;                  /* Multiplicative colour after lighting. Default {1,1,1}. */
    
    /* Diffuse models */
    i32          cel_bands;             /* ≥2 for cel shading (posterises N·L into bands). 0/1 = off. */
    i32          diffuse_wrap;          /* 0 = Lambert, 1 = smoothstep wrapping for softer falloff. */
    real         oren_nayar_sigma;      /* Roughness (0 = Lambert, >0 enables Oren‑Nayar diffuse). Typical 0.2‑1.0. */
    real         minnaert_k;            /* Limb darkening exponent (0..1). 0 = off, 0.5 = classic Minnaert. */
    
    /* Procedural bump */
    real         bump_amplitude;        /* Normal perturbation amount. 0 = off, typical 0.02–0.1. */
    real         bump_frequency;        /* Density of the bumps (higher = more ripples). */
    real         bump_speed;            /* Animation speed factor (multiplied by time). 0 = static. */
    
    /* Colour remapping */
    vec3         gooch_cool;            /* Cool colour for Gooch shading (shadows). */
    vec3         gooch_warm;            /* Warm colour for Gooch shading (lit areas). */
    vec3         back_glow_color;       /* Colour of back‑face (X‑ray) glow (additive). */
    
    /* Edge and glancing effects */
    vec3         rim_color;             /* Colour of rim light (additive edge glow). */
    real         rim_exponent;          /* Rim sharpness (0 = off, typical 2‑5). Higher = tighter rim. */
    vec3         fresnel_color;         /* Colour to blend at glancing angles (multiplicative blend). */
    real         fresnel_exponent;      /* Fresnel strength (0 = off, typical 3‑5). Higher = sharper transition. */
    
    /* Specular highlight */
    real         specular_exponent;     /* Blinn‑Phong exponent (0 = off). Typical 8‑512. */
    vec3         specular_color;        /* Highlight colour. */
    real         specular_threshold;    /* >0 enables toon specular hard cutoff (0..1). */
    
    /* Time‑based */
    vec3         emissive_color;        /* Constant self‑light colour (additive). */
    real         emissive_pulse_frequency; /* Pulse rate (cycles per second). */
    real         emissive_pulse_phase;     /* Phase offset (radians). */
    real         emissive_pulse_amplitude; /* Pulse strength (0 = off, typical 0.1‑0.5). */
    vec3         strobe_color;          /* Additive flashing colour. */
    real         strobe_frequency;      /* Flash frequency (Hz). 0 = off. */
    real         strobe_phase;          /* Phase offset (radians). */

    /* Atmosphere */
    i32           skip_fog;            /* 0 = apply global fog, 1 = skip (for objects that should stay bright). */
    
    /* Post‑lighting colour treatment */
    
    real         iridescence_strength;  /* View‑angle rainbow shift (0 = off, typical 0.1‑1.0). */
    
    /* Stylizing effects */
    real         glitch_intensity;      /* Noise overlay strength (0 = off, typical 0.05‑0.2). */
    real         fringe_intensity;      /* Chromatic aberration strength (0 = off, typical 0.01‑0.05). */
    i32          posterize_levels;      /* ≥2 quantises final colour into that many levels. 0/1 = off. */
} material_definition;

/* =========================================================================
 * Tag group definition ('mtrl')
 * ========================================================================= */
TAG_GROUP_BEGIN(material, 'mtrl', sizeof(struct material_definition))
    FIELD_ENUM("mode", shading_mode),
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
    FIELD_VEC3("fresnel_color"),
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
    FIELD_I32("skip_fog"),           /* bool stored as i32 */
    FIELD_REAL("iridescence_strength"),
    FIELD_REAL("glitch_intensity"),
    FIELD_REAL("fringe_intensity"),
    FIELD_I32("posterize_levels"),
    FIELD_TERMINATOR,
TAG_GROUP_END(material, sizeof(struct material_definition))


/* =========================================================================
* Material defaults
* ========================================================================= */

const struct material_definition DEFAULT_MATERIAL_WIREFRAME = {
    /*.mode                =*/ SHADE_WIREFRAME,
    /*.color               =*/ {1.0f, 1.0f, 1.0f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_FLAT = {
    /*.mode                =*/ SHADE_FLAT,
    /*.color               =*/ {1.0f, 1.0f, 1.0f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_GOURAUD = {
    /*.mode                =*/ SHADE_GOURAUD,
    /*.color               =*/ {1.0f, 1.0f, 1.0f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_PHONG = {
    /*.mode                =*/ SHADE_PHONG,
    /*.color               =*/ {1.0f, 1.0f, 1.0f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_WATER = {
    /*.mode                =*/ SHADE_PHONG,
    /*.color               =*/ {0.10f, 0.35f, 0.65f},  /* brighter tropical blue */
    /*.ambient_light_factor=*/ 0.80f,  /* shadowed water still looks bright */
    /*.alpha               =*/ 0.90f,  /* mostly opaque but still translucent */
    /*.saturation          =*/ 1.00f,
    /*.tint                =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands           =*/ 0,
    /*.diffuse_wrap        =*/ 0.20f,  /* gentle edge softening */
    /*.oren_nayar_sigma    =*/ 0.00f,
    /*.minnaert_k          =*/ 0.00f,
    /*.bump_amplitude      =*/ 0.40f,  /* subtle ripples */
    /*.bump_frequency      =*/ 32.0f,
    /*.bump_speed          =*/ 2.00f,
    /*.gooch_cool          =*/ {0,0,0},
    /*.gooch_warm          =*/ {0,0,0},
    /*.back_glow_color     =*/ {0.0f, 0.20f, 0.0f},  /* brighter subsurface glow */
    /*.rim_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_exponent        =*/ 0.00f,
    /*.fresnel_color       =*/ {0.55f, 0.85f, 1.00f}, /* sky reflection */
    /*.fresnel_exponent    =*/ 3.00f,  /* slightly lower so it doesn't darken too much */
    /*.specular_exponent   =*/ 384.0f, /* still sharp sun glitter */
    /*.specular_color      =*/ {1.00f, 1.00f, 1.00f},
    /*.specular_threshold  =*/ 0.00f,
    /*.emissive_color      =*/ {0.00f, 0.00f, 0.00f}, /* no artificial glow */
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color        =*/ {0,0,0},
    /*.strobe_frequency    =*/ 0.00f,
    /*.strobe_phase        =*/ 0.00f,
    /*.skip_fog            =*/ 0,
    /*.iridescence_strength=*/ 0.05f, /* barely-there oil-slick rainbow */
    /*.glitch_intensity    =*/ 0.00f,
    /*.fringe_intensity    =*/ 0.01f  /* subtle chromatic aberration */
};

const struct material_definition DEFAULT_MATERIAL_GRASS = {
    /*.mode                =*/ SHADE_GOURAUD,
    /*.color               =*/ {0.25f, 0.6f, 0.15f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.3f,
    /*.tint                =*/ {1,1,1},
    /*.cel_bands           =*/ 3,
    /*.diffuse_wrap        =*/ 1,
    /*.oren_nayar_sigma    =*/ 0.0f,
    /*.minnaert_k          =*/ 0.0f,
    /*.bump_amplitude      =*/ 0.1f,
    /*.bump_frequency      =*/ 32.0f,
    /*.bump_speed          =*/ 0.0f,
    /*.gooch_cool          =*/ {0,0,0},
    /*.gooch_warm          =*/ {0,0,0},
    /*.back_glow_color     =*/ {0.05f, 0.2f, 0.02f},
    /*.rim_color           =*/ {0.5f, 0.9f, 0.2f},
    /*.rim_exponent        =*/ 1.5f,
    /*.fresnel_color       =*/ {0.3f, 0.7f, 0.4f},
    /*.fresnel_exponent    =*/ 2.0f,
    /*.specular_exponent   =*/ 16.0f,
    /*.specular_color      =*/ {0.3f, 0.5f, 0.1f},
    /*.specular_threshold  =*/ 0.0f,
    /*.emissive_color      =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.0f,
    /*.emissive_pulse_phase     =*/ 0.0f,
    /*.emissive_pulse_amplitude =*/ 0.0f,
    /*.strobe_color        =*/ {0,0,0},
    /*.strobe_frequency    =*/ 0.0f,
    /*.strobe_phase        =*/ 0.0f,
    /*.skip_fog            =*/ 0,
    /*.iridescence_strength=*/ 0.08f,
    /*.glitch_intensity    =*/ 0.0f,
    /*.fringe_intensity    =*/ 0.01f,
};

const struct material_definition DEFAULT_MATERIAL_CLOTH = {
    /*.mode                =*/ SHADE_GOURAUD,     /* or GOURAUD, depending on your model */
    /*.color               =*/ {0.6f, 0.2f, 0.3f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1,1,1},
    /*.cel_bands           =*/ 0,
    /*.diffuse_wrap        =*/ 1,             /* softer falloff */
    /*.oren_nayar_sigma    =*/ 0.7f,          /* fabric roughness */
    /*.minnaert_k          =*/ 0.0f,
    /*.bump_amplitude      =*/ 0.01f,
    /*.bump_frequency      =*/ 30.0f,
    /*.bump_speed          =*/ 0.0f,
    /*.gooch_cool          =*/ {0,0,0},
    /*.gooch_warm          =*/ {0,0,0},
    /*.back_glow_color     =*/ {0,0,0},
    /*.rim_color           =*/ {0,0,0},
    /*.rim_exponent        =*/ 0.0f,
    /*.fresnel_color       =*/ {0,0,0},
    /*.fresnel_exponent    =*/ 0.0f,
    /*.specular_exponent   =*/ 0.0f,
    /*.specular_color      =*/ {0,0,0},
    /*.specular_threshold  =*/ 0.0f,
    /*.emissive_color      =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.0f,
    /*.emissive_pulse_phase     =*/ 0.0f,
    /*.emissive_pulse_amplitude =*/ 0.0f,
    /*.strobe_color        =*/ {0,0,0},
    /*.strobe_frequency    =*/ 0.0f,
    /*.strobe_phase        =*/ 0.0f,
    /*.skip_fog            =*/ 0,
    /*.iridescence_strength=*/ 0.0f,
    /*.glitch_intensity    =*/ 0.0f,
    /*.fringe_intensity    =*/ 0.0f,
    /*.posterize_levels    =*/ 0
};

#ifdef __cplusplus
}
#endif
#endif /* MATERIAL_DEFINITION_H */