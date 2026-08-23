/*
  audiomixer.c - Mixer with exaggerated HRTF (notch + low‑pass) with distinct front/back/above/below.
  Behind: very dark, quiet; Below: dark; Above: bright; Front: brightest.
*/

#include "audiomixer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ---- Tube saturation (unchanged) ---- */
static float tube_saturate(float x, float drive)
{
    if (drive <= 0.0f) return x;
    float gain = 1.0f + drive * 4.0f;
    float y = x * gain;
    y = tanhf(y);
    float second_gain = 1.0f + 0.5f * drive;
    y = tanhf(y * second_gain);
    y = tanhf(y * 1.2f);
    float asym = 0.08f * drive;
    y += asym * x * x;
    if (y < -1.0f) y = -1.0f;
    if (y >  1.0f) y =  1.0f;
    return y;
}

/* ---- Reverb (Freeverb style, unchanged) ---- */
static void reverb_update(reverb_t *rv)
{
    const float base_gains[8] = { 0.805f, 0.770f, 0.720f, 0.680f, 0.640f, 0.600f, 0.560f, 0.520f };
    float decay_factor = 0.5f + rv->decay * 0.5f;
    for (int i = 0; i < 8; i++) {
        rv->comb_gain[i] = base_gains[i] * decay_factor;
    }
}

static void reverb_init(reverb_t *rv, int sample_rate)
{
    int i, j;
    const int comb_delays[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    const int ap_delays[4] = { 225, 341, 441, 556 };

    if (rv->initialized) {
        for (i = 0; i < 8; i++) {
            if (rv->comb_buf[i]) { free(rv->comb_buf[i]); rv->comb_buf[i] = NULL; }
        }
        for (i = 0; i < 4; i++) {
            if (rv->ap_buf[i]) { free(rv->ap_buf[i]); rv->ap_buf[i] = NULL; }
        }
    }

    rv->sample_rate = sample_rate;
    rv->wet = 0.2f;
    rv->decay = 0.5f;
    rv->damping = 0.5f;
    rv->ap_gain = 0.7f;

    for (i = 0; i < 8; i++) {
        int len = comb_delays[i];
        rv->comb_len[i] = len;
        rv->comb_pos[i] = 0;
        rv->comb_buf[i] = (float*)calloc(len, sizeof(float));
        rv->comb_damping[i] = 0.0f;
        for (j = 0; j < len; j++) {
            rv->comb_buf[i][j] = ((float)rand() / RAND_MAX) * 0.0005f;
        }
    }
    reverb_update(rv);

    for (i = 0; i < 4; i++) {
        int len = ap_delays[i];
        rv->ap_len[i] = len;
        rv->ap_pos[i] = 0;
        rv->ap_buf[i] = (float*)calloc(len, sizeof(float));
    }

    rv->initialized = 1;
}

static void reverb_process(reverb_t *rv, float *buffer, int frames, int channels)
{
    if (!rv->initialized || rv->wet <= 0.0f) return;
    int i, c, k;
    float wet = rv->wet;
    float dry = 1.0f - wet;
    float damp = 0.05f + rv->damping * 0.94f;

    for (i = 0; i < frames; i++) {
        float *samples = buffer + i * channels;
        float wet_frame[AP_MAX_CHANNELS] = {0};
        for (c = 0; c < channels && c < 2; c++) {
            float input = samples[c];
            float comb_sum = 0.0f;
            for (k = 0; k < 8; k++) {
                float *buf = rv->comb_buf[k];
                int pos = rv->comb_pos[k];
                float out = buf[pos];
                float damped_feedback = rv->comb_damping[k] + damp * (out - rv->comb_damping[k]);
                rv->comb_damping[k] = damped_feedback;
                buf[pos] = input + rv->comb_gain[k] * damped_feedback;
                rv->comb_pos[k] = (pos + 1) % rv->comb_len[k];
                comb_sum += damped_feedback;
            }
            comb_sum *= 0.125f;
            float ap_out = comb_sum;
            for (k = 0; k < 4; k++) {
                float *buf = rv->ap_buf[k];
                int pos = rv->ap_pos[k];
                float out = buf[pos];
                buf[pos] = ap_out + rv->ap_gain * out;
                rv->ap_pos[k] = (pos + 1) % rv->ap_len[k];
                ap_out = out - rv->ap_gain * ap_out;
            }
            wet_frame[c] = ap_out;
        }
        for (c = 0; c < channels; c++) {
            samples[c] = dry * samples[c] + wet * wet_frame[c];
        }
    }
}

/* ---- Low‑pass filter (one‑pole) ---- */
static float lowpass(float input, float *state, float cutoff)
{
    if (cutoff > 0.9999f) return input;
    *state = *state + cutoff * (input - *state);
    return *state;
}

/* ---- Biquad peaking filter (for HRTF notch) ---- */
static float biquad_process(float input, float *state,
                            float b0, float b1, float b2,
                            float a1, float a2)
{
    float z1 = state[0];
    float z2 = state[1];
    float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    state[0] = z1;
    state[1] = z2;
    return output;
}

/* ---- Compute biquad coefficients for peaking notch ---- */
static void compute_peak_coeffs(float freq, float sr, float Q, float gain_db,
                                float *b0, float *b1, float *b2,
                                float *a1, float *a2)
{
    float omega = 2.0f * 3.14159265f * freq / sr;
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * Q);
    float A = powf(10.0f, gain_db / 40.0f);
    float a0;

    *b0 = 1.0f + alpha * A;
    *b1 = -2.0f * cos_omega;
    *b2 = 1.0f - alpha * A;
    a0 = 1.0f + alpha / A;
    *a1 = -2.0f * cos_omega;
    *a2 = 1.0f - alpha / A;

    float inv_a0 = 1.0f / a0;
    *b0 *= inv_a0;
    *b1 *= inv_a0;
    *b2 *= inv_a0;
    *a1 *= inv_a0;
    *a2 *= inv_a0;
}

