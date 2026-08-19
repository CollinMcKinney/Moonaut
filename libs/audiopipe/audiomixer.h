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
    float        rolloff;
    float        max_distance;

    float        smooth_x, smooth_y, smooth_z;

    /* HRTF state: low‑pass for distance (one‑pole) */
    float        hrtf_lp_state[AP_MAX_CHANNELS];

    /* HRTF state: biquad for pinna notch (2 states per channel) */
    float        hrtf_bq_state[AP_MAX_CHANNELS * 2];

    float        pitch;
    float        volume;
    float        drive;

    int          releasing;
    float        release_gain;

    int          active;
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
} reverb_t;

typedef struct {
    int      count;
    voice_t *voices[AP_MAX_MIXER_VOICES];
    int      sample_rate;
    listener_t listener;
    reverb_t reverb;
} effect_mixer_ctx_t;

void effect_mixer(float *buffer, int frames, int out_channels, void *userdata);

#endif