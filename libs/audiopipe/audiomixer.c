/*
    audiomixer.c
        Parametric HRTF (with 3 dynamic pinna notches, frontal resonance,
    and torso shadow), ITD, frequency‑dependent ILD, perceptual distance,
    Doppler, directional exciters (pre‑low, post‑mid, post‑high),
    tube saturation, peak EQ, soft‑knee limiter, and zipper‑free smoothing.
*/

/* ---- Tuning Constants ---- */
#define EAR_ANGLE_DEGREES       70.0f
#define ITD_MAX_DELAY_SEC       0.0006f
#define DOPPLER_SCALE           1.0f
#define DOPPLER_MIN             0.7f
#define DOPPLER_MAX             1.4f
#define SPEED_OF_SOUND          343.0f

#define NOTCH_BASE_FREQ         7500.0f
#define NOTCH_FRONT_SHIFT       2000.0f
#define NOTCH_ELEV_SHIFT        1500.0f
#define NOTCH_MIN_FREQ          4000.0f
#define NOTCH_MAX_FREQ          11000.0f
#define NOTCH_Q                 1.5f
#define NOTCH_GAIN_SIDE         -8.0f
#define NOTCH_GAIN_BEHIND       -14.0f
#define NOTCH_GAIN_ELEV         -6.0f
#define NOTCH_GAIN_MIN          -20.0f

#define SECONDARY_NOTCH_BASE_FREQ   11000.0f
#define SECONDARY_NOTCH_SHIFT       1000.0f
#define SECONDARY_NOTCH_ELEV_SHIFT  500.0f
#define SECONDARY_NOTCH_MIN_FREQ    9000.0f
#define SECONDARY_NOTCH_MAX_FREQ    13000.0f
#define SECONDARY_NOTCH_SCALE   0.5f

#define TERTIARY_NOTCH_FREQ     14000.0f
#define TERTIARY_NOTCH_SCALE    0.25f

#define RESONANCE_FREQ          2500.0f
#define RESONANCE_Q             1.0f
#define RESONANCE_GAIN_MAX      4.0f
#define RESONANCE_THRESHOLD     0.5f

#define ELEV_FREQ_MIN           2000.0f
#define ELEV_FREQ_MAX           8000.0f
#define SIDE_CUTOFF_SCALE       0.4f
#define DIST_CUTOFF_SCALE       0.8f
#define PERCEPTUAL_DB_PER_DOUBLING 3.0f

#define HRTF_SMOOTH_ALPHA       0.2f
#define NOTCH_MOD_SMOOTH_ALPHA  0.3f
#define POS_SMOOTH_ALPHA        0.0024f
#define RELEASE_DECAY           0.01f
#define REVERB_SMOOTH_ALPHA     0.05f
#define HEAD_SMOOTH_ALPHA       0.15f

/* ---- Directional Exciters ---- */
#define PRE_EXCIT_LOW_FREQ      1500.0f   /* Generates harmonics for low-frequency sounds */
#define PRE_EXCIT_LOW_GAIN      2.0f
#define PRE_EXCIT_LOW_Q         0.6f

#define POST_EXCIT_MID_FREQ     NOTCH_BASE_FREQ   /* 7500 Hz – primary notch boost */
#define POST_EXCIT_MID_GAIN     3.0f
#define POST_EXCIT_MID_Q        0.8f

#define POST_EXCIT_HIGH_FREQ    SECONDARY_NOTCH_BASE_FREQ   /* 11000 Hz – secondary notch boost */
#define POST_EXCIT_HIGH_GAIN    3.0f
#define POST_EXCIT_HIGH_Q       0.8f

/* ---- Frequency-Dependent ILD Crossover ---- */
#define CROSSOVER_FREQ          1200.0f

/* ---- Torso Shadow ---- */
#define TORSO_BOOST_REFERENCE_DB  2.0f

#include "audiomixer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

/* ========================================================================
   TUBE SATURATION
   ======================================================================== */
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

/* ========================================================================
   REVERB (Freeverb style – simple and cheap)
   ======================================================================== */
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
    rv->decay = 0.4f;
    rv->damping = 0.7f;
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

    rv->target_decay = rv->decay;
    rv->target_damping = rv->damping;
    rv->smooth_decay = rv->decay;
    rv->smooth_damping = rv->damping;
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

/* ========================================================================
   FILTER HELPERS
   ======================================================================== */
static float lowpass(float input, float *state, float cutoff)
{
    if (cutoff > 0.9999f) return input;
    *state = *state + cutoff * (input - *state);
    return *state;
}

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

/* ========================================================================
   DOWNMIX
   ======================================================================== */
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

/* ========================================================================
   DISTANCE ATTENUATION (Perceptual)
   ======================================================================== */
static float distance_attenuation(float distance, float rolloff)
{
    if (distance <= 0.0f) return 1.0f;
    if (rolloff <= 0.0f) return 1.0f;

    float ref_distance = 1.0f;
    float doublings = log2f(distance / ref_distance);
    if (doublings < 0.0f) doublings = 0.0f;

    float db_drop = PERCEPTUAL_DB_PER_DOUBLING * rolloff * doublings;
    float atten = powf(10.0f, -db_drop / 20.0f);
    if (atten < 0.001f) atten = 0.001f;
    return atten;
}

/* ========================================================================
   PAN GAINS (Constant Power) – Used as base for frequency‑dependent ILD
   ======================================================================== */
static void pan_gains(float angle_rad, float *left_gain, float *right_gain)
{
    float pan = sinf(angle_rad);
    *left_gain  = (float)sqrt((1.0f - pan) / 2.0f);
    *right_gain = (float)sqrt((1.0f + pan) / 2.0f);
}

/* ========================================================================
   MAIN MIXER
   ======================================================================== */
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

    /* ---- Smooth geometry‑driven reverb parameters ---- */
    if (ctx->reverb.target_decay > 0.01f) {
        float alpha = REVERB_SMOOTH_ALPHA;
        ctx->reverb.smooth_decay += alpha * (ctx->reverb.target_decay - ctx->reverb.smooth_decay);
        ctx->reverb.smooth_damping += alpha * (ctx->reverb.target_damping - ctx->reverb.smooth_damping);
        ctx->reverb.decay = ctx->reverb.smooth_decay;
        ctx->reverb.damping = ctx->reverb.smooth_damping;
        reverb_update(&ctx->reverb);
    }

    /* ---- Smooth listener rotation (internal) ---- */
    float target_fx = ctx->listener.forward_x;
    float target_fy = ctx->listener.forward_y;
    float target_fz = ctx->listener.forward_z;

    if (!ctx->listener.smooth_forward_initialized) {
        ctx->listener.smooth_forward_x = target_fx;
        ctx->listener.smooth_forward_y = target_fy;
        ctx->listener.smooth_forward_z = target_fz;
        ctx->listener.smooth_forward_initialized = 1;
    }

    ctx->listener.smooth_forward_x += HEAD_SMOOTH_ALPHA * (target_fx - ctx->listener.smooth_forward_x);
    ctx->listener.smooth_forward_y += HEAD_SMOOTH_ALPHA * (target_fy - ctx->listener.smooth_forward_y);
    ctx->listener.smooth_forward_z += HEAD_SMOOTH_ALPHA * (target_fz - ctx->listener.smooth_forward_z);

    float lf_x = ctx->listener.smooth_forward_x;
    float lf_y = ctx->listener.smooth_forward_y;
    float lf_z = ctx->listener.smooth_forward_z;

    float up_x = ctx->listener.up_x;
    float up_y = ctx->listener.up_y;
    float up_z = ctx->listener.up_z;
    float up_len = sqrtf(up_x*up_x + up_y*up_y + up_z*up_z);
    if (up_len > 0.001f) {
        up_x /= up_len; up_y /= up_len; up_z /= up_len;
    } else {
        up_x = 0.0f; up_y = 1.0f; up_z = 0.0f;
    }

    float right_x = lf_y * up_z - lf_z * up_y;
    float right_y = lf_z * up_x - lf_x * up_z;
    float right_z = lf_x * up_y - lf_y * up_x;
    float right_len = sqrtf(right_x*right_x + right_y*right_y + right_z*right_z);
    if (right_len > 0.001f) {
        right_x /= right_len; right_y /= right_len; right_z /= right_len;
    } else {
        right_x = 1.0f; right_y = 0.0f; right_z = 0.0f;
    }

    float pos_alpha = POS_SMOOTH_ALPHA;

    for (i = 0; i < frames; i++) {
        for (v = 0; v < ctx->count; v++) {
            vo = ctx->voices[v];
            if (!vo->active || vo->channels < 1 || vo->volume <= 0.0f)
                continue;

            float gain = 1.0f;
            if (vo->releasing) {
                float decay = 1.0f - 1.0f / (RELEASE_DECAY * sample_rate);
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

            float atten = distance_attenuation(distance, vo->rolloff);
            if (atten <= 0.0f) continue;

            /* ---- Head-relative azimuth ---- */
            float proj_forward = dx * lf_x + dy * lf_y + dz * lf_z;
            float proj_right   = dx * right_x + dy * right_y + dz * right_z;
            float angle = atan2f(proj_right, proj_forward);
            float frontness = proj_forward / (distance + 0.001f);
            frontness = fminf(fmaxf(frontness, -1.0f), 1.0f);

            float left_pan_gain, right_pan_gain;
            pan_gains(angle, &left_pan_gain, &right_pan_gain);

            /* ---- Head-relative elevation ---- */
            float proj_up = dx * up_x + dy * up_y + dz * up_z;
            float elevation = asinf(proj_up / (distance + 0.001f));
            float elev_norm = elevation / (3.14159265f / 2.0f);
            if (elev_norm > 1.0f) elev_norm = 1.0f;
            if (elev_norm < -1.0f) elev_norm = -1.0f;

            /* ---- Distance low‑pass (air absorption) ---- */
            float cutoff = 1.0f - atten;
            if (cutoff < 0.0f) cutoff = 0.0f;
            if (cutoff > 0.95f) cutoff = 0.95f;
            float dist_cutoff = 1.0f - cutoff * DIST_CUTOFF_SCALE;

            /* ---- Elevation low‑pass (log mapping) ---- */
            float elev_norm_smooth = (elevation + 1.5708f) / 3.14159265f;
            if (elev_norm_smooth < 0.0f) elev_norm_smooth = 0.0f;
            if (elev_norm_smooth > 1.0f) elev_norm_smooth = 1.0f;

            float log_min = logf(ELEV_FREQ_MIN);
            float log_max = logf(ELEV_FREQ_MAX);
            float log_freq = log_min + (log_max - log_min) * elev_norm_smooth;
            float elev_freq = expf(log_freq);
            float elev_cutoff = elev_freq / sample_rate;
            if (elev_cutoff < 0.01f) elev_cutoff = 0.01f;
            if (elev_cutoff > 1.0f) elev_cutoff = 1.0f;

            /* ---- Doppler ---- */
            float rel_vel_x = vo->vel_x - ctx->listener.vel_x;
            float rel_vel_y = vo->vel_y - ctx->listener.vel_y;
            float rel_vel_z = vo->vel_z - ctx->listener.vel_z;
            float rel_vel = (rel_vel_x*dx + rel_vel_y*dy + rel_vel_z*dz) / (distance + 0.001f);
            float effective_rel_vel = rel_vel * DOPPLER_SCALE;
            float doppler = SPEED_OF_SOUND / (SPEED_OF_SOUND + effective_rel_vel);
            if (doppler < DOPPLER_MIN) doppler = DOPPLER_MIN;
            if (doppler > DOPPLER_MAX) doppler = DOPPLER_MAX;
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

            if (vo->position >= vo->num_frames) {
                if (vo->looping) {
                    vo->position = fmod(vo->position, (double)vo->num_frames);
                } else {
                    vo->active = 0;
                    vo->releasing = 0;
                    vo->position = 0;
                    continue;
                }
            }

            /* ---- Downmix to mono ---- */
            float mono_sample = 0.0f;
            for (c = 0; c < vo->channels; c++) {
                mono_sample += src[c];
            }
            mono_sample /= (float)vo->channels;

            /* ================================================================
               SIGNAL CHAIN – ARTISTIC → SPATIAL
               ================================================================ */

            /* 2. PRE‑SATURATION EXCITER (Low only) */
            float b0, b1, b2, a1, a2;
            compute_peak_coeffs(PRE_EXCIT_LOW_FREQ, sample_rate, PRE_EXCIT_LOW_Q, PRE_EXCIT_LOW_GAIN,
                                &b0, &b1, &b2, &a1, &a2);
            mono_sample = biquad_process(mono_sample, vo->pre_excit_state[0],
                                         b0, b1, b2, a1, a2);

            /* 3. Tube Saturation */
            if (vo->drive > 0.0f) {
                float saturated = tube_saturate(mono_sample, vo->drive);
                mono_sample = mono_sample * (1.0f - vo->sat_mix) + saturated * vo->sat_mix;
            }

            /* 4. POST‑SATURATION EXCITERS (Mid + High) */
            compute_peak_coeffs(POST_EXCIT_MID_FREQ, sample_rate, POST_EXCIT_MID_Q, POST_EXCIT_MID_GAIN,
                                &b0, &b1, &b2, &a1, &a2);
            mono_sample = biquad_process(mono_sample, vo->post_excit_state[0],
                                         b0, b1, b2, a1, a2);

            compute_peak_coeffs(POST_EXCIT_HIGH_FREQ, sample_rate, POST_EXCIT_HIGH_Q, POST_EXCIT_HIGH_GAIN,
                                &b0, &b1, &b2, &a1, &a2);
            mono_sample = biquad_process(mono_sample, vo->post_excit_state[1],
                                         b0, b1, b2, a1, a2);

            /* 5. Artistic EQ */
            if (vo->eq_gain_db != 0.0f) {
                float b0_e, b1_e, b2_e, a1_e, a2_e;
                compute_peak_coeffs(vo->eq_freq, sample_rate, vo->eq_q, vo->eq_gain_db,
                                    &b0_e, &b1_e, &b2_e, &a1_e, &a2_e);
                mono_sample = biquad_process(mono_sample, vo->eq_state, b0_e, b1_e, b2_e, a1_e, a2_e);
            }

            /* 6. Distance Attenuation (volume) */
            float vol = vo->volume * atten * gain;
            if (vol < 0.0001f) vol = 0.0001f;
            mono_sample *= vol;

            /* ---- ITD (smooth crossfade) ---- */
            int max_delay_samples = (int)(ITD_MAX_DELAY_SEC * sample_rate + 0.5f);
            if (max_delay_samples < 1) max_delay_samples = 1;
            if (max_delay_samples > 127) max_delay_samples = 127;

            float abs_angle = fabsf(angle);
            float angle_norm = abs_angle / 1.5708f;
            if (angle_norm > 1.0f) angle_norm = 1.0f;
            int delay_samples = (int)(max_delay_samples * angle_norm);

            vo->delay_buffer[0][vo->delay_write_pos] = mono_sample;
            vo->delay_buffer[1][vo->delay_write_pos] = mono_sample;
            vo->delay_write_pos = (vo->delay_write_pos + 1) % 128;

            float delayed_sample = mono_sample;
            if (delay_samples > 0) {
                int read_pos = (vo->delay_write_pos - delay_samples + 128) % 128;
                delayed_sample = vo->delay_buffer[0][read_pos];
            }

            float pan = sinf(angle);
            float left_blend = (pan > 0.0f) ? pan : 0.0f;
            float right_blend = (pan < 0.0f) ? -pan : 0.0f;

            float left_hrtf_input = (1.0f - left_blend) * mono_sample + left_blend * delayed_sample;
            float right_hrtf_input = (1.0f - right_blend) * mono_sample + right_blend * delayed_sample;

            /* ---- HRTF parameters (smoothed) ---- */
            float side_factor = fabsf(angle) / 1.5708f;
            if (side_factor > 1.0f) side_factor = 1.0f;

            static int first_frame = 1;
            if (first_frame) {
                vo->smooth_side_factor = side_factor;
                vo->smooth_elev_norm = elev_norm;
                first_frame = 0;
            }
            float mod_alpha = NOTCH_MOD_SMOOTH_ALPHA;
            vo->smooth_side_factor += mod_alpha * (side_factor - vo->smooth_side_factor);
            vo->smooth_elev_norm += mod_alpha * (elev_norm - vo->smooth_elev_norm);

            float smooth_side = vo->smooth_side_factor;
            float smooth_elev = vo->smooth_elev_norm;

            float behind_factor = (frontness < 0.0f) ? -frontness : 0.0f;

            float left_notch_gain_side = NOTCH_GAIN_SIDE * smooth_side;
            float left_notch_gain_behind = NOTCH_GAIN_BEHIND * behind_factor;
            float left_notch_gain_elev = NOTCH_GAIN_ELEV * smooth_elev;
            float left_total_notch_gain = left_notch_gain_side + left_notch_gain_behind + left_notch_gain_elev;
            if (left_total_notch_gain > 0.0f) left_total_notch_gain = 0.0f;
            if (left_total_notch_gain < NOTCH_GAIN_MIN) left_total_notch_gain = NOTCH_GAIN_MIN;

            float right_notch_gain_side = NOTCH_GAIN_SIDE * smooth_side;
            float right_notch_gain_behind = NOTCH_GAIN_BEHIND * behind_factor;
            float right_notch_gain_elev = NOTCH_GAIN_ELEV * smooth_elev;
            float right_total_notch_gain = right_notch_gain_side + right_notch_gain_behind + right_notch_gain_elev;
            if (right_total_notch_gain > 0.0f) right_total_notch_gain = 0.0f;
            if (right_total_notch_gain < NOTCH_GAIN_MIN) right_total_notch_gain = NOTCH_GAIN_MIN;

            float raw_left_notch_freq = NOTCH_BASE_FREQ + NOTCH_FRONT_SHIFT * frontness + NOTCH_ELEV_SHIFT * smooth_elev;
            raw_left_notch_freq = fminf(fmaxf(raw_left_notch_freq, NOTCH_MIN_FREQ), NOTCH_MAX_FREQ);
            float raw_right_notch_freq = raw_left_notch_freq; /* symmetric */

            float left_behind_cutoff = 1.0f;
            if (behind_factor > 0.01f) {
                float freq = 4000.0f - 3000.0f * behind_factor;
                freq = fmaxf(freq, 800.0f);
                left_behind_cutoff = freq / sample_rate;
                left_behind_cutoff = fminf(fmaxf(left_behind_cutoff, 0.01f), 1.0f);
            }
            float right_behind_cutoff = left_behind_cutoff;

            float side_cutoff = 1.0f - SIDE_CUTOFF_SCALE * smooth_side;

            float raw_left_cutoff = dist_cutoff;
            if (elev_cutoff < raw_left_cutoff) raw_left_cutoff = elev_cutoff;
            if (left_behind_cutoff < raw_left_cutoff) raw_left_cutoff = left_behind_cutoff;

            float raw_right_cutoff = dist_cutoff;
            if (elev_cutoff < raw_right_cutoff) raw_right_cutoff = elev_cutoff;
            if (right_behind_cutoff < raw_right_cutoff) raw_right_cutoff = right_behind_cutoff;

            float hrtf_alpha = HRTF_SMOOTH_ALPHA;
            if (first_frame) {
                vo->smooth_left_notch_freq = raw_left_notch_freq;
                vo->smooth_right_notch_freq = raw_right_notch_freq;
                vo->smooth_left_cutoff = raw_left_cutoff;
                vo->smooth_right_cutoff = raw_right_cutoff;
            }
            vo->smooth_left_notch_freq += hrtf_alpha * (raw_left_notch_freq - vo->smooth_left_notch_freq);
            vo->smooth_right_notch_freq += hrtf_alpha * (raw_right_notch_freq - vo->smooth_right_notch_freq);
            vo->smooth_left_cutoff += hrtf_alpha * (raw_left_cutoff - vo->smooth_left_cutoff);
            vo->smooth_right_cutoff += hrtf_alpha * (raw_right_cutoff - vo->smooth_right_cutoff);

            float left_notch_freq = vo->smooth_left_notch_freq;
            float right_notch_freq = vo->smooth_right_notch_freq;
            float left_cutoff = vo->smooth_left_cutoff;
            float right_cutoff = vo->smooth_right_cutoff;

            if (angle > 0.0f) {
                if (side_cutoff < left_cutoff) left_cutoff = side_cutoff;
            } else if (angle < 0.0f) {
                if (side_cutoff < right_cutoff) right_cutoff = side_cutoff;
            }

            float Q = NOTCH_Q;
            float b0_notch, b1_notch, b2_notch, a1_notch, a2_notch;

            /* ---- LEFT EAR HRTF ---- */
            if (left_total_notch_gain < -0.1f) {
                compute_peak_coeffs(left_notch_freq, sample_rate, Q, left_total_notch_gain,
                                    &b0_notch, &b1_notch, &b2_notch, &a1_notch, &a2_notch);
                left_hrtf_input = biquad_process(left_hrtf_input, &vo->hrtf_bq_state[0],
                                                 b0_notch, b1_notch, b2_notch, a1_notch, a2_notch);
            }
            /* Secondary notch (left) */
            float left_secondary_freq = SECONDARY_NOTCH_BASE_FREQ
                                      + SECONDARY_NOTCH_SHIFT * frontness
                                      + SECONDARY_NOTCH_ELEV_SHIFT * smooth_elev;
            left_secondary_freq = fminf(fmaxf(left_secondary_freq, SECONDARY_NOTCH_MIN_FREQ), SECONDARY_NOTCH_MAX_FREQ);
            float left_secondary_gain = left_total_notch_gain * SECONDARY_NOTCH_SCALE;
            if (left_secondary_gain < -0.1f) {
                float b0_s, b1_s, b2_s, a1_s, a2_s;
                compute_peak_coeffs(left_secondary_freq, sample_rate, Q, left_secondary_gain,
                                    &b0_s, &b1_s, &b2_s, &a1_s, &a2_s);
                left_hrtf_input = biquad_process(left_hrtf_input, &vo->secondary_bq_state[0],
                                                 b0_s, b1_s, b2_s, a1_s, a2_s);
            }
            /* Tertiary notch (left) */
            float left_tertiary_gain = left_total_notch_gain * TERTIARY_NOTCH_SCALE;
            if (left_tertiary_gain < -0.1f) {
                float b0_t, b1_t, b2_t, a1_t, a2_t;
                compute_peak_coeffs(TERTIARY_NOTCH_FREQ, sample_rate, Q, left_tertiary_gain,
                                    &b0_t, &b1_t, &b2_t, &a1_t, &a2_t);
                left_hrtf_input = biquad_process(left_hrtf_input, &vo->tertiary_bq_state[0],
                                                 b0_t, b1_t, b2_t, a1_t, a2_t);
            }
            /* Resonance (left) */
            float left_res_gain_db = RESONANCE_GAIN_MAX * fmaxf(frontness, 0.0f);
            vo->smooth_res_gain_left += HRTF_SMOOTH_ALPHA * (left_res_gain_db - vo->smooth_res_gain_left);
            left_res_gain_db = vo->smooth_res_gain_left;
            if (left_res_gain_db > RESONANCE_THRESHOLD) {
                float b0_r, b1_r, b2_r, a1_r, a2_r;
                compute_peak_coeffs(RESONANCE_FREQ, sample_rate, RESONANCE_Q, left_res_gain_db,
                                    &b0_r, &b1_r, &b2_r, &a1_r, &a2_r);
                left_hrtf_input = biquad_process(left_hrtf_input, &vo->resonance_state[0],
                                                 b0_r, b1_r, b2_r, a1_r, a2_r);
            }
            /* Low-pass (left) */
            if (left_cutoff < 0.9999f) {
                left_hrtf_input = lowpass(left_hrtf_input, &vo->hrtf_lp_state[0], left_cutoff);
                left_hrtf_input = lowpass(left_hrtf_input, &vo->hrtf_lp2_state[0], left_cutoff);
            }

            /* ---- RIGHT EAR HRTF (mirrors left, using right_notch_freq) ---- */
            if (right_total_notch_gain < -0.1f) {
                compute_peak_coeffs(right_notch_freq, sample_rate, Q, right_total_notch_gain,
                                    &b0_notch, &b1_notch, &b2_notch, &a1_notch, &a2_notch);
                right_hrtf_input = biquad_process(right_hrtf_input, &vo->hrtf_bq_state[2],
                                                  b0_notch, b1_notch, b2_notch, a1_notch, a2_notch);
            }
            /* Secondary notch (right) */
            float right_secondary_freq = SECONDARY_NOTCH_BASE_FREQ
                                       + SECONDARY_NOTCH_SHIFT * frontness
                                       + SECONDARY_NOTCH_ELEV_SHIFT * smooth_elev;
            right_secondary_freq = fminf(fmaxf(right_secondary_freq, SECONDARY_NOTCH_MIN_FREQ), SECONDARY_NOTCH_MAX_FREQ);
            float right_secondary_gain = right_total_notch_gain * SECONDARY_NOTCH_SCALE;
            if (right_secondary_gain < -0.1f) {
                float b0_s, b1_s, b2_s, a1_s, a2_s;
                compute_peak_coeffs(right_secondary_freq, sample_rate, Q, right_secondary_gain,
                                    &b0_s, &b1_s, &b2_s, &a1_s, &a2_s);
                right_hrtf_input = biquad_process(right_hrtf_input, &vo->secondary_bq_state[2],
                                                  b0_s, b1_s, b2_s, a1_s, a2_s);
            }
            /* Tertiary notch (right) */
            float right_tertiary_gain = right_total_notch_gain * TERTIARY_NOTCH_SCALE;
            if (right_tertiary_gain < -0.1f) {
                float b0_t, b1_t, b2_t, a1_t, a2_t;
                compute_peak_coeffs(TERTIARY_NOTCH_FREQ, sample_rate, Q, right_tertiary_gain,
                                    &b0_t, &b1_t, &b2_t, &a1_t, &a2_t);
                right_hrtf_input = biquad_process(right_hrtf_input, &vo->tertiary_bq_state[2],
                                                  b0_t, b1_t, b2_t, a1_t, a2_t);
            }
            /* Resonance (right) */
            float right_res_gain_db = RESONANCE_GAIN_MAX * fmaxf(frontness, 0.0f);
            vo->smooth_res_gain_right += HRTF_SMOOTH_ALPHA * (right_res_gain_db - vo->smooth_res_gain_right);
            right_res_gain_db = vo->smooth_res_gain_right;
            if (right_res_gain_db > RESONANCE_THRESHOLD) {
                float b0_r, b1_r, b2_r, a1_r, a2_r;
                compute_peak_coeffs(RESONANCE_FREQ, sample_rate, RESONANCE_Q, right_res_gain_db,
                                    &b0_r, &b1_r, &b2_r, &a1_r, &a2_r);
                right_hrtf_input = biquad_process(right_hrtf_input, &vo->resonance_state[2],
                                                  b0_r, b1_r, b2_r, a1_r, a2_r);
            }
            /* Low-pass (right) */
            if (right_cutoff < 0.9999f) {
                right_hrtf_input = lowpass(right_hrtf_input, &vo->hrtf_lp_state[1], right_cutoff);
                right_hrtf_input = lowpass(right_hrtf_input, &vo->hrtf_lp2_state[1], right_cutoff);
            }

            /* ---- Frequency-Dependent ILD (Head Shadow) ---- */
            float cross_coeff = expf(-2.0f * 3.14159265f * CROSSOVER_FREQ / sample_rate);

            float low_left = left_hrtf_input * (1.0f - cross_coeff) + vo->crossover_lpf_state[0] * cross_coeff;
            vo->crossover_lpf_state[0] = low_left;
            float low_right = right_hrtf_input * (1.0f - cross_coeff) + vo->crossover_lpf_state[1] * cross_coeff;
            vo->crossover_lpf_state[1] = low_right;

            float high_left = left_hrtf_input - low_left;
            float high_right = right_hrtf_input - low_right;

            /* Aggressive high-band panning */
            float side_aggressive = vo->smooth_side_factor;
            float high_left_gain = 1.0f - 0.9f * side_aggressive * (angle > 0.0f ? 1.0f : 0.0f);
            float high_right_gain = 1.0f - 0.9f * side_aggressive * (angle < 0.0f ? 1.0f : 0.0f);
            if (high_left_gain < 0.0f) high_left_gain = 0.0f;
            if (high_right_gain < 0.0f) high_right_gain = 0.0f;

            /* ---- TORSO SHADOW (Broad Low-Mid EQ) ---- */
            float torso_boost_db = TORSO_BOOST_REFERENCE_DB * frontness * (1.0f - fabsf(smooth_side) * 0.5f);
            if (torso_boost_db > 3.0f) torso_boost_db = 3.0f;
            if (torso_boost_db < -3.0f) torso_boost_db = -3.0f;
            float torso_gain = powf(10.0f, torso_boost_db / 20.0f);

            /* Apply torso shadow to the low bands only */
            low_left *= torso_gain;
            low_right *= torso_gain;

            /* ---- Recombine ---- */
            float left_signal = low_left * left_pan_gain + high_left * high_left_gain;
            float right_signal = low_right * right_pan_gain + high_right * high_right_gain;

            float frame[AP_MAX_CHANNELS];
            frame[0] = left_signal;
            frame[1] = right_signal;
            for (c = 2; c < out_channels; c++) {
                frame[c] = (left_signal + right_signal) * 0.5f;
            }

            float *output = buffer + i * out_channels;
            for (c = 0; c < out_channels; c++) {
                output[c] += frame[c];
            }
        }
    }

    /* ---- Dynamic Reverb Wet ---- */
    float total_dist = 0.0f;
    int voice_count = 0;
    for (v = 0; v < ctx->count; v++) {
        vo = ctx->voices[v];
        if (vo->active && vo->volume > 0.0f) {
            float dx, dy, dz;
            if (vo->portal_active) {
                dx = vo->portal_pos_x - ctx->listener.pos_x;
                dy = vo->portal_pos_y - ctx->listener.pos_y;
                dz = vo->portal_pos_z - ctx->listener.pos_z;
            } else {
                dx = vo->smooth_x - ctx->listener.pos_x;
                dy = vo->smooth_y - ctx->listener.pos_y;
                dz = vo->smooth_z - ctx->listener.pos_z;
            }
            total_dist += sqrtf(dx*dx + dy*dy + dz*dz);
            voice_count++;
        }
    }

    if (voice_count > 0) {
        float avg_dist = total_dist / (float)voice_count;
        float room_scale = fminf(ctx->reverb.target_decay / 2.0f, 1.0f);
        if (ctx->reverb.target_decay <= 0.01f) room_scale = 0.5f;
        float distance_factor = 1.0f - expf(-avg_dist * 0.1f);
        float target_wet = 0.05f + 0.40f * distance_factor * (0.5f + 0.5f * room_scale);
        if (target_wet > 0.5f) target_wet = 0.5f;
        if (target_wet < 0.0f) target_wet = 0.0f;

        static float smooth_wet = 0.0f;
        smooth_wet += 0.05f * (target_wet - smooth_wet);
        ctx->reverb.wet = smooth_wet;
    }

    reverb_process(&ctx->reverb, buffer, frames, out_channels);

    /* ---- Master Soft-Knee Limiter ---- */
    int total_samples = frames * out_channels;
    float master_gain = 1.0f;
    float threshold = 0.8f;
    float knee = 0.2f;

    for (i = 0; i < total_samples; i++) {
        float x = buffer[i] * master_gain;
        float abs_x = fabsf(x);
        float compressed;

        if (abs_x < threshold) {
            compressed = x;
        } else if (abs_x < threshold + knee) {
            float t = (abs_x - threshold) / knee;
            float gain_reduction = 1.0f - (t * t * 0.5f);
            compressed = x * gain_reduction;
        } else {
            compressed = (x / abs_x) * (threshold + (knee * 0.5f));
        }

        buffer[i] = compressed;
    }
}