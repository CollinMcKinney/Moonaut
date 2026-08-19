/*
  wavloader.c – Implementation of the WAV loader (pure ANSI C89).
*/

#include "wavloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: read little‑endian integer, up to 4 bytes */
static int read_le_int(FILE *fp, int bytes)
{
    unsigned char buf[4] = {0};
    int val = 0;
    int i;

    if (fread(buf, 1, bytes, fp) != (size_t)bytes)
        return 0;

    for (i = 0; i < bytes; i++)
        val |= buf[i] << (i * 8);

    /* sign extend if needed (for 24‑bit) */
    if (bytes == 3 && (val & 0x800000))
        val |= 0xFF000000;

    return val;
}

float* load_wav(const char *filename, int *out_sample_rate,
                int *out_channels, int *out_num_frames)
{
    FILE *fp;
    unsigned char header[44];
    int sample_rate, channels, bits_per_sample, block_align;
    int data_size, format;
    float *float_data;
    int num_frames, i, c;
    int bytes_per_sample, is_float;
    int expected_frame_size, frame_size;
    int data_pos;
    unsigned char chunk[8];
    int chunk_size;
    float *src;

    /* ---- initialise ---- */
    float_data = NULL;
    is_float = 0;
    data_size = 0;
    data_pos = 36;

    printf("Attempting to open: %s\n", filename);
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen failed");
        return NULL;
    }

    if (fread(header, 1, 44, fp) != 44) {
        printf("Failed to read 44‑byte header\n");
        fclose(fp);
        return NULL;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        printf("Not a valid WAV file\n");
        fclose(fp);
        return NULL;
    }

    if (memcmp(header + 12, "fmt ", 4) != 0) {
        printf("Missing fmt chunk\n");
        fclose(fp);
        return NULL;
    }

    format = header[20] | (header[21] << 8);
    channels = header[22] | (header[23] << 8);
    sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    bits_per_sample = header[34] | (header[35] << 8);
    block_align = header[32] | (header[33] << 8);

    printf("WAV info: %d Hz, %d ch, %d bits, block_align=%d, format=0x%04X\n",
           sample_rate, channels, bits_per_sample, block_align, format);

    if (format == 3) {
        if (bits_per_sample != 32) {
            printf("Float format requires 32 bits\n");
            fclose(fp);
            return NULL;
        }
        is_float = 1;
    } else if (format == 1) {
        if (bits_per_sample != 8 && bits_per_sample != 16 &&
            bits_per_sample != 24 && bits_per_sample != 32) {
            printf("Unsupported PCM bit depth: %d\n", bits_per_sample);
            fclose(fp);
            return NULL;
        }
    } else {
        printf("Unsupported format 0x%04X\n", format);
        fclose(fp);
        return NULL;
    }

    /* ---- Find data chunk ---- */
    while (1) {
        if (fseek(fp, data_pos, SEEK_SET) != 0) break;
        if (fread(chunk, 1, 8, fp) != 8) break;
        if (memcmp(chunk, "data", 4) == 0) {
            data_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
            data_pos += 8;
            break;
        }
        chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
        data_pos += 8 + chunk_size;
        if (data_pos >= 44) break;
    }

    if (data_size <= 0) {
        printf("Data chunk not found or empty\n");
        fclose(fp);
        return NULL;
    }

    bytes_per_sample = bits_per_sample / 8;
    expected_frame_size = channels * bytes_per_sample;
    frame_size = (block_align > expected_frame_size) ? block_align : expected_frame_size;
    num_frames = data_size / frame_size;

    float_data = (float*)malloc((size_t)num_frames * channels * sizeof(float));
    if (!float_data) {
        printf("malloc failed\n");
        fclose(fp);
        return NULL;
    }

    fseek(fp, data_pos, SEEK_SET);

    if (is_float) {
        src = (float*)malloc(data_size);
        if (!src) {
            free(float_data);
            fclose(fp);
            return NULL;
        }
        fread(src, 1, data_size, fp);
        memcpy(float_data, src, data_size);
        free(src);
    } else {
        for (i = 0; i < num_frames; i++) {
            for (c = 0; c < channels; c++) {
                int val;
                float max_pos;

                if (bits_per_sample == 8) {
                    unsigned char u;
                    fread(&u, 1, 1, fp);
                    val = (int)u - 128;
                } else if (bits_per_sample == 16) {
                    short s;
                    fread(&s, 2, 1, fp);
                    val = s;
                } else if (bits_per_sample == 24) {
                    unsigned char b[3];
                    fread(b, 1, 3, fp);
                    val = b[0] | (b[1] << 8) | (b[2] << 16);
                    if (val & 0x800000) val |= 0xFF000000;
                } else { /* bits_per_sample == 32 */
                    int s;
                    fread(&s, 4, 1, fp);
                    val = s;
                }

                max_pos = (bits_per_sample == 8) ? 127.0f : (float)((1 << (bits_per_sample - 1)) - 1);
                float_data[i * channels + c] = (float)val / max_pos;
                if (float_data[i * channels + c] < -1.0f)
                    float_data[i * channels + c] = -1.0f;
                if (float_data[i * channels + c] > 1.0f)
                    float_data[i * channels + c] = 1.0f;
            }
            if (block_align > expected_frame_size)
                fseek(fp, block_align - expected_frame_size, SEEK_CUR);
        }
    }

    fclose(fp);
    *out_sample_rate = sample_rate;
    *out_channels = channels;
    *out_num_frames = num_frames;

    printf("Loaded %d frames, %.1f seconds\n", num_frames, (float)num_frames / sample_rate);
    return float_data;
}