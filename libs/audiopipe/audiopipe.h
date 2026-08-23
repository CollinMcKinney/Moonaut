/*
  audiopipe.h - Minimal cross‑platform audio output (ANSI C89)
  =============================================================
  This header declares the public API of audiopipe, a tiny pipe from
  your mixer to the speakers. You fully own mixing, scheduling, and
  threading - this library only handles platform audio output.

  Supported platforms:
    Windows   (WASAPI)  - link with ole32.lib
    macOS     (CoreAudio / AudioUnit) - link with CoreAudio.framework & AudioUnit.framework
    Linux     (ALSA)    - link with -lasound

  Usage:
    #include "audiopipe.h"
    // call ap_init(), then ap_start(), then call ap_update() regularly
    // from your own audio thread.
*/

#ifndef AUDIOPIPE_H
#define AUDIOPIPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mixer callback - called by ap_update() on YOUR thread.
   buffer   : interleaved 32‑bit float samples (L0,R0, L1,R1, ...)
   frames   : number of sample frames (per channel)
   channels : channel count (1 to 8, or any value you pass to ap_init)
   userdata : the pointer you passed to ap_init() */
typedef void (*ap_mix_cb)(float *buffer, int frames, int channels, void *userdata);

/* Initialise the audio output device.
   sample_rate   : e.g. 44100
   channels      : 1 (mono), 2 (stereo), up to 8 (7.1 surround)
   buffer_frames : desired latency in frames (e.g. 256 gives ~5.8 ms @44100 Hz)
   mix_cb        : your mixer function
   userdata      : opaque pointer passed to the mixer
   Returns 0 on success, non‑zero on failure. */
int  ap_init(int sample_rate, int channels, int buffer_frames,
             ap_mix_cb mix_cb, void *userdata);

/* Start playing. Usually called once after ap_init(). */
void ap_start(void);

/* Stop (pause) the audio stream. */
void ap_stop(void);

/* Feed new samples. Call this regularly from your audio thread/job.
   If the device needs data, your mixer callback will be invoked. */
void ap_update(void);

/* Release the audio device. No more calls to ap_update() afterwards. */
void ap_shutdown(void);

/* New: Returns the actual sample rate the device is using.
   On WASAPI this may differ from the requested rate.
   Returns 0 if not yet initialized. */
int ap_get_actual_sample_rate(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIOPIPE_H */