/*
  wavloader.h - Simple WAV file loader (PCM 8/16/24/32-bit and 32-bit float).
  Declares the load_wav function and its helper.
*/

#ifndef WAVLOADER_H
#define WAVLOADER_H

/* Load a WAV file, returning interleaved float samples.
   Returns NULL on failure. The caller must free the returned pointer.
   Parameters:
     filename        - path to the .wav file
     out_sample_rate - set to the sample rate (Hz)
     out_channels    - set to the number of channels
     out_num_frames  - set to the total number of frames (samples per channel)
*/
float* load_wav(const char *filename, int *out_sample_rate,
                int *out_channels, int *out_num_frames);

#endif /* WAVLOADER_H */