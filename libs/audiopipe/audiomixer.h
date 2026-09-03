#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <math.h>

#define AP_MAX_CHANNELS 8

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

    /* HRTF state: resonance peak (biquad) – per ear */
    float        resonance_state[AP_MAX_CHANNELS * 2];

    /* Smoothed resonance gains (to avoid digital zipper artifacts) */
    float        smooth_res_gain_left;
    float        smooth_res_gain_right;

    /* HRTF state: low‑pass (one‑pole) – first pole per ear */
    float        hrtf_lp_state[AP_MAX_CHANNELS];

    /* HRTF state: low‑pass (one‑pole) – second pole per ear (12dB/oct) */
    float        hrtf_lp2_state[AP_MAX_CHANNELS];

    /* Smoothed HRTF parameters (to avoid digital zipper artifacts) */
    float        smooth_left_notch_freq;
    float        smooth_right_notch_freq;
    float        smooth_left_cutoff;
    float        smooth_right_cutoff;

    /* Delay buffer for ITD (Interaural Time Difference) */
    float        delay_buffer[2][128]; /* 2 channels, 128 samples max (~2.6ms @ 48kHz) */
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

    /* ---- 3‑Band Directional Exciters (6 total) ---- */
    float        pre_excit_state[3][AP_MAX_CHANNELS * 2];  /* Low(1500), Mid(7500), High(11000) */
    float        post_excit_state[3][AP_MAX_CHANNELS * 2]; /* Low, Mid, High */

    /* ================================================================
       GEOMETRY HOOKS (Dormant until filled by renderer)
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

#define AP_MAX_MIXER_VOICES 8

typedef struct {
    float pos_x, pos_y, pos_z;
    float vel_x, vel_y, vel_z;
    float forward_x, forward_y, forward_z;
    float up_x, up_y, up_z;
} listener_t;

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

typedef struct {
    int      count;
    voice_t *voices[AP_MAX_MIXER_VOICES];
    int      sample_rate;
    listener_t listener;
    reverb_t reverb;
} effect_mixer_ctx_t;

void effect_mixer(float *buffer, int frames, int out_channels, void *userdata);

/* ---- Geometry Data Setters ---- */
static inline void audio_voice_set_occlusion(voice_t *v, float occlusion) {
    if (v) v->occlusion = occlusion;
}
static inline void audio_voice_set_portal(voice_t *v, float x, float y, float z, float dampening) {
    if (v) {
        v->portal_active = 1;
        v->portal_pos_x = x;
        v->portal_pos_y = y;
        v->portal_pos_z = z;
        v->portal_dampening = dampening;
    }
}
static inline void audio_voice_clear_portal(voice_t *v) {
    if (v) v->portal_active = 0;
}
static inline void audio_voice_set_reflection(voice_t *v, float delay_sec, float gain) {
    if (v) {
        v->reflection_delay_sec = delay_sec;
        v->reflection_gain = gain;
    }
}
static inline void audio_reverb_set_room(reverb_t *rv, float decay, float damping) {
    if (rv) {
        rv->target_decay = decay;
        rv->target_damping = damping;
    }
}

#endif