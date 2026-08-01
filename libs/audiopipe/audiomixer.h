#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#define AP_MAX_CHANNELS 8

typedef struct {
    const float *samples;
    int          num_frames;
    int          channels;
    int          source_sample_rate;

    /* Audio clock: advances at exactly the sample rate */
    double       position;       /* now double precision */
    float        pitch;
    float        volume;

    int          active;
} voice_t;

#define AP_MAX_MIXER_VOICES 8
typedef struct {
    int      count;
    voice_t *voices[AP_MAX_MIXER_VOICES];
    int      sample_rate;        /* device sample rate */
} effect_mixer_ctx_t;

void effect_mixer(float *buffer, int frames, int out_channels, void *userdata);

#endif