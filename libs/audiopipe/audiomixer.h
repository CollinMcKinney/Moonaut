#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <math.h>

#define AP_MAX_CHANNELS 8
#define AP_MAX_MIXER_VOICES 8

/* ------------------------------------------------------------------------
   VOICE STRUCT (Per-Sound Instance)
   ------------------------------------------------------------------------ */
typedef struct {
    const float *samples;
    int          num_frames;
    int          channels;
    int          source_sample_rate;

    double       position;

    /* 3D Audio */
    float        pos_x, pos_y, pos_z;
    float        vel_x, vel_y, vel_z;
    float        rolloff;          /* 1.0 = 3dB/doubling (perceptually accurate); 0.0 = no roll-off. */

    float        smooth_x, smooth_y, smooth_z;

    /* HRTF state: primary notch (biquad) – per ear */
    float        hrtf_bq_state[AP_MAX_CHANNELS * 2];

    /* HRTF state: secondary notch (biquad) – per ear */
    float        secondary_bq_state[AP_MAX_CHANNELS * 2];

    /* HRTF state: tertiary notch (biquad) – per ear */
    float        tertiary_bq_state[AP_MAX_CHANNELS * 2];

    /* HRTF state: resonance peak (biquad) – per ear */
    float        resonance_state[AP_MAX_CHANNELS * 2];

    /* Smoothed resonance gains */
    float        smooth_res_gain_left;
    float        smooth_res_gain_right;

    /* HRTF state: low‑pass (one‑pole) – first pole per ear */
    float        hrtf_lp_state[AP_MAX_CHANNELS];

    /* HRTF state: low‑pass (one‑pole) – second pole per ear (12dB/oct) */
    float        hrtf_lp2_state[AP_MAX_CHANNELS];

    /* Smoothed HRTF parameters */
    float        smooth_left_notch_freq;
    float        smooth_right_notch_freq;
    float        smooth_left_cutoff;
    float        smooth_right_cutoff;

    /* Smoothed notch modulation parameters */
    float        smooth_side_factor;
    float        smooth_elev_norm;

    /* Delay buffer for ITD (Interaural Time Difference) */
    float        delay_buffer[2][128];
    int          delay_write_pos;

    /* ---- Artistic Effects ---- */
    float        pitch;
    float        volume;
    float        drive;             /* Tube saturation drive (0.0 to 1.0) */
    float        sat_mix;           /* Saturation blend (0.0 dry, 1.0 fully saturated) */
    float        eq_gain_db;        /* Artistic EQ boost/cut (e.g., +6.0) */
    float        eq_freq;           /* EQ center frequency (e.g., 3000.0f) */
    float        eq_q;              /* EQ bandwidth (e.g., 0.8) */
    float        eq_state[AP_MAX_CHANNELS * 2]; /* Biquad state for EQ */

    /* ---- Directional Exciters (Pre-Low + Post-Mid + Post-High) ---- */
    float        pre_excit_state[2][AP_MAX_CHANNELS * 2];   /* only using index 0 (Low) */
    float        post_excit_state[2][AP_MAX_CHANNELS * 2];  /* index 0: Mid, index 1: High */

    /* ---- Frequency-Dependent ILD crossover states ---- */
    float        crossover_lpf_state[AP_MAX_CHANNELS];      /* one‑pole low‑pass for left/right (stereo) */

    /* ================================================================
       GEOMETRY HOOKS (Filled by renderer's compute shader)
       ================================================================ */

    /* ---- Occlusion (Wall Muffling) ---- */
    float        occlusion;              /* 0.0 = clear, 1.0 = fully blocked */
    float        occlusion_lp_state[AP_MAX_CHANNELS]; /* Filter state for occlusion */

    /* ---- Portal (Redirect sound through a doorway) ---- */
    int          portal_active;          /* 1 if sound is routed through a portal */
    float        portal_pos_x;           /* World X position of the doorway */
    float        portal_pos_y;           /* World Y position of the doorway */
    float        portal_pos_z;           /* World Z position of the doorway */
    float        portal_dampening;       /* Extra muffling factor (0.0..1.0) from portal */

    /* ---- Early Reflections (Slap Echo) ---- */
    float        reflection_delay_sec;   /* Delay in seconds (e.g., 0.02) */
    float        reflection_gain;        /* Volume of the reflection (0.0 to 1.0) */
    float        reflection_buffer[256]; /* Circular buffer for delay */
    int          reflection_write_pos;   /* Write head for the buffer */

    int          releasing;
    float        release_gain;

    int          active;
    int          looping;        /* 1 = loop, 0 = play once */
} voice_t;

/* ------------------------------------------------------------------------
   LISTENER STRUCT
   ------------------------------------------------------------------------ */
typedef struct {
    float pos_x, pos_y, pos_z;
    float vel_x, vel_y, vel_z;
    float forward_x, forward_y, forward_z;
    float up_x, up_y, up_z;            /* Camera's up vector (head-relative) */

    /* ---- Internal smoothed forward (updated inside mixer) ---- */
    float smooth_forward_x;
    float smooth_forward_y;
    float smooth_forward_z;
    int   smooth_forward_initialized;
} listener_t;

/* ------------------------------------------------------------------------
   REVERB STRUCT
   ------------------------------------------------------------------------ */
typedef struct {
    float wet;
    float decay;
    float damping;
    int   sample_rate;

    float *comb_buf[8];
    int    comb_len[8];
    int    comb_pos[8];
    float  comb_gain[8];
    float  comb_damping[8];

    float *ap_buf[4];
    int    ap_len[4];
    int    ap_pos[4];
    float  ap_gain;

    int initialized;

    /* ---- Geometry-Driven Reverb Targets (for smooth interpolation) ---- */
    float target_decay;      /* Desired reverb time (e.g., 0.5 for closet, 4.0 for hall) */
    float target_damping;    /* Desired high-frequency damping (0.0 bright, 1.0 dark) */
    float smooth_decay;      /* Current smoothed decay (interpolates toward target) */
    float smooth_damping;    /* Current smoothed damping */
} reverb_t;

/* ------------------------------------------------------------------------
   MIXER CONTEXT
   ------------------------------------------------------------------------ */
typedef struct {
    int      count;
    voice_t *voices[AP_MAX_MIXER_VOICES];
    int      sample_rate;
    listener_t listener;
    reverb_t reverb;
} effect_mixer_ctx_t;

/* ------------------------------------------------------------------------
   GEOMETRY DATA STRUCT (Unified API – filled by compute shader)
   ------------------------------------------------------------------------ */
typedef struct audio_voice_geometry {
    /* Occlusion */
    float occlusion;              /* 0.0 = clear, 1.0 = fully blocked */

    /* Portal (Doorway / Aperture) */
    int   portal_active;          /* 1 if sound is routed through a portal */
    float portal_pos_x;           /* World X position of the doorway */
    float portal_pos_y;           /* World Y position of the doorway */
    float portal_pos_z;           /* World Z position of the doorway */
    float portal_dampening;       /* Extra muffling factor (0.0..1.0) */

    /* Early Reflections (Slap Echo) */
    float reflection_delay_sec;   /* Delay in seconds (e.g., 0.015) */
    float reflection_gain;        /* Volume of the reflection (0.0 to 1.0) */
} audio_voice_geometry_t;

/* ------------------------------------------------------------------------
   PUBLIC API
   ------------------------------------------------------------------------ */
void effect_mixer(float *buffer, int frames, int out_channels, void *userdata);

/* ---- Geometry Setters (Call from CPU after compute shader readback) ---- */
static inline void audio_voice_set_geometry(voice_t *v, const audio_voice_geometry_t *geo) {
    if (!v || !geo) return;
    v->occlusion = geo->occlusion;
    v->portal_active = geo->portal_active;
    v->portal_pos_x  = geo->portal_pos_x;
    v->portal_pos_y  = geo->portal_pos_y;
    v->portal_pos_z  = geo->portal_pos_z;
    v->portal_dampening = geo->portal_dampening;
    v->reflection_delay_sec = geo->reflection_delay_sec;
    v->reflection_gain = geo->reflection_gain;
}

static inline void audio_set_listener_room(reverb_t *rv, float decay, float damping) {
    if (rv) {
        rv->target_decay = decay;
        rv->target_damping = damping;
    }
}

#endif /* AUDIO_MIXER_H */