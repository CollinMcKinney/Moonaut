#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <math.h>

#define AP_MAX_CHANNELS 8
#define AP_MAX_MIXER_VOICES 8

/* ------------------------------------------------------------------------
   HRTF STATE STRUCT – all directional processing state for one audio path.
   We embed two of these in each voice: one for the direct path, one for
   the portal path.
   ------------------------------------------------------------------------ */
typedef struct {
    /* Primary notch biquad per ear */
    float        hrtf_bq_state[AP_MAX_CHANNELS * 2];
    /* Secondary notch biquad per ear */
    float        secondary_bq_state[AP_MAX_CHANNELS * 2];
    /* Tertiary notch biquad per ear */
    float        tertiary_bq_state[AP_MAX_CHANNELS * 2];
    /* Resonance peak biquad per ear */
    float        resonance_state[AP_MAX_CHANNELS * 2];
    /* Smoothed resonance gains */
    float        smooth_res_gain_left;
    float        smooth_res_gain_right;
    /* Low‑pass one‑pole first pole per ear */
    float        hrtf_lp_state[AP_MAX_CHANNELS];
    /* Low‑pass one‑pole second pole per ear */
    float        hrtf_lp2_state[AP_MAX_CHANNELS];
    /* Smoothed HRTF parameters */
    float        smooth_left_notch_freq;
    float        smooth_right_notch_freq;
    float        smooth_left_cutoff;
    float        smooth_right_cutoff;
    /* Smoothed notch modulation parameters */
    float        smooth_side_factor;
    float        smooth_elev_norm;
    /* ITD delay buffer */
    float        delay_buffer[2][128];
    int          delay_write_pos;
    /* Frequency‑dependent ILD crossover states */
    float        crossover_lpf_state[AP_MAX_CHANNELS];
    /* Occlusion smoothing / low‑pass (used for portal dampening as well) */
    float        occlusion_smooth;
    float        occlusion_lp_state[AP_MAX_CHANNELS];
} hrtf_state_t;

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
    float        rolloff;

    float        smooth_x, smooth_y, smooth_z;

    /* ---- Artistic Effects ---- */
    float        pitch;
    float        volume;
    float        drive;
    float        sat_mix;
    float        eq_gain_db;
    float        eq_freq;
    float        eq_q;
    float        eq_state[AP_MAX_CHANNELS * 2]; /* Biquad state for EQ */

    /* ---- Directional Exciters (Pre-Low + Post-Mid + Post-High) ---- */
    float        pre_excit_state[2][AP_MAX_CHANNELS * 2];   /* only using index 0 (Low) */
    float        post_excit_state[2][AP_MAX_CHANNELS * 2];  /* index 0: Mid, index 1: High */

    /* ---- Occlusion (Wall Muffling) ---- */
    float        occlusion;              /* 0.0 = clear, 1.0 = fully blocked */

    /* ---- Portal (Redirect sound through a doorway) ---- */
    int          portal_active;
    float        portal_pos_x;
    float        portal_pos_y;
    float        portal_pos_z;
    float        portal_dampening;
    float        portal_buffer[256];     /* extra delay line if needed, currently unused */
    int          portal_write_pos;

    /* ---- Early Reflections (Slap Echo) ---- */
    float        reflection_delay_sec;
    float        reflection_gain;
    float        reflection_buffer[256];
    int          reflection_write_pos;

    int          releasing;
    float        release_gain;

    int          active;
    int          looping;

    /* ---- Two independent HRTF state instances ---- */
    hrtf_state_t hrtf_main;
    hrtf_state_t hrtf_portal;
} voice_t;

/* ------------------------------------------------------------------------
   LISTENER STRUCT
   ------------------------------------------------------------------------ */
typedef struct {
    float pos_x, pos_y, pos_z;
    float vel_x, vel_y, vel_z;
    float forward_x, forward_y, forward_z;
    float up_x, up_y, up_z;

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

    float avg_depth;
    float target_decay;
    float target_damping;
    float smooth_decay;
    float smooth_damping;
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
   PUBLIC API
   ------------------------------------------------------------------------ */
void effect_mixer(float *buffer, int frames, int out_channels, void *userdata);
void audio_set_room_geometry(float max_depth, float avg_depth, float variance);

static inline void audio_set_listener_room(reverb_t *rv, float decay, float damping) {
    if (rv) {
        rv->target_decay = decay;
        rv->target_damping = damping;
    }
}

#endif /* AUDIO_MIXER_H */