/* ---- Downmix (unchanged) ---- */
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

/* ---- 3D helpers ---- */
static float distance_attenuation(float distance, float rolloff, float max_distance)
{
    if (distance <= 0.0f) return 1.0f;
    float atten = 1.0f / (1.0f + rolloff * distance * distance);
    if (atten < 0.0f) atten = 0.0f;
    (void)max_distance;
    return atten;
}

static void pan_gains(float angle_rad, float *left_gain, float *right_gain)
{
    float pan = sinf(angle_rad);
    *left_gain  = (float)sqrt((1.0f - pan) / 2.0f);
    *right_gain = (float)sqrt((1.0f + pan) / 2.0f);
}

/* ---- Main mixer ---- */
void effect_mixer(float *buffer, int frames, int out_channels, void *userdata)
{
    effect_mixer_ctx_t *ctx = (effect_mixer_ctx_t*)userdata;
    voice_t *vo;
    int i, v, c;
    float sample_rate = (float)ctx->sample_rate;

    if (!ctx->reverb.initialized) {
        reverb_init(&ctx->reverb, ctx->sample_rate);
    }

    memset(buffer, 0, (size_t)frames * out_channels * sizeof(float));

    float lf_x = ctx->listener.forward_x;
    float lf_y = ctx->listener.forward_y;
    float lf_z = ctx->listener.forward_z;
    float up_x = 0.0f, up_y = 1.0f, up_z = 0.0f;
    float right_x = up_y * lf_z - up_z * lf_y;
    float right_y = up_z * lf_x - up_x * lf_z;
    float right_z = up_x * lf_y - up_y * lf_x;
    float len = sqrtf(right_x*right_x + right_y*right_y + right_z*right_z);
    if (len > 0.0f) {
        right_x /= len; right_y /= len; right_z /= len;
    } else {
        right_x = 1.0f; right_y = 0.0f; right_z = 0.0f;
    }

    float pos_alpha = 0.05f;

    for (i = 0; i < frames; i++) {
        for (v = 0; v < ctx->count; v++) {
            vo = ctx->voices[v];
            if (!vo->active || vo->channels < 1 || vo->volume <= 0.0f)
                continue;

            float gain = 1.0f;
            if (vo->releasing) {
                float decay = 1.0f - 1.0f / (0.01f * sample_rate);
                vo->release_gain *= decay;
                if (vo->release_gain < 0.001f) {
                    vo->active = 0;
                    vo->releasing = 0;
                    vo->release_gain = 0.0f;
                    continue;
                }
                gain = vo->release_gain;
            } else {
                vo->release_gain = 1.0f;
            }

            vo->smooth_x += pos_alpha * (vo->pos_x - vo->smooth_x);
            vo->smooth_y += pos_alpha * (vo->pos_y - vo->smooth_y);
            vo->smooth_z += pos_alpha * (vo->pos_z - vo->smooth_z);

            float dx = vo->smooth_x - ctx->listener.pos_x;
            float dy = vo->smooth_y - ctx->listener.pos_y;
            float dz = vo->smooth_z - ctx->listener.pos_z;
            float distance = sqrtf(dx*dx + dy*dy + dz*dz);

            float atten = distance_attenuation(distance, vo->rolloff, vo->max_distance);
            if (atten <= 0.0f) continue;

            float proj_forward = dx * lf_x + dy * lf_y + dz * lf_z;
            float proj_right   = dx * right_x + dy * right_y + dz * right_z;
            float angle = atan2f(proj_right, proj_forward);
            float left_gain, right_gain;
            pan_gains(angle, &left_gain, &right_gain);

            /* ---- Distance low‑pass ---- */
            float cutoff = 1.0f - atten;
            if (cutoff < 0.0f) cutoff = 0.0f;
            if (cutoff > 0.95f) cutoff = 0.95f;
            float dist_cutoff = 1.0f - cutoff * 0.8f;

            /* ---- HRTF: compute notch gain and volume dip based on position ---- */
            float frontness = proj_forward / (distance + 0.001f);
            frontness = fminf(fmaxf(frontness, -1.0f), 1.0f);

            float elevation = asinf(dy / (distance + 0.001f));
            float elev_norm = fabsf(elevation) / (3.14159265f / 2.0f);

            /* ---- Behind (frontness < 0) ---- */
            float behind_factor = (frontness < 0.0f) ? -frontness : 0.0f; /* 0..1 */
            /* ---- Elevation factor ---- */
            float elev_factor = elev_norm; /* 0..1 */

            /* ---- Notch gains: behind very strong, above/below moderate ---- */
            float notch_gain_behind = -14.0f * behind_factor;  /* -14 dB behind */
            float notch_gain_elev = -6.0f * elev_factor;      /* -6 dB above/below */
            float total_notch_gain = notch_gain_behind + notch_gain_elev;
            if (total_notch_gain > 0.0f) total_notch_gain = 0.0f;
            if (total_notch_gain < -20.0f) total_notch_gain = -20.0f;

            /* ---- Volume dips: behind -6 dB, above/below -2 dB ---- */
            float volume_dip_behind = -6.0f * behind_factor;
            float volume_dip_elev   = -2.0f * elev_factor;
            float total_volume_dip_db = volume_dip_behind + volume_dip_elev;
            if (total_volume_dip_db > 0.0f) total_volume_dip_db = 0.0f;
            if (total_volume_dip_db < -8.0f) total_volume_dip_db = -8.0f;
            float volume_mult = powf(10.0f, total_volume_dip_db / 20.0f);

            /* ---- Elevation low‑pass: ABOVE vs BELOW, plus BEHIND extra dark ---- */
            float elev_cutoff = 1.0f;
            if (fabsf(elevation) > 0.05f) {
                float elev_abs = fabsf(elevation);
                float elev_norm_lp = elev_abs / (3.14159265f / 2.0f); /* 0..1 */
                if (elevation > 0) { /* ABOVE */
                    /* bright: 8000 Hz -> 4000 Hz */
                    float freq = 8000.0f - 4000.0f * elev_norm_lp;
                    freq = fmaxf(freq, 4000.0f);
                    elev_cutoff = freq / sample_rate;
                } else { /* BELOW */
                    /* medium-dark: 5000 Hz -> 2000 Hz */
                    float freq = 5000.0f - 3000.0f * elev_norm_lp;
                    freq = fmaxf(freq, 2000.0f);
                    elev_cutoff = freq / sample_rate;
                }
                elev_cutoff = fminf(fmaxf(elev_cutoff, 0.01f), 1.0f);
            }

            /* ---- Behind low‑pass (extra dark) ---- */
            float behind_cutoff = 1.0f;
            if (behind_factor > 0.01f) {
                /* fully behind: 1000 Hz */
                float freq = 4000.0f - 3000.0f * behind_factor;
                freq = fmaxf(freq, 800.0f);
                behind_cutoff = freq / sample_rate;
                behind_cutoff = fminf(fmaxf(behind_cutoff, 0.01f), 1.0f);
            }

            /* ---- Combine cutoffs (take the lowest) ---- */
            float total_cutoff = dist_cutoff;
            if (elev_cutoff < total_cutoff) total_cutoff = elev_cutoff;
            if (behind_cutoff < total_cutoff) total_cutoff = behind_cutoff;

            /* ---- Compute biquad coefficients (notch at 7.5 kHz, Q=1.5) ---- */
            float b0, b1, b2, a1, a2;
            float notch_freq = 7500.0f;
            float Q = 1.5f;
            compute_peak_coeffs(notch_freq, sample_rate, Q, total_notch_gain,
                                &b0, &b1, &b2, &a1, &a2);

            /* ---- Doppler ---- */
            float rel_vel_x = vo->vel_x - ctx->listener.vel_x;
            float rel_vel_y = vo->vel_y - ctx->listener.vel_y;
            float rel_vel_z = vo->vel_z - ctx->listener.vel_z;
            float rel_vel = (rel_vel_x*dx + rel_vel_y*dy + rel_vel_z*dz) / (distance + 0.001f);
            float speed_of_sound = 343.0f;
            float doppler = speed_of_sound / (speed_of_sound - rel_vel);
            if (doppler < 0.5f) doppler = 0.5f;
            if (doppler > 2.0f) doppler = 2.0f;
            float pitch = vo->pitch * doppler;

            /* ---- Read sample ---- */
            int ipos = (int)vo->position;
            double frac = vo->position - ipos;
            if (ipos >= vo->num_frames - 1) {
                vo->active = 0;
                continue;
            }

            float src[AP_MAX_CHANNELS];
            const float *base = vo->samples + ipos * vo->channels;
            const float *base_next = base + vo->channels;
            for (c = 0; c < vo->channels; c++) {
                src[c] = base[c] + (base_next[c] - base[c]) * (float)frac;
            }

            double resample_ratio = (double)vo->source_sample_rate / sample_rate;
            vo->position += pitch * resample_ratio;

            float frame[AP_MAX_CHANNELS];
            downmix_frame(src, vo->channels, frame, out_channels);

            /* ---- Apply volume (base * distance * release * volume_mult) ---- */
            float vol = vo->volume * atten * gain * volume_mult;
            if (vol != 1.0f) {
                for (c = 0; c < out_channels; c++)
                    frame[c] *= vol;
            }

            /* ---- Pan (stereo) ---- */
            if (out_channels == 2) {
                float l = frame[0] * left_gain + frame[1] * left_gain;
                float r = frame[0] * right_gain + frame[1] * right_gain;
                frame[0] = l;
                frame[1] = r;
            }

            /* ---- Apply HRTF notch (peaking filter) ---- */
            if (total_notch_gain < -0.1f) {
                for (c = 0; c < out_channels; c++) {
                    float *state = &vo->hrtf_bq_state[c * 2];
                    frame[c] = biquad_process(frame[c], state, b0, b1, b2, a1, a2);
                }
            }

            /* ---- Apply combined low‑pass ---- */
            if (total_cutoff < 0.9999f) {
                for (c = 0; c < out_channels; c++) {
                    frame[c] = lowpass(frame[c], &vo->hrtf_lp_state[c], total_cutoff);
                }
            }

            /* ---- Tube saturation (optional) ---- */
            if (vo->drive > 0.0f) {
                for (c = 0; c < out_channels; c++)
                    frame[c] = tube_saturate(frame[c], vo->drive);
            }

            /* ---- Accumulate ---- */
            float *output = buffer + i * out_channels;
            for (c = 0; c < out_channels; c++) {
                output[c] += frame[c];
            }
        }
    }

    /* ---- Reverb ---- */
    reverb_process(&ctx->reverb, buffer, frames, out_channels);

    /* ---- Master clamp ---- */
    int total_samples = frames * out_channels;
    for (i = 0; i < total_samples; i++) {
        if (buffer[i] < -1.0f) buffer[i] = -1.0f;
        else if (buffer[i] > 1.0f) buffer[i] = 1.0f;
    }
}