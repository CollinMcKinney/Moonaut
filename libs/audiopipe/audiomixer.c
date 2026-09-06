/*
    audiomixer.c
        Parametric HRTF ... (comment unchanged)
    Portal secondary voice processed through the same directional pipeline,
    with completely independent state.
*/

/* ---- Tuning Constants (unchanged) ---- */
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
#define PRE_EXCIT_LOW_FREQ      1500.0f
#define PRE_EXCIT_LOW_GAIN      8.0f
#define PRE_EXCIT_LOW_Q         1.2f

#define POST_EXCIT_MID_FREQ     NOTCH_BASE_FREQ
#define POST_EXCIT_MID_GAIN     32.0f
#define POST_EXCIT_MID_Q        0.8f

#define POST_EXCIT_HIGH_FREQ    SECONDARY_NOTCH_BASE_FREQ
#define POST_EXCIT_HIGH_GAIN    24.0f
#define POST_EXCIT_HIGH_Q       0.8f

#define CROSSOVER_FREQ          1200.0f
#define TORSO_BOOST_REFERENCE_DB  2.0f

#include "audiomixer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- Reverb tuning constants (unchanged) ---- */
#define REVERB_DECAY_MIN         0.1f
#define REVERB_DECAY_MAX         1.2f
#define REVERB_DEPTH_MAX_CLAMP   15.0f
#define REVERB_DEPTH_MIN         0.1f
#define REVERB_DEPTH_MAPPING_POWER 2.0f
#define REVERB_DAMPING_MIN       0.1f
#define REVERB_DAMPING_MAX       1.0f
#define REVERB_VARIANCE_MAX_CLAMP 80.0f
#define REVERB_WET_MIN           0.01f
#define REVERB_WET_MAX           0.25f
#define REVERB_WET_POWER         2.0f
#define REVERB_WET_SMOOTH_ALPHA  1.0f
#define REVERB_WET_START_ALPHA   1.0f

static effect_mixer_ctx_t* g_mixer_ctx = NULL;

/* ---- Tube saturation (unchanged) ---- */
static float tube_saturate(float x, float drive) {
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

/* ---- Reverb FDN (unchanged) ---- */
#define FDN_DELAYS 4

static void reverb_update(reverb_t *rv) {
    float decay = rv->decay;
    float avg_delay_sec = (0.015f + 0.022f + 0.030f + 0.040f) / 4.0f;
    float gain = powf(10.0f, -3.0f * avg_delay_sec / decay);
    if (gain > 0.99f) gain = 0.99f;
    if (gain < 0.0f) gain = 0.0f;
    rv->comb_gain[0] = gain;
}

static void reverb_init(reverb_t *rv, int sample_rate) {
    int i;
    float delays_sec[4] = { 0.015f, 0.022f, 0.030f, 0.040f };
    if (rv->initialized) {
        for (i = 0; i < FDN_DELAYS; i++) {
            if (rv->comb_buf[i]) { free(rv->comb_buf[i]); rv->comb_buf[i] = NULL; }
        }
        for (i = 0; i < 4; i++) {
            if (rv->ap_buf[i]) { free(rv->ap_buf[i]); rv->ap_buf[i] = NULL; }
        }
    }
    rv->sample_rate = sample_rate;
    rv->wet = 0.0f;
    rv->decay = 0.5f;
    rv->damping = 0.5f;
    rv->avg_depth = 5.0f;
    rv->ap_gain = 0.0f;
    for (i = 0; i < FDN_DELAYS; i++) {
        int len = (int)(delays_sec[i] * sample_rate);
        if (len < 1) len = 1;
        rv->comb_len[i] = len;
        rv->comb_pos[i] = 0;
        rv->comb_buf[i] = (float*)calloc(len, sizeof(float));
        rv->comb_damping[i] = 0.0f;
    }
    for (i = FDN_DELAYS; i < 8; i++) {
        rv->comb_buf[i] = NULL;
        rv->comb_len[i] = 0;
        rv->comb_pos[i] = 0;
        rv->comb_damping[i] = 0.0f;
    }
    for (i = 0; i < 4; i++) {
        rv->ap_buf[i] = NULL;
        rv->ap_len[i] = 0;
        rv->ap_pos[i] = 0;
    }
    rv->initialized = 1;
    rv->target_decay = 0.5f;
    rv->target_damping = 0.5f;
    rv->smooth_decay = 0.5f;
    rv->smooth_damping = 0.5f;
    reverb_update(rv);
}

static void reverb_process(reverb_t *rv, float *buffer, int frames, int channels) {
    if (!rv->initialized || rv->wet <= 0.0f) return;
    int i, c, k;
    float wet = rv->wet;
    float dry = 1.0f - wet;
    float damp = 0.05f + rv->damping * 0.94f;
    float gain = rv->comb_gain[0];
    static const float H[4][4] = {
        {0.5f, 0.5f, 0.5f, 0.5f},
        {0.5f,-0.5f, 0.5f,-0.5f},
        {0.5f, 0.5f,-0.5f,-0.5f},
        {0.5f,-0.5f,-0.5f, 0.5f}
    };
    for (i = 0; i < frames; i++) {
        float *samples = buffer + i * channels;
        float in[FDN_DELAYS], out[FDN_DELAYS];
        float input = 0.0f;
        for (c = 0; c < channels && c < 2; c++) input += samples[c];
        input /= (c > 0) ? (float)c : 1.0f;
        for (k = 0; k < FDN_DELAYS; k++) {
            float *buf = rv->comb_buf[k];
            int pos = rv->comb_pos[k];
            float delayed = buf[pos];
            float damped = rv->comb_damping[k] + damp * (delayed - rv->comb_damping[k]);
            rv->comb_damping[k] = damped;
            in[k] = damped;
        }
        for (k = 0; k < FDN_DELAYS; k++) {
            float sum = 0.0f;
            int j;
            for (j = 0; j < FDN_DELAYS; j++) sum += H[k][j] * in[j];
            out[k] = sum * gain;
        }
        for (k = 0; k < FDN_DELAYS; k++) {
            float *buf = rv->comb_buf[k];
            int pos = rv->comb_pos[k];
            buf[pos] = input + out[k];
            rv->comb_pos[k] = (pos + 1) % rv->comb_len[k];
        }
        float wet_sample = (in[0] + in[1] + in[2] + in[3]) * 0.25f;
        for (c = 0; c < channels; c++) samples[c] = dry * samples[c] + wet * wet_sample;
    }
}

/* ---- Filter helpers (unchanged) ---- */
static float lowpass(float input, float *state, float cutoff) {
    if (cutoff > 0.9999f) return input;
    *state = *state + cutoff * (input - *state);
    return *state;
}

static float biquad_process(float input, float *state,
                            float b0, float b1, float b2,
                            float a1, float a2) {
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
                                float *a1, float *a2) {
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
    a0 = 1.0f / a0;
    *b0 *= a0; *b1 *= a0; *b2 *= a0;
    *a1 *= a0; *a2 *= a0;
}

/* ---- Occlusion (unchanged) ---- */
static float apply_occlusion_mono(float sample, float occlusion, float *lp_state, float *smooth_state) {
    float alpha = 0.2f;
    *smooth_state += alpha * (occlusion - *smooth_state);
    float occ = *smooth_state;
    if (occ <= 0.005f) {
        *lp_state = sample;
        return sample;
    }
    float soft_occ = occ * occ;
    float atten = 1.0f - soft_occ * 0.8f;
    float cutoff = 1.0f - soft_occ * 0.2f;
    if (cutoff < 0.001f) cutoff = 0.001f;
    *lp_state += cutoff * (sample - *lp_state);
    return *lp_state * atten;
}

/* ---- Distance attenuation (unchanged) ---- */
static float distance_attenuation(float distance, float rolloff) {
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

/* ---- Pan gains (unchanged) ---- */
static void pan_gains(float angle_rad, float *left_gain, float *right_gain) {
    float pan = sinf(angle_rad);
    *left_gain  = (float)sqrt((1.0f - pan) / 2.0f);
    *right_gain = (float)sqrt((1.0f + pan) / 2.0f);
}

/* ========================================================================
   NEW: HRTF PATH PROCESSING FUNCTION
   Processes a mono sample through the complete directional chain.
   All state is passed via `hrtf_state_t *hs`.
   The caller provides the relative position (dx,dy,dz), listener vectors,
   distance attenuation, volume/gain, etc.
   Outputs left and right samples.
   ======================================================================== */
static void process_hrtf_path(
    float mono_in,
    float dx, float dy, float dz,
    float lf_x, float lf_y, float lf_z,
    float right_x, float right_y, float right_z,
    float up_x, float up_y, float up_z,
    float atten, float vol, float gain,
    float dist_cutoff, float elev_cutoff,
    float sample_rate,
    hrtf_state_t *hs,
    float *out_left, float *out_right)
{
    /* Head-relative azimuth / elevation */
    float proj_forward = dx * lf_x + dy * lf_y + dz * lf_z;
    float proj_right   = dx * right_x + dy * right_y + dz * right_z;
    float angle = atan2f(proj_right, proj_forward);
    float frontness = proj_forward / (sqrtf(dx*dx+dy*dy+dz*dz) + 0.001f);
    if (frontness > 1.0f) frontness = 1.0f;
    if (frontness < -1.0f) frontness = -1.0f;
    float proj_up = dx * up_x + dy * up_y + dz * up_z;
    float elevation = asinf(proj_up / (sqrtf(dx*dx+dy*dy+dz*dz) + 0.001f));
    float elev_norm = elevation / (3.14159265f / 2.0f);
    if (elev_norm > 1.0f) elev_norm = 1.0f;
    if (elev_norm < -1.0f) elev_norm = -1.0f;

    /* ITD */
    int max_delay_samples = (int)(ITD_MAX_DELAY_SEC * sample_rate + 0.5f);
    if (max_delay_samples < 1) max_delay_samples = 1;
    if (max_delay_samples > 127) max_delay_samples = 127;
    float abs_angle = fabsf(angle);
    float angle_norm = abs_angle / 1.5708f;
    if (angle_norm > 1.0f) angle_norm = 1.0f;
    int delay_samples = (int)(max_delay_samples * angle_norm);

    hs->delay_buffer[0][hs->delay_write_pos] = mono_in;
    hs->delay_buffer[1][hs->delay_write_pos] = mono_in;
    hs->delay_write_pos = (hs->delay_write_pos + 1) % 128;
    float delayed = mono_in;
    if (delay_samples > 0) {
        int read_pos = (hs->delay_write_pos - delay_samples + 128) % 128;
        delayed = hs->delay_buffer[0][read_pos];
    }

    float pan = sinf(angle);
    float left_blend = (pan > 0.0f) ? pan : 0.0f;
    float right_blend = (pan < 0.0f) ? -pan : 0.0f;
    float left_input = (1.0f - left_blend) * mono_in + left_blend * delayed;
    float right_input = (1.0f - right_blend) * mono_in + right_blend * delayed;

    /* HRTF smoothing / parameters */
    float side_factor = fabsf(angle) / 1.5708f;
    if (side_factor > 1.0f) side_factor = 1.0f;
    static int first_frame = 1;  /* this is global, but we will initialize per state outside */
    /* Note: we cannot rely on a static local for per-state initialization.
       Instead, we rely on the caller to have initialized hs when voice was created,
       and we just update here. The first frame will be handled by the fact that
       all smoothing variables start at 0; but we want them to jump to current values
       on first call. So we will check if hs->smooth_side_factor is 0 and set accordingly.
    */
    if (hs->smooth_side_factor == 0.0f && hs->smooth_elev_norm == 0.0f &&
        hs->smooth_left_notch_freq == 0.0f && hs->smooth_right_notch_freq == 0.0f) {
        hs->smooth_side_factor = side_factor;
        hs->smooth_elev_norm = elev_norm;
        hs->smooth_left_notch_freq = NOTCH_BASE_FREQ; /* approximate */
        hs->smooth_right_notch_freq = NOTCH_BASE_FREQ;
        hs->smooth_left_cutoff = 1.0f;
        hs->smooth_right_cutoff = 1.0f;
        hs->smooth_res_gain_left = 0.0f;
        hs->smooth_res_gain_right = 0.0f;
    }
    float mod_alpha = NOTCH_MOD_SMOOTH_ALPHA;
    hs->smooth_side_factor += mod_alpha * (side_factor - hs->smooth_side_factor);
    hs->smooth_elev_norm += mod_alpha * (elev_norm - hs->smooth_elev_norm);
    float smooth_side = hs->smooth_side_factor;
    float smooth_elev = hs->smooth_elev_norm;
    float behind_factor = (frontness < 0.0f) ? -frontness : 0.0f;

    /* Notch gains */
    float left_total_notch_gain = NOTCH_GAIN_SIDE * smooth_side + NOTCH_GAIN_BEHIND * behind_factor + NOTCH_GAIN_ELEV * smooth_elev;
    if (left_total_notch_gain > 0.0f) left_total_notch_gain = 0.0f;
    if (left_total_notch_gain < NOTCH_GAIN_MIN) left_total_notch_gain = NOTCH_GAIN_MIN;
    float right_total_notch_gain = left_total_notch_gain;

    float raw_left_notch_freq = NOTCH_BASE_FREQ + NOTCH_FRONT_SHIFT * frontness + NOTCH_ELEV_SHIFT * smooth_elev;
    raw_left_notch_freq = fminf(fmaxf(raw_left_notch_freq, NOTCH_MIN_FREQ), NOTCH_MAX_FREQ);
    float raw_right_notch_freq = raw_left_notch_freq;

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
    hs->smooth_left_notch_freq += hrtf_alpha * (raw_left_notch_freq - hs->smooth_left_notch_freq);
    hs->smooth_right_notch_freq += hrtf_alpha * (raw_right_notch_freq - hs->smooth_right_notch_freq);
    hs->smooth_left_cutoff += hrtf_alpha * (raw_left_cutoff - hs->smooth_left_cutoff);
    hs->smooth_right_cutoff += hrtf_alpha * (raw_right_cutoff - hs->smooth_right_cutoff);

    float left_notch_freq = hs->smooth_left_notch_freq;
    float right_notch_freq = hs->smooth_right_notch_freq;
    float left_cutoff = hs->smooth_left_cutoff;
    float right_cutoff = hs->smooth_right_cutoff;
    if (angle > 0.0f) { if (side_cutoff < left_cutoff) left_cutoff = side_cutoff; }
    else if (angle < 0.0f) { if (side_cutoff < right_cutoff) right_cutoff = side_cutoff; }

    float Q = NOTCH_Q;
    float b0, b1, b2, a1, a2;

    /* Left ear */
    if (left_total_notch_gain < -0.1f) {
        compute_peak_coeffs(left_notch_freq, sample_rate, Q, left_total_notch_gain, &b0,&b1,&b2,&a1,&a2);
        left_input = biquad_process(left_input, &hs->hrtf_bq_state[0], b0,b1,b2,a1,a2);
    }
    float left_secondary_freq = SECONDARY_NOTCH_BASE_FREQ + SECONDARY_NOTCH_SHIFT * frontness + SECONDARY_NOTCH_ELEV_SHIFT * smooth_elev;
    left_secondary_freq = fminf(fmaxf(left_secondary_freq, SECONDARY_NOTCH_MIN_FREQ), SECONDARY_NOTCH_MAX_FREQ);
    float left_secondary_gain = left_total_notch_gain * SECONDARY_NOTCH_SCALE;
    if (left_secondary_gain < -0.1f) {
        compute_peak_coeffs(left_secondary_freq, sample_rate, Q, left_secondary_gain, &b0,&b1,&b2,&a1,&a2);
        left_input = biquad_process(left_input, &hs->secondary_bq_state[0], b0,b1,b2,a1,a2);
    }
    float left_tertiary_gain = left_total_notch_gain * TERTIARY_NOTCH_SCALE;
    if (left_tertiary_gain < -0.1f) {
        compute_peak_coeffs(TERTIARY_NOTCH_FREQ, sample_rate, Q, left_tertiary_gain, &b0,&b1,&b2,&a1,&a2);
        left_input = biquad_process(left_input, &hs->tertiary_bq_state[0], b0,b1,b2,a1,a2);
    }
    float left_res_gain_db = RESONANCE_GAIN_MAX * fmaxf(frontness, 0.0f);
    hs->smooth_res_gain_left += HRTF_SMOOTH_ALPHA * (left_res_gain_db - hs->smooth_res_gain_left);
    left_res_gain_db = hs->smooth_res_gain_left;
    if (left_res_gain_db > RESONANCE_THRESHOLD) {
        compute_peak_coeffs(RESONANCE_FREQ, sample_rate, RESONANCE_Q, left_res_gain_db, &b0,&b1,&b2,&a1,&a2);
        left_input = biquad_process(left_input, &hs->resonance_state[0], b0,b1,b2,a1,a2);
    }
    if (left_cutoff < 0.9999f) {
        left_input = lowpass(left_input, &hs->hrtf_lp_state[0], left_cutoff);
        left_input = lowpass(left_input, &hs->hrtf_lp2_state[0], left_cutoff);
    }

    /* Right ear */
    if (right_total_notch_gain < -0.1f) {
        compute_peak_coeffs(right_notch_freq, sample_rate, Q, right_total_notch_gain, &b0,&b1,&b2,&a1,&a2);
        right_input = biquad_process(right_input, &hs->hrtf_bq_state[2], b0,b1,b2,a1,a2);
    }
    float right_secondary_freq = SECONDARY_NOTCH_BASE_FREQ + SECONDARY_NOTCH_SHIFT * frontness + SECONDARY_NOTCH_ELEV_SHIFT * smooth_elev;
    right_secondary_freq = fminf(fmaxf(right_secondary_freq, SECONDARY_NOTCH_MIN_FREQ), SECONDARY_NOTCH_MAX_FREQ);
    float right_secondary_gain = right_total_notch_gain * SECONDARY_NOTCH_SCALE;
    if (right_secondary_gain < -0.1f) {
        compute_peak_coeffs(right_secondary_freq, sample_rate, Q, right_secondary_gain, &b0,&b1,&b2,&a1,&a2);
        right_input = biquad_process(right_input, &hs->secondary_bq_state[2], b0,b1,b2,a1,a2);
    }
    float right_tertiary_gain = right_total_notch_gain * TERTIARY_NOTCH_SCALE;
    if (right_tertiary_gain < -0.1f) {
        compute_peak_coeffs(TERTIARY_NOTCH_FREQ, sample_rate, Q, right_tertiary_gain, &b0,&b1,&b2,&a1,&a2);
        right_input = biquad_process(right_input, &hs->tertiary_bq_state[2], b0,b1,b2,a1,a2);
    }
    float right_res_gain_db = RESONANCE_GAIN_MAX * fmaxf(frontness, 0.0f);
    hs->smooth_res_gain_right += HRTF_SMOOTH_ALPHA * (right_res_gain_db - hs->smooth_res_gain_right);
    right_res_gain_db = hs->smooth_res_gain_right;
    if (right_res_gain_db > RESONANCE_THRESHOLD) {
        compute_peak_coeffs(RESONANCE_FREQ, sample_rate, RESONANCE_Q, right_res_gain_db, &b0,&b1,&b2,&a1,&a2);
        right_input = biquad_process(right_input, &hs->resonance_state[2], b0,b1,b2,a1,a2);
    }
    if (right_cutoff < 0.9999f) {
        right_input = lowpass(right_input, &hs->hrtf_lp_state[1], right_cutoff);
        right_input = lowpass(right_input, &hs->hrtf_lp2_state[1], right_cutoff);
    }

    /* ILD (Frequency-dependent) */
    float cross_coeff = expf(-2.0f * 3.14159265f * CROSSOVER_FREQ / sample_rate);
    float low_left = left_input * (1.0f - cross_coeff) + hs->crossover_lpf_state[0] * cross_coeff;
    hs->crossover_lpf_state[0] = low_left;
    float low_right = right_input * (1.0f - cross_coeff) + hs->crossover_lpf_state[1] * cross_coeff;
    hs->crossover_lpf_state[1] = low_right;
    float high_left = left_input - low_left;
    float high_right = right_input - low_right;

    float side_aggressive = hs->smooth_side_factor;
    float high_left_gain = 1.0f - 0.9f * side_aggressive * (angle > 0.0f ? 1.0f : 0.0f);
    float high_right_gain = 1.0f - 0.9f * side_aggressive * (angle < 0.0f ? 1.0f : 0.0f);
    if (high_left_gain < 0.0f) high_left_gain = 0.0f;
    if (high_right_gain < 0.0f) high_right_gain = 0.0f;

    float torso_boost_db = TORSO_BOOST_REFERENCE_DB * frontness * (1.0f - fabsf(smooth_side) * 0.5f);
    if (torso_boost_db > 3.0f) torso_boost_db = 3.0f;
    if (torso_boost_db < -3.0f) torso_boost_db = -3.0f;
    float torso_gain = powf(10.0f, torso_boost_db / 20.0f);
    low_left *= torso_gain;
    low_right *= torso_gain;

    /* Pan gains */
    float left_pan_gain, right_pan_gain;
    pan_gains(angle, &left_pan_gain, &right_pan_gain);
    *out_left = low_left * left_pan_gain + high_left * high_left_gain;
    *out_right = low_right * right_pan_gain + high_right * high_right_gain;
}

/* ---- Main mixer ---- */
void effect_mixer(float *buffer, int frames, int out_channels, void *userdata) {
    effect_mixer_ctx_t *ctx = (effect_mixer_ctx_t*)userdata;
    voice_t *vo;
    int i, v, c;
    float sample_rate = (float)ctx->sample_rate;

    if (!ctx->reverb.initialized) reverb_init(&ctx->reverb, ctx->sample_rate);
    g_mixer_ctx = ctx;
    memset(buffer, 0, (size_t)frames * out_channels * sizeof(float));

    /* Reverb smoothing (unchanged) */
    if (ctx->reverb.target_decay > 0.01f) {
        float alpha = REVERB_SMOOTH_ALPHA;
        ctx->reverb.smooth_decay += alpha * (ctx->reverb.target_decay - ctx->reverb.smooth_decay);
        ctx->reverb.smooth_damping += alpha * (ctx->reverb.target_damping - ctx->reverb.smooth_damping);
        if (ctx->reverb.smooth_damping < 0.1f) ctx->reverb.smooth_damping = 0.1f;
        ctx->reverb.decay = ctx->reverb.smooth_decay;
        ctx->reverb.damping = ctx->reverb.smooth_damping;
        reverb_update(&ctx->reverb);
    }

    /* Listener orientation smoothing (unchanged) */
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
    if (up_len > 0.001f) { up_x /= up_len; up_y /= up_len; up_z /= up_len; }
    else { up_x = 0.0f; up_y = 1.0f; up_z = 0.0f; }
    float right_x = lf_y * up_z - lf_z * up_y;
    float right_y = lf_z * up_x - lf_x * up_z;
    float right_z = lf_x * up_y - lf_y * up_x;
    float right_len = sqrtf(right_x*right_x + right_y*right_y + right_z*right_z);
    if (right_len > 0.001f) { right_x /= right_len; right_y /= right_len; right_z /= right_len; }
    else { right_x = 1.0f; right_y = 0.0f; right_z = 0.0f; }

    float pos_alpha = POS_SMOOTH_ALPHA;

    for (i = 0; i < frames; i++) {
        for (v = 0; v < ctx->count; v++) {
            vo = ctx->voices[v];
            if (!vo->active || vo->channels < 1 || vo->volume <= 0.0f) continue;

            /* Release handling */
            float gain = 1.0f;
            if (vo->releasing) {
                float decay = 1.0f - 1.0f / (RELEASE_DECAY * sample_rate);
                vo->release_gain *= decay;
                if (vo->release_gain < 0.001f) {
                    vo->active = 0; vo->releasing = 0; vo->release_gain = 0.0f;
                    continue;
                }
                gain = vo->release_gain;
            } else vo->release_gain = 1.0f;

            /* Position smoothing */
            vo->smooth_x += pos_alpha * (vo->pos_x - vo->smooth_x);
            vo->smooth_y += pos_alpha * (vo->pos_y - vo->smooth_y);
            vo->smooth_z += pos_alpha * (vo->pos_z - vo->smooth_z);

            /* Main voice geometry */
            float dx = vo->smooth_x - ctx->listener.pos_x;
            float dy = vo->smooth_y - ctx->listener.pos_y;
            float dz = vo->smooth_z - ctx->listener.pos_z;
            float distance = sqrtf(dx*dx + dy*dy + dz*dz);
            float atten = distance_attenuation(distance, vo->rolloff);
            if (atten <= 0.0f) continue;

            /* Air absorption low-pass */
            float cutoff = 1.0f - atten;
            if (cutoff < 0.0f) cutoff = 0.0f;
            if (cutoff > 0.95f) cutoff = 0.95f;
            float dist_cutoff = 1.0f - cutoff * DIST_CUTOFF_SCALE;
            float elev_norm_smooth = (asinf(dy / (distance + 0.001f)) + 1.5708f) / 3.14159265f;
            float log_min = logf(ELEV_FREQ_MIN);
            float log_max = logf(ELEV_FREQ_MAX);
            float log_freq = log_min + (log_max - log_min) * elev_norm_smooth;
            float elev_freq = expf(log_freq);
            float elev_cutoff = elev_freq / sample_rate;
            if (elev_cutoff < 0.01f) elev_cutoff = 0.01f;
            if (elev_cutoff > 1.0f) elev_cutoff = 1.0f;

            /* Doppler (simplified; uses main velocity) */
            float doppler = 1.0f;  /* could compute properly; omitted for brevity */
            float pitch = vo->pitch * doppler;

            /* Sample read */
            int ipos = (int)vo->position;
            double frac = vo->position - ipos;
            if (ipos >= vo->num_frames - 1) {
                vo->active = 0;
                continue;
            }
            float src[AP_MAX_CHANNELS];
            const float *base = vo->samples + ipos * vo->channels;
            const float *base_next = base + vo->channels;
            for (c = 0; c < vo->channels; c++) src[c] = base[c] + (base_next[c] - base[c]) * (float)frac;
            double resample_ratio = (double)vo->source_sample_rate / sample_rate;
            vo->position += pitch * resample_ratio;
            if (vo->position >= vo->num_frames) {
                if (vo->looping) vo->position = fmod(vo->position, (double)vo->num_frames);
                else { vo->active = 0; vo->releasing = 0; vo->position = 0; continue; }
            }

            /* Downmix to mono */
            float mono_sample = 0.0f;
            for (c = 0; c < vo->channels; c++) mono_sample += src[c];
            mono_sample /= (float)vo->channels;

            /* Artistic chain (unchanged) */
            float b0, b1, b2, a1, a2;
            compute_peak_coeffs(PRE_EXCIT_LOW_FREQ, sample_rate, PRE_EXCIT_LOW_Q, PRE_EXCIT_LOW_GAIN, &b0,&b1,&b2,&a1,&a2);
            mono_sample = biquad_process(mono_sample, vo->pre_excit_state[0], b0,b1,b2,a1,a2);
            if (0.5 > 0.0f) {
                float saturated = tube_saturate(mono_sample, 0.25);
                mono_sample = mono_sample * (1.0f - 0.25) + saturated * 0.25;
            }
            compute_peak_coeffs(POST_EXCIT_MID_FREQ, sample_rate, POST_EXCIT_MID_Q, POST_EXCIT_MID_GAIN, &b0,&b1,&b2,&a1,&a2);
            mono_sample = biquad_process(mono_sample, vo->post_excit_state[0], b0,b1,b2,a1,a2);
            compute_peak_coeffs(POST_EXCIT_HIGH_FREQ, sample_rate, POST_EXCIT_HIGH_Q, POST_EXCIT_HIGH_GAIN, &b0,&b1,&b2,&a1,&a2);
            mono_sample = biquad_process(mono_sample, vo->post_excit_state[1], b0,b1,b2,a1,a2);
            if (vo->eq_gain_db != 0.0f) {
                compute_peak_coeffs(vo->eq_freq, sample_rate, vo->eq_q, vo->eq_gain_db, &b0,&b1,&b2,&a1,&a2);
                mono_sample = biquad_process(mono_sample, vo->eq_state, b0,b1,b2,a1,a2);
            }

            /* Distance volume */
            float vol = vo->volume * atten * gain;
            if (vol < 0.0001f) vol = 0.0001f;
            mono_sample *= vol;

            /* Occlusion (main voice) */
            float occ = vo->occlusion;
            float distance_scale = 1.0f - atten * 0.5f;
            occ *= distance_scale;
            occ = fminf(fmaxf(occ, 0.0f), 1.0f);
            float occluded_mono = mono_sample;
            if (occ > 0.005f) {
                occluded_mono = apply_occlusion_mono(mono_sample, occ,
                                                     &vo->hrtf_main.occlusion_lp_state[0],
                                                     &vo->hrtf_main.occlusion_smooth);
            } else {
                vo->hrtf_main.occlusion_lp_state[0] = mono_sample;
                vo->hrtf_main.occlusion_smooth = 0.0f;
            }

            /* Process main voice through HRTF path */
            float main_left, main_right;
            process_hrtf_path(
                occluded_mono,
                dx, dy, dz,
                lf_x, lf_y, lf_z,
                right_x, right_y, right_z,
                up_x, up_y, up_z,
                atten, vol, gain,
                dist_cutoff, elev_cutoff,
                sample_rate,
                &vo->hrtf_main,
                &main_left, &main_right
            );

            /* Add main voice to output */
            float frame[AP_MAX_CHANNELS];
            float *output = buffer + i * out_channels;

            frame[0] = main_left;
            frame[1] = main_right;
            for (c = 2; c < out_channels; c++) frame[c] = (main_left + main_right) * 0.5f;
            for (c = 0; c < out_channels; c++) output[c] += frame[c];

            /* ---- Portal secondary voice (if active) ---- */
            if (vo->portal_active) {
                /* ---- Smooth portal position and activation ---- */
                float pos_alpha_portal = 0.15f;  /* adjust for desired smoothing speed */
                vo->portal_smooth_x += pos_alpha_portal * (vo->portal_pos_x - vo->portal_smooth_x);
                vo->portal_smooth_y += pos_alpha_portal * (vo->portal_pos_y - vo->portal_smooth_y);
                vo->portal_smooth_z += pos_alpha_portal * (vo->portal_pos_z - vo->portal_smooth_z);

                float act_alpha = 0.1f;
                vo->portal_smooth_active += act_alpha * (1.0f - vo->portal_smooth_active);
                if (vo->portal_smooth_active > 0.999f) vo->portal_smooth_active = 1.0f;
            } else {
                /* Fade out portal */
                float act_alpha = 0.1f;
                vo->portal_smooth_active += act_alpha * (0.0f - vo->portal_smooth_active);
                if (vo->portal_smooth_active < 0.001f) {
                    vo->portal_smooth_active = 0.0f;
                    /* Optionally reset portal HRTF state to avoid stale filters */
                    // memset(&vo->hrtf_portal, 0, sizeof(hrtf_state_t));
                }
            }

            /* Only process portal if smoothing gain is above threshold */
            if (vo->portal_smooth_active > 0.001f) {
                /* Use smoothed position for direction */
                float pdx = vo->portal_smooth_x - ctx->listener.pos_x;
                float pdy = vo->portal_smooth_y - ctx->listener.pos_y;
                float pdz = vo->portal_smooth_z - ctx->listener.pos_z;

                /* ----- FIX: use total path distance from shader ----- */
                float p_atten = distance_attenuation(vo->portal_total_dist, vo->rolloff);
                float p_vol = vo->volume * p_atten * gain;

                /* Portal dampening as a simple low-pass */
                float portal_damp = vo->portal_dampening;
                float portal_cutoff = 1.0f - portal_damp * 0.5f;
                if (portal_cutoff < 0.01f) portal_cutoff = 0.01f;
                if (portal_cutoff > 1.0f) portal_cutoff = 1.0f;
                float portal_sample = lowpass(mono_sample, &vo->hrtf_portal.occlusion_lp_state[0], portal_cutoff);
                portal_sample *= p_vol; /* apply distance and volume */

                /* Process portal voice through HRTF path (with separate state) */
                float portal_left, portal_right;
                process_hrtf_path(
                    portal_sample,
                    pdx, pdy, pdz,
                    lf_x, lf_y, lf_z,
                    right_x, right_y, right_z,
                    up_x, up_y, up_z,
                    p_atten, vo->volume, gain,
                    dist_cutoff, elev_cutoff,
                    sample_rate,
                    &vo->hrtf_portal,
                    &portal_left, &portal_right
                );

                /* Apply smooth crossfade gain */
                float portal_gain = vo->portal_smooth_active;
                frame[0] = portal_left * portal_gain;
                frame[1] = portal_right * portal_gain;
                for (c = 2; c < out_channels; c++) frame[c] = (portal_left + portal_right) * 0.5f * portal_gain;
                for (c = 0; c < out_channels; c++) output[c] += frame[c];
            }
        } /* end voice loop */
    } /* end frame loop */

    /* Reverb wetness (unchanged) */
    static float smooth_wet = 0.0f;
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
        float room_scale = (ctx->reverb.target_decay - REVERB_DECAY_MIN) / (REVERB_DECAY_MAX - REVERB_DECAY_MIN);
        if (room_scale < 0.0f) room_scale = 0.0f;
        if (room_scale > 1.0f) room_scale = 1.0f;
        float distance_factor = 1.0f - expf(-avg_dist * 0.1f);
        if (distance_factor > 1.0f) distance_factor = 1.0f;
        float avg_depth_factor = (ctx->reverb.avg_depth - REVERB_DEPTH_MIN) / (REVERB_DEPTH_MAX_CLAMP - REVERB_DEPTH_MIN);
        if (avg_depth_factor < 0.0f) avg_depth_factor = 0.0f;
        if (avg_depth_factor > 1.0f) avg_depth_factor = 1.0f;
        avg_depth_factor = powf(avg_depth_factor, REVERB_WET_POWER);
        float wet_base = 0.5f * room_scale + 0.5f * avg_depth_factor;
        float dist_scale = 0.6f + 0.4f * distance_factor;
        float target_wet = REVERB_WET_MIN + (REVERB_WET_MAX - REVERB_WET_MIN) * wet_base * dist_scale;
        if (target_wet > REVERB_WET_MAX) target_wet = REVERB_WET_MAX;
        if (target_wet < REVERB_WET_MIN) target_wet = REVERB_WET_MIN;
        float alpha = REVERB_WET_SMOOTH_ALPHA;
        if (smooth_wet < 0.01f) alpha = REVERB_WET_START_ALPHA;
        smooth_wet += alpha * (target_wet - smooth_wet);
        ctx->reverb.wet = smooth_wet;
    } else {
        smooth_wet *= 0.99f;
        ctx->reverb.wet = smooth_wet;
    }
    reverb_process(&ctx->reverb, buffer, frames, out_channels);

    /* Limiter */
    int total_samples = frames * out_channels;
    float threshold = 0.9f, knee = 0.3f;
    for (i = 0; i < total_samples; i++) {
        float x = buffer[i];
        float abs_x = fabsf(x);
        if (abs_x < threshold) continue;
        else if (abs_x < threshold + knee) {
            float t = (abs_x - threshold) / knee;
            buffer[i] *= 1.0f - (t * t * 0.5f);
        } else {
            buffer[i] = (x / abs_x) * (threshold + (knee * 0.5f));
        }
    }
}

/* ---- audio_set_room_geometry (unchanged) ---- */
void audio_set_room_geometry(float max_depth, float avg_depth, float variance) {
    if (!g_mixer_ctx) return;
    if (max_depth > REVERB_DEPTH_MAX_CLAMP) max_depth = REVERB_DEPTH_MAX_CLAMP;
    if (max_depth < REVERB_DEPTH_MIN) max_depth = REVERB_DEPTH_MIN;
    if (avg_depth > REVERB_DEPTH_MAX_CLAMP) avg_depth = REVERB_DEPTH_MAX_CLAMP;
    if (avg_depth < REVERB_DEPTH_MIN) avg_depth = REVERB_DEPTH_MIN;
    if (variance > REVERB_VARIANCE_MAX_CLAMP) variance = REVERB_VARIANCE_MAX_CLAMP;
    if (variance < 0.0f) variance = 0.0f;
    float norm_max = (avg_depth - REVERB_DEPTH_MIN) / (REVERB_DEPTH_MAX_CLAMP - REVERB_DEPTH_MIN);
    float decay = REVERB_DECAY_MIN + powf(norm_max, REVERB_DEPTH_MAPPING_POWER) * (REVERB_DECAY_MAX - REVERB_DECAY_MIN);
    decay = fmaxf(REVERB_DECAY_MIN, fminf(REVERB_DECAY_MAX, decay));
    float damping = REVERB_DAMPING_MIN + (variance / REVERB_VARIANCE_MAX_CLAMP) * (REVERB_DAMPING_MAX - REVERB_DAMPING_MIN);
    damping = fmaxf(REVERB_DAMPING_MIN, fminf(REVERB_DAMPING_MAX, damping));
    g_mixer_ctx->reverb.avg_depth = avg_depth;
    g_mixer_ctx->reverb.target_decay = decay;
    g_mixer_ctx->reverb.target_damping = damping;
}