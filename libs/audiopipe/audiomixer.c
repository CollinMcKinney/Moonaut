/*
  audiomixer.c – Pure mixer with double‑precision sample clock.
  The position advances by exactly source_rate / device_rate per sample.
*/

#include "audiomixer.h"
#include <string.h>

static void downmix_frame(const float *src, int src_channels,
                          float *dst, int dst_channels)
{
    int c;
    if (src_channels == dst_channels) {
        for (c = 0; c < dst_channels; c++)
            dst[c] = src[c];
    } else if (src_channels == 3 && dst_channels == 2) {
        dst[0] = src[0] + src[2] * 0.70710678f;
        dst[1] = src[1] + src[2] * 0.70710678f;
    } else if (src_channels > dst_channels) {
        int groups = dst_channels;
        int ch_per_group = src_channels / groups;
        int rem = src_channels % groups;
        int idx = 0;
        for (c = 0; c < groups; c++) {
            int n = ch_per_group + (c < rem ? 1 : 0);
            float sum = 0.0f;
            int i;
            for (i = 0; i < n; i++)
                sum += src[idx++];
            dst[c] = sum / (float)n;
        }
    } else {
        for (c = 0; c < dst_channels; c++)
            dst[c] = src[c % src_channels];
    }
}

void effect_mixer(float *buffer, int frames, int out_channels, void *userdata)
{
    effect_mixer_ctx_t *ctx = (effect_mixer_ctx_t*)userdata;
    voice_t *vo;
    int i, v, c;
    double step_ratio;   /* pre‑computed per voice */

    memset(buffer, 0, (size_t)frames * out_channels * sizeof(float));

    for (v = 0; v < ctx->count; v++) {
        vo = ctx->voices[v];
        if (!vo->active || vo->channels < 1 || vo->volume <= 0.0f)
            continue;

        /* Pre‑compute the sample step for this voice */
        step_ratio = (double)vo->source_sample_rate / (double)ctx->sample_rate * vo->pitch;

        for (i = 0; i < frames; i++) {
            /* ---- Read interpolated sample ---- */
            int ipos = (int)vo->position;
            double frac = vo->position - ipos;
            if (ipos >= vo->num_frames - 1) {
                vo->active = 0;
                break;
            }

            float src[AP_MAX_CHANNELS];
            const float *base = vo->samples + ipos * vo->channels;
            const float *base_next = base + vo->channels;
            for (c = 0; c < vo->channels; c++) {
                src[c] = base[c] + (base_next[c] - base[c]) * (float)frac;
            }

            /* ---- Advance position by the exact step ---- */
            vo->position += step_ratio;

            /* ---- Downmix and volume ---- */
            float frame[AP_MAX_CHANNELS];
            downmix_frame(src, vo->channels, frame, out_channels);

            if (vo->volume != 1.0f) {
                for (c = 0; c < out_channels; c++)
                    frame[c] *= vo->volume;
            }

            /* ---- Accumulate ---- */
            float *output = buffer + i * out_channels;
            for (c = 0; c < out_channels; c++) {
                output[c] += frame[c];
            }
        }
    }

    /* Master clamp */
    int total_samples = frames * out_channels;
    for (i = 0; i < total_samples; i++) {
        if (buffer[i] < -1.0f) buffer[i] = -1.0f;
        else if (buffer[i] > 1.0f) buffer[i] = 1.0f;
    }
}