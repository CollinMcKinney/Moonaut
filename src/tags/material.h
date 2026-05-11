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
    bool         skip_fog;            /* 0/false -> apply global fog, 1/true -> skip (for objects that should stay bright). */
    
    /* Post‑lighting colour treatment */

    real         iridescence_strength;  /* View‑angle rainbow shift (0 = off, typical 0.1‑1.0). */
    
    /* Stylizing effects */
    real         glitch_intensity;      /* Noise overlay strength (0 = off, typical 0.05‑0.2). */
    real         fringe_intensity;      /* Chromatic aberration strength (0 = off, typical 0.01‑0.05). */
    i32          posterize_levels;      /* ≥2 quantises final colour into that many levels. 0/1 = off. */

    bool         double_sided;          /* 0/false -> cull backface; !0/true -> render backface. */

    real         roughness;             /* Roughness intensity */
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
    FIELD_BOOL("skip_fog"),
    FIELD_REAL("iridescence_strength"),
    FIELD_REAL("glitch_intensity"),
    FIELD_REAL("fringe_intensity"),
    FIELD_I32("posterize_levels"),
    FIELD_BOOL("double_sided"),
    FIELD_REAL("roughness"),
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
    /*.color               =*/ {0.5f, 0.5f, 0.5f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_GOURAUD = {
    /*.mode                =*/ SHADE_GOURAUD,
    /*.color               =*/ {0.5f, 0.5f, 0.5f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_PHONG = {
    /*.mode                =*/ SHADE_PHONG,
    /*.color               =*/ {0.5f, 0.5f, 0.5f},
    /*.ambient_light_factor=*/ 1.0f,
    /*.alpha               =*/ 1.0f,
    /*.saturation          =*/ 1.0f,
    /*.tint                =*/ {1.0f, 1.0f, 1.0f}
};

const struct material_definition DEFAULT_MATERIAL_WATER = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.10f, 0.35f, 0.65f},  /* brighter tropical blue */
    /*.ambient_light_factor     =*/ 0.80f,  /* shadowed water still looks bright */
    /*.alpha                    =*/ 0.70f,  /* mostly opaque but still translucent */
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0.20f,  /* gentle edge softening */
    /*.oren_nayar_sigma         =*/ 0.30f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.40f,  /* subtle ripples */
    /*.bump_frequency           =*/ 32.0f,
    /*.bump_speed               =*/ 2.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.0f, 0.20f, 0.0f},  /* brighter subsurface glow */
    /*.rim_color                =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_exponent             =*/ 0.00f,
    /*.fresnel_color            =*/ {0.55f, 0.85f, 1.00f}, /* sky reflection */
    /*.fresnel_exponent         =*/ 3.00f,  /* slightly lower so it doesn't darken too much */
    /*.specular_exponent        =*/ 384.0f, /* still sharp sun glitter */
    /*.specular_color           =*/ {1.00f, 1.00f, 1.00f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.00f, 0.00f}, /* no artificial glow */
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.05f, /* barely-there oil-slick rainbow */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.01f  /* subtle chromatic aberration */
};

const struct material_definition DEFAULT_MATERIAL_GRASS = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.0f, 0.24f, 0.08f},     /* bright grass green */
    /*.ambient_light_factor     =*/ 1.0f,
    /*.alpha                    =*/ 1.0f,
    /*.saturation               =*/ 1.2f,                   /* slightly vivid for natural look */
    /*.tint                     =*/ {1,1,1},
    /*.cel_bands                =*/ 4,                      /* more toon bands for blade definition */
    /*.diffuse_wrap             =*/ 1,
    /*.oren_nayar_sigma         =*/ 0.70f,                   /* rough fibrous surface */
    /*.minnaert_k               =*/ 0.0f,
    /*.bump_amplitude           =*/ 0.15f,                  /* pronounced blade texture */
    /*.bump_frequency           =*/ 16.0f,
    /*.bump_speed               =*/ 1.0f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.1f, 0.3f, 0.05f},   /* subtle subsurface scatter */
    /*.rim_color                =*/ {0.4f, 0.8f, 0.15f},   /* backlit blade edges */
    /*.rim_exponent             =*/ 2.0f,
    /*.fresnel_color            =*/ {0.5f, 0.9f, 0.6f},    /* dew reflection at grazing */
    /*.fresnel_exponent         =*/ 3.0f,
    /*.specular_exponent        =*/ 4.0f,                  /* sharper blade highlights */
    /*.specular_color           =*/ {0.3f, 0.4f, 0.15f},    /* bright green specular */
    /*.specular_threshold       =*/ 0.0f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.0f,
    /*.emissive_pulse_phase     =*/ 0.0f,
    /*.emissive_pulse_amplitude =*/ 0.0f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.0f,
    /*.strobe_phase             =*/ 0.0f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.05f,                  /* slight rainbow for moisture */
    /*.glitch_intensity         =*/ 0.02f,
    /*.fringe_intensity         =*/ 0.01f,
};

const struct material_definition DEFAULT_MATERIAL_CLOTH = {
    /*.mode                     =*/ SHADE_FLAT,     /* or GOURAUD, depending on your model */
    /*.color                    =*/ {0.6f, 0.2f, 0.3f},
    /*.ambient_light_factor     =*/ 1.0f,
    /*.alpha                    =*/ 1.0f,
    /*.saturation               =*/ 1.0f,
    /*.tint                     =*/ {1,1,1},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,             /* softer falloff */
    /*.oren_nayar_sigma         =*/ 0.50f,          /* fabric roughness */
    /*.minnaert_k               =*/ 0.0f,
    /*.bump_amplitude           =*/ 0.01f,
    /*.bump_frequency           =*/ 30.0f,
    /*.bump_speed               =*/ 0.0f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0,0,0},
    /*.rim_color                =*/ {0,0,0},
    /*.rim_exponent             =*/ 0.0f,
    /*.fresnel_color            =*/ {0,0,0},
    /*.fresnel_exponent         =*/ 0.0f,
    /*.specular_exponent        =*/ 2.0f,
    /*.specular_color           =*/ {0,0,0},
    /*.specular_threshold       =*/ 0.0f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.0f,
    /*.emissive_pulse_phase     =*/ 0.0f,
    /*.emissive_pulse_amplitude =*/ 0.0f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.0f,
    /*.strobe_phase             =*/ 0.0f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.0f,
    /*.glitch_intensity         =*/ 0.0f,
    /*.fringe_intensity         =*/ 0.0f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.20f
};

const struct material_definition DEFAULT_MATERIAL_WOOD = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.55f, 0.32f, 0.12f},   /* warm brown */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,                       /* no cel shading */
    /*.diffuse_wrap             =*/ 1,                       /* soft falloff */
    /*.oren_nayar_sigma         =*/ 0.40f,                   /* fibrous roughness */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,                   /* grain effect */
    /*.bump_frequency           =*/ 48.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_color                =*/ {0.15f, 0.08f, 0.03f},   /* subtle backlit edge */
    /*.rim_exponent             =*/ 2.50f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 64.00f,                  /* light polish */
    /*.specular_color           =*/ {0.50f, 0.35f, 0.20f},   /* brighter warm highlight */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.15f
};

const struct material_definition DEFAULT_MATERIAL_METAL = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.5f, 0.65f, 0.70f},   /* dark base, color from specular */
    /*.ambient_light_factor     =*/ 0.5f,                   /* low ambient for metallic darkness */
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.2f,
    /*.tint                     =*/ {1.0f, 1.8f, 2.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,                       /* sharp metallic */
    /*.oren_nayar_sigma         =*/ 0.10f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,                   /* subtle brushed grain */
    /*.bump_frequency           =*/ 512.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0,0,0},
    /*.rim_color                =*/ {1.00f, 1.00f, 1.00f},   /* subtle metallic rim */
    /*.rim_exponent             =*/ 3.00f,
    /*.fresnel_color            =*/ {1.0f, 1.0f, 1.0f},   /* bright reflection at grazing */
    /*.fresnel_exponent         =*/ 3.0f,                   /* stronger fresnel */
    /*.specular_exponent        =*/ 32.0f,                  /* very tight highlight */
    /*.specular_color           =*/ {1.00f, 1.00f, 1.00f},   /* pure white specular */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.0,0.0,0.0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.30f,                   /* slight rainbow for polished metal */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_GLASS = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.85f, 0.95f, 1.00f},   /* pale ice blue */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 0.3f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.00f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.03f,                   /* subtle crystal undulation */
    /*.bump_frequency           =*/ 80.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.05f, 0.10f, 0.20f},   /* faint internal blue */
    /*.rim_color                =*/ {0.30f, 0.50f, 0.80f},   /* blue rim (additive) – matches base hue */
    /*.rim_exponent             =*/ 3.00f,
    /*.fresnel_color            =*/ {0.70f, 0.85f, 1.00f},   /* white-blue reflection */
    /*.fresnel_exponent         =*/ 5.00f,
    /*.specular_exponent        =*/ 512.0f,
    /*.specular_color           =*/ {1.00f, 1.00f, 1.00f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.02f, 0.05f},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.12f,                   /* subtle prismatic sparkle */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.02f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ true
};

const struct material_definition DEFAULT_MATERIAL_SKIN = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.87f, 0.8f, 0.64f},   /* warm peach */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,                       /* soft falloff */
    /*.oren_nayar_sigma         =*/ 0.40f,                   /* skin-like roughness */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.02f,                   /* fine pores */
    /*.bump_frequency           =*/ 128.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.25f, 0.15f, 0.22f},   /* cool shadow (purplish) – kept for artistic SSS */
    /*.gooch_warm               =*/ {1.00f, 0.80f, 0.65f},   /* warm lit */
    /*.back_glow_color          =*/ {0.15f, 0.05f, 0.05f},   /* forward‑scatter red glow */
    /*.rim_color                =*/ {0.45f, 0.20f, 0.15f},   /* warm rim */
    /*.rim_exponent             =*/ 2.00f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 16.0f,                   /* soft broad highlight */
    /*.specular_color           =*/ {0.50f, 0.40f, 0.35f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_RUBBER = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.10f, 0.10f, 0.10f},   /* very dark grey */
    /*.ambient_light_factor     =*/ 0.50f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 0.80f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,
    /*.oren_nayar_sigma         =*/ 0.90f,                   /* fully rough */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.10f,                   /* heavy grain */
    /*.bump_frequency           =*/ 32.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0,0,0},
    /*.rim_color                =*/ {0.08f, 0.08f, 0.08f},   /* neutral grey rim */
    /*.rim_exponent             =*/ 3.00f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 4.00f,                   /* slightly sharper dull shine */
    /*.specular_color           =*/ {0.25f, 0.25f, 0.25f},   /* brighter grey highlight */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_ICE = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.65f, 0.85f, 0.95f},   /* pale cyan */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 0.85f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.00f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.08f,                   /* frost crystals */
    /*.bump_frequency           =*/ 64.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.10f, 0.25f, 0.40f},   /* deeper blue internal glow */
    /*.rim_color                =*/ {0.70f, 0.85f, 1.00f},   /* bright white‑blue rim */
    /*.rim_exponent             =*/ 4.00f,
    /*.fresnel_color            =*/ {0.90f, 0.95f, 1.00f},   /* strong reflection */
    /*.fresnel_exponent         =*/ 4.50f,
    /*.specular_exponent        =*/ 256.0f,
    /*.specular_color           =*/ {1.00f, 1.00f, 1.00f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.02f, 0.05f, 0.12f},   /* cold self‑light */
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.10f,                   /* prismatic sparkle */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.03f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_STONE = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.48f, 0.43f, 0.38f},   /* neutral grey‑brown */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 0.90f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,
    /*.oren_nayar_sigma         =*/ 1.00f,                   /* rough stone */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.10f,                   /* coarse surface */
    /*.bump_frequency           =*/ 24.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0,0,0},
    /*.rim_color                =*/ {0.12f, 0.11f, 0.09f},   /* very subtle grey‑brown rim – avoids green shift */
    /*.rim_exponent             =*/ 2.00f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 16.00f,                  /* slightly sharper highlight for rough surface */
    /*.specular_color           =*/ {0.40f, 0.38f, 0.35f},   /* brighter grey highlight */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.20f
};

const struct material_definition DEFAULT_MATERIAL_LAVA = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.15f, 0.05f, 0.00f},   /* darker black-brown crust */
    /*.ambient_light_factor     =*/ 0.50f,                   /* darker base for deeper shadows */
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.5f,                   /* moderated reds for brown tones */
    /*.tint                     =*/ {2.00f, 2.00f, 0.00f},
    /*.cel_bands                =*/ 0,                       /* more bands for heat contrast */
    /*.diffuse_wrap             =*/ 1,
    /*.oren_nayar_sigma         =*/ 0.60f,                   /* rougher surface */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.40f,                   /* pronounced bump for texture */
    /*.bump_frequency           =*/ 32.0f,
    /*.bump_speed               =*/ 2.00f,                   /* slightly faster flow */
    /*.gooch_cool               =*/ {0.13f, 0.00f, 0.00f},   /* deeper red shadows */
    /*.gooch_warm               =*/ {1.00f, 1.00f, 0.00f},   /* more orange, less yellow */
    /*.back_glow_color          =*/ {0.50f, 0.50f, 0.00f},   /* redder internal glow */
    /*.rim_color                =*/ {0.80f, 0.30f, 0.00f},   /* redder rim */
    /*.rim_exponent             =*/ 1.50f,                   /* softer rim for Gouraud */
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 0.0f,                   /* subtle shine on crust */
    /*.specular_color           =*/ {0.40f, 0.40f, 0.00f},   /* darker brown highlight */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.50f, 0.33f, 0.00f},   /* darker red glow */
    /*.emissive_pulse_frequency =*/ 1.00f,              /* steady pulse */
    /*.emissive_pulse_phase     =*/ 1.57f,              /* phase shift */
    /*.emissive_pulse_amplitude =*/ 0.25f,              /* subtle variation */
    /*.strobe_color             =*/ {0.10f, 0.04f, 0.00f},   /* darker red flashes */
    /*.strobe_frequency         =*/ 0.50f,                  /* occasional bursts */
    /*.strobe_phase             =*/ 2.10f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.04f,                   /* slight instability */
    /*.fringe_intensity         =*/ 0.50f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_TOON = {
    /*.mode                     =*/ SHADE_FLAT,                  /* flat shading for toon look */
    /*.color                    =*/ {0.90f, 0.70f, 0.40f},   /* cartoon orange */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.50f,                    /* more vibrant */
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 5,                        /* lit or shadow */
    /*.diffuse_wrap             =*/ 0,                        /* crisp transitions */
    /*.oren_nayar_sigma         =*/ 0.00f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.10f, 0.15f, 0.40f},   /* darker blue shadow */
    /*.gooch_warm               =*/ {1.00f, 0.90f, 0.60f},   /* brighter warm lit */
    /*.back_glow_color          =*/ {0,0,0},
    /*.rim_color                =*/ {0.00f, 0.00f, 0.00f},   /* black outline */
    /*.rim_exponent             =*/ 8.00f,                    /* very sharp outline */
    /*.fresnel_color            =*/ {0,0,0},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 100.0f,                   /* sharp highlight */
    /*.specular_color           =*/ {1.00f, 1.00f, 0.80f},   /* bright white-yellow */
    /*.specular_threshold       =*/ 0.90f,                    /* hard specular cut */
    /*.emissive_color           =*/ {0,0,0},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 8                         /* more quantized */
};

const struct material_definition DEFAULT_MATERIAL_HOLOGRAM = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.20f, 0.60f, 0.80f},   /* cyan/blue base */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 0.55f,
    /*.saturation               =*/ 1.10f,
    /*.tint                     =*/ {1.00f, 1.50f, 2.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.00f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,0,0},
    /*.gooch_warm               =*/ {0,0,0},
    /*.back_glow_color          =*/ {0.15f, 0.45f, 0.65f},   /* internal glow */
    /*.rim_color                =*/ {0.40f, 0.80f, 1.00f},   /* bright blue rim */
    /*.rim_exponent             =*/ 64.00f,
    /*.fresnel_color            =*/ {0.70f, 0.90f, 1.00f},   /* teal‑white reflection */
    /*.fresnel_exponent         =*/ 4.00f,
    /*.specular_exponent        =*/ 0.0f,
    /*.specular_color           =*/ {0.60f, 0.85f, 1.00f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.05f, 0.15f, 0.25f},   /* base tech glow */
    /*.emissive_pulse_frequency =*/ 0.80f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.15f,
    /*.strobe_color             =*/ {0.10f, 0.35f, 0.55f},   /* subtle flicker */
    /*.strobe_frequency         =*/ 1.0f,
    /*.strobe_phase             =*/ 0.5f,
    /*.skip_fog                 =*/ true,                       /* stay bright in fog */
    /*.iridescence_strength     =*/ 0.30f,                   /* rainbow data shimmer */
    /*.glitch_intensity         =*/ 0.50f,
    /*.fringe_intensity         =*/ 0.25f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ true
};

const struct material_definition DEFAULT_MATERIAL_IRIDESCENT = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {1.00f, 1.00f, 1.00f},   /* pure white base for full rainbow shift */
    /*.ambient_light_factor     =*/ 1.00f,                   /* full ambient to enhance colors */
    /*.alpha                    =*/ 0.90f,                   /* near opaque for visibility */
    /*.saturation               =*/ 2.50f,                   /* max saturation for vibrant rainbows */
    /*.tint                     =*/ {2.50f, 1.50f, 1.00f},   /* red-green bias for rainbow spectrum */
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.10f,                   /* very smooth surface */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,                   /* minimal bump for clean effect */
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.50f, 0.00f, 0.50f},   /* purple shadows for rainbow depth */
    /*.gooch_warm               =*/ {1.00f, 0.80f, 0.20f},   /* orange highlights */
    /*.back_glow_color          =*/ {0.80f, 0.60f, 1.00f},   /* magenta internal glow */
    /*.rim_color                =*/ {1.00f, 0.50f, 0.00f},   /* orange rim */
    /*.rim_exponent             =*/ 2.00f,
    /*.fresnel_color            =*/ {0.50f, 0.50f, 0.50f},   /* neutral fresnel for angle dependence */
    /*.fresnel_exponent         =*/ 2.00f,
    /*.specular_exponent        =*/ 16.0f,                   /* moderate specular for GOURAUD */
    /*.specular_color           =*/ {1.00f, 1.00f, 0.80f},   /* warm white highlights */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.30f, 0.20f, 0.40f},   /* purple emissive for rainbow base */
    /*.emissive_pulse_frequency =*/ 0.50f,              /* slow pulse for dynamic effect */
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.20f,
    /*.strobe_color             =*/ {0.50f, 0.00f, 0.50f},   /* magenta flashes */
    /*.strobe_frequency         =*/ 1.00f,                  /* occasional bursts */
    /*.strobe_phase             =*/ 1.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.90f,                   /* max rainbow shift */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.20f,                   /* chromatic aberration for rainbow spread */
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_PLASTIC = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.20f, 0.50f, 0.80f},   /* blue plastic */
    /*.ambient_light_factor     =*/ 0.70f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.20f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,                       /* soft falloff */
    /*.oren_nayar_sigma         =*/ 0.00f,                   /* smooth surface */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.01f,                   /* minimal texture */
    /*.bump_frequency           =*/ 128.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.10f, 0.25f, 0.40f},   /* blue shadows */
    /*.gooch_warm               =*/ {0.50f, 0.80f, 1.00f},   /* bright blue highlights */
    /*.back_glow_color          =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_color                =*/ {0.40f, 0.70f, 0.90f},   /* blue rim */
    /*.rim_exponent             =*/ 3.00f,
    /*.fresnel_color            =*/ {0.60f, 0.80f, 0.90f},   /* blue reflection */
    /*.fresnel_exponent         =*/ 4.00f,
    /*.specular_exponent        =*/ 32.0f,                   /* plastic shine */
    /*.specular_color           =*/ {0.80f, 0.90f, 1.00f},   /* blue highlights */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.02f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_BRICK = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.50f, 0.19f, 0.10f},
    /*.ambient_light_factor     =*/ 0.60f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.80f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.35f, 0.12f, 0.06f},
    /*.gooch_warm               =*/ {0.75f, 0.26f, 0.14f},
    /*.back_glow_color          =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_color                =*/ {0.62f, 0.21f, 0.11f},
    /*.rim_exponent             =*/ 0.50f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 0.00f,
    /*.specular_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.25f
};

const struct material_definition DEFAULT_MATERIAL_LEATHER = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.30f, 0.15f, 0.09f},   /* darker brown */
    /*.ambient_light_factor     =*/ 0.80f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,                       /* soft falloff */
    /*.oren_nayar_sigma         =*/ 0.50f,                   /* flexible roughness */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.15f,                   /* grain texture */
    /*.bump_frequency           =*/ 32.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.18f, 0.09f, 0.05f},   /* cool shadows */
    /*.gooch_warm               =*/ {0.42f, 0.21f, 0.13f},   /* warm highlights */
    /*.back_glow_color          =*/ {0.08f, 0.04f, 0.02f},   /* slight subsurface */
    /*.rim_color                =*/ {0.36f, 0.18f, 0.11f},   /* warm rim */
    /*.rim_exponent             =*/ 1.50f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 4.0f,                   /* moderate shine */
    /*.specular_color           =*/ {0.45f, 0.23f, 0.14f},   /* highlights */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.05f
};

const struct material_definition DEFAULT_MATERIAL_GOLD = {
    /*.mode                     =*/ SHADE_FLAT,           /* keep GOURAUD for efficiency */
    /*.color                    =*/ {0.80f, 0.60f, 0.20f},   /* gold base */
    /*.ambient_light_factor     =*/ 0.50f,                   /* low ambient for darkness */
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.50f,                   /* rich color */
    /*.tint                     =*/ {1.50f, 1.20f, 0.80f},   /* warm gold tint */
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.10f,                   /* smooth metal */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.05f,                   /* subtle polish */
    /*.bump_frequency           =*/ 512.0f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.40f, 0.30f, 0.10f},   /* dark gold shadows */
    /*.gooch_warm               =*/ {1.00f, 0.80f, 0.30f},   /* bright gold highlights */
    /*.back_glow_color          =*/ {0.50f, 0.40f, 0.10f},   /* internal warmth */
    /*.rim_color                =*/ {1.00f, 0.70f, 0.20f},   /* gold rim */
    /*.rim_exponent             =*/ 3.00f,
    /*.fresnel_color            =*/ {0.90f, 0.80f, 0.50f},   /* reflective gold */
    /*.fresnel_exponent         =*/ 4.00f,
    /*.specular_exponent        =*/ 32.0f,                   /* sharp metal shine */
    /*.specular_color           =*/ {1.00f, 0.90f, 0.60f},   /* bright gold specular */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.20f, 0.15f, 0.05f},   /* subtle glow */
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.12f,                   /* slight luster shift */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0
};

const struct material_definition DEFAULT_MATERIAL_SNOW = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.95f, 0.95f, 1.00f},   /* white with blue tint */
    /*.ambient_light_factor     =*/ 1.00f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 0.90f,                   /* desaturated */
    /*.tint                     =*/ {1.00f, 1.00f, 1.20f},   /* cool blue tint */
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,                       /* soft snow */
    /*.oren_nayar_sigma         =*/ 0.60f,                   /* powdery roughness */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,                   /* uneven surface */
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.70f, 0.70f, 0.90f},   /* cool shadows */
    /*.gooch_warm               =*/ {1.00f, 1.00f, 1.00f},   /* white highlights */
    /*.back_glow_color          =*/ {0.20f, 0.20f, 0.40f},   /* blue subsurface */
    /*.rim_color                =*/ {0.90f, 0.90f, 1.00f},   /* frosty rim */
    /*.rim_exponent             =*/ 2.00f,
    /*.fresnel_color            =*/ {0.80f, 0.80f, 1.00f},   /* ice reflection */
    /*.fresnel_exponent         =*/ 3.00f,
    /*.specular_exponent        =*/ 64.0f,                   /* sparkle */
    /*.specular_color           =*/ {1.00f, 1.00f, 1.00f},   /* white snowflakes */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.05f, 0.05f, 0.10f},   /* faint blue glow */
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.10f,                   /* subtle shift */
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.05f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.05f
};

const struct material_definition DEFAULT_MATERIAL_DIRT = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.35f, 0.20f, 0.10f},   /* deep dark earthy brown */
    /*.ambient_light_factor     =*/ 0.45f,
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 2.50f,                   /* less muted for richer brown */
    /*.tint                     =*/ {1.00f, 1.00f, 1.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 1,
    /*.oren_nayar_sigma         =*/ 0.0f,                   /* rougher */
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,                   /* more pronounced texture */
    /*.bump_frequency           =*/ 0.00f,                   /* grainier */
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0.15f, 0.12f, 0.08f},   /* darker earth */
    /*.gooch_warm               =*/ {0.40f, 0.35f, 0.30f},   /* muted sunlit dirt */
    /*.back_glow_color          =*/ {0.00f, 0.00f, 0.00f},
    /*.rim_color                =*/ {0.20f, 0.18f, 0.12f},   /* darker earthy rim */
    /*.rim_exponent             =*/ 1.00f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 0.0f,                    /* no shine */
    /*.specular_color           =*/ {0.00f, 0.00f, 0.00f},   /* no highlights */
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 0.00f, 0.00f},
    /*.emissive_pulse_frequency =*/ 0.00f,
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.00f,
    /*.strobe_color             =*/ {0,0,0},
    /*.strobe_frequency         =*/ 0.00f,
    /*.strobe_phase             =*/ 0.00f,
    /*.skip_fog                 =*/ false,
    /*.iridescence_strength     =*/ 0.00f,
    /*.glitch_intensity         =*/ 0.00f,
    /*.fringe_intensity         =*/ 0.00f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ false,
    /*.roughness                =*/ 0.25f
};

const struct material_definition DEFAULT_MATERIAL_NEON = {
    /*.mode                     =*/ SHADE_FLAT,
    /*.color                    =*/ {0.00f, 1.00f, 1.00f},   /* pure emissive */
    /*.ambient_light_factor     =*/ 0.00f,                   /* allow emissive to show TODO: get emissive working with 0.00f alpha.*/
    /*.alpha                    =*/ 1.00f,
    /*.saturation               =*/ 1.00f,
    /*.tint                     =*/ {0.00f, 2.00f, 2.00f},
    /*.cel_bands                =*/ 0,
    /*.diffuse_wrap             =*/ 0,
    /*.oren_nayar_sigma         =*/ 0.00f,
    /*.minnaert_k               =*/ 0.00f,
    /*.bump_amplitude           =*/ 0.00f,
    /*.bump_frequency           =*/ 0.00f,
    /*.bump_speed               =*/ 0.00f,
    /*.gooch_cool               =*/ {0,1,1},
    /*.gooch_warm               =*/ {0,1,1},
    /*.back_glow_color          =*/ {0.00f, 1.00f, 1.00f},
    /*.rim_color                =*/ {0.00f, 1.00f, 1.00f},
    /*.rim_exponent             =*/ 16.00f,
    /*.fresnel_color            =*/ {0.00f, 0.00f, 0.00f},
    /*.fresnel_exponent         =*/ 0.00f,
    /*.specular_exponent        =*/ 0.00f,
    /*.specular_color           =*/ {0,0,0},
    /*.specular_threshold       =*/ 0.00f,
    /*.emissive_color           =*/ {0.00f, 4.00f, 4.00f},   /* bright blue glow */
    /*.emissive_pulse_frequency =*/ 4.00f,              /* moderate pulse */
    /*.emissive_pulse_phase     =*/ 0.00f,
    /*.emissive_pulse_amplitude =*/ 0.50f,              /* half range pulse */
    /*.strobe_color             =*/ {0.00f, 0.00f, 0.00f},   /* bright blue flashes */
    /*.strobe_frequency         =*/ 8.00f,
    /*.strobe_phase             =*/ 0.25f,
    /*.skip_fog                 =*/ true,                       /* glow through fog */
    /*.iridescence_strength     =*/ 0.05f,
    /*.glitch_intensity         =*/ 0.2f,
    /*.fringe_intensity         =*/ 0.1f,
    /*.posterize_levels         =*/ 0,
    /*.double_sided             =*/ true
};

#ifdef __cplusplus
}
#endif
#endif /* MATERIAL_DEFINITION_H */
