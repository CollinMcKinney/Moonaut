/*
  audiopipe.c - Minimal cross‑platform audio output.
  Windows: WASAPI (low‑latency, float) - with mix‑format fallback
  Linux:   ALSA (uses -lasound)
  macOS:   CoreAudio (uses -framework CoreAudio -framework AudioUnit)
*/

#include "audiopipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Platform detection                                                 */
/* ------------------------------------------------------------------ */
#if defined(_WIN32)
#  define AP_WINDOWS 1
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_MAC
#    define AP_MACOS 1
#  endif
#elif defined(__linux__) || defined(__unix__)
#  define AP_LINUX 1
#else
#  error "audiopipe: unsupported platform. Add an #ifdef block for your OS."
#endif

/* ------------------------------------------------------------------ */
/* Common helpers                                                      */
/* ------------------------------------------------------------------ */
static void *ap_malloc(size_t sz) { return malloc(sz); }
static void  ap_free(void *p)     { free(p); }

/* ------------------------------------------------------------------ */
/* Windows - WASAPI (diagnostic + fallback)                           */
/* ------------------------------------------------------------------ */
#ifdef AP_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <objbase.h>

/* ---- Manual GUID definitions (no external uuid.lib) ---- */
static const GUID IID_IMMDeviceEnumerator = {
    0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}
};
static const GUID CLSID_MMDeviceEnumerator = {
    0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}
};
static const GUID IID_IAudioClient = {
    0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}
};
static const GUID IID_IAudioRenderClient = {
    0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}
};
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

#ifndef SPEAKER_MONO
#define SPEAKER_MONO             0x00000004
#endif
#ifndef SPEAKER_STEREO
#define SPEAKER_STEREO           0x00000003
#endif
#ifndef SPEAKER_QUAD
#define SPEAKER_QUAD             0x00000033
#endif
#ifndef SPEAKER_5POINT1
#define SPEAKER_5POINT1          0x0000003F
#endif
#ifndef SPEAKER_7POINT1
#define SPEAKER_7POINT1          0x0000013F
#endif

/* Context structure */
typedef struct {
    int            sample_rate;
    int            channels;
    int            buffer_frames;
    ap_mix_cb      mix_cb;
    void          *userdata;
    volatile int   running;
} ap_ctx;

static ap_ctx g_audio;
static IAudioClient       *g_pAudioClient;
static IAudioRenderClient *g_pRenderClient;
static HANDLE              g_hEvent;
static UINT32              g_bufferFrameCount;
static UINT32              g_periodFrames;
static int                 g_initialized;
static int                 g_actual_sample_rate;   /* NEW: actual device rate */

#define VTABLE(iface) ((iface)->lpVtbl)

/* Helper to print HRESULT error */
static void print_hresult(const char *func, HRESULT hr)
{
    printf("ERROR: %s failed with HRESULT 0x%08lx\n", func, (unsigned long)hr);
}

/* ------------------------------------------------------------------ */
/* WASAPI backend                                                     */
/* ------------------------------------------------------------------ */
int ap_init(int sample_rate, int channels, int buffer_frames,
            ap_mix_cb mix_cb, void *userdata)
{
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    WAVEFORMATEX *pwfx = NULL;
    WAVEFORMATEX *pwfx_orig = NULL;   /* keep original mix format */
    WAVEFORMATEXTENSIBLE wfxExt = {0};
    REFERENCE_TIME hnsRequestedDuration;
    UINT32 minPeriodFrames;
    int use_mix_format = 0;

    printf("ap_init called: %d Hz, %d ch, buffer_frames=%d\n",
           sample_rate, channels, buffer_frames);

    hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != S_FALSE) {
        print_hresult("CoInitialize", hr);
        return -1;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
                          CLSCTX_ALL, &IID_IMMDeviceEnumerator,
                          (void**)&pEnumerator);
    if (FAILED(hr)) {
        print_hresult("CoCreateInstance(MMDeviceEnumerator)", hr);
        goto error;
    }

    hr = VTABLE(pEnumerator)->GetDefaultAudioEndpoint(pEnumerator,
                                                       eRender, eConsole,
                                                       &pDevice);
    if (FAILED(hr)) {
        print_hresult("GetDefaultAudioEndpoint", hr);
        goto error;
    }

    hr = VTABLE(pDevice)->Activate(pDevice, &IID_IAudioClient,
                                   CLSCTX_ALL, NULL,
                                   (void**)&g_pAudioClient);
    if (FAILED(hr)) {
        print_hresult("Activate(IAudioClient)", hr);
        goto error;
    }

    hr = VTABLE(g_pAudioClient)->GetMixFormat(g_pAudioClient, &pwfx);
    if (FAILED(hr)) {
        print_hresult("GetMixFormat", hr);
        goto error;
    }
    pwfx_orig = pwfx;   /* save the original mix format */

    printf("Device mix format: tag=0x%04X, channels=%d, sample_rate=%d\n",
           pwfx->wFormatTag, pwfx->nChannels, pwfx->nSamplesPerSec);

    /* If the mix format is float and matches our channel count, we can use it directly.
       Otherwise, we try to build our own float format. */
    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pwfx->nChannels == (WORD)channels) {
        /* Use the device's mix format as-is (it already matches our requested sample rate) */
        use_mix_format = 1;
        printf("Using device mix format (float, %d channels, %d Hz)\n",
               pwfx->nChannels, pwfx->nSamplesPerSec);
    } else {
        /* Build our custom float format (may be rejected if sample rate/channels differ) */
        WAVEFORMATEXTENSIBLE *pExt = &wfxExt;
        pExt->Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
        pExt->Format.nChannels       = (WORD)channels;
        pExt->Format.nSamplesPerSec  = (DWORD)sample_rate;
        pExt->Format.wBitsPerSample  = 32;
        pExt->Format.nBlockAlign     = (WORD)(channels * 4);
        pExt->Format.nAvgBytesPerSec = (DWORD)(sample_rate * channels * 4);
        pExt->Format.cbSize          = 22;
        pExt->SubFormat              = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        pExt->Samples.wValidBitsPerSample = 32;
        pExt->dwChannelMask = (channels == 1) ? SPEAKER_MONO :
                              (channels == 2) ? SPEAKER_STEREO :
                              (channels == 4) ? SPEAKER_QUAD :
                              (channels == 6) ? SPEAKER_5POINT1 :
                              (channels == 8) ? SPEAKER_7POINT1 : 0;
        pwfx = (WAVEFORMATEX*)pExt;
        printf("Using custom float format (%d channels, %d Hz)\n", channels, sample_rate);
    }

    hr = VTABLE(g_pAudioClient)->GetDevicePeriod(g_pAudioClient, NULL, &hnsRequestedDuration);
    if (FAILED(hr)) {
        print_hresult("GetDevicePeriod", hr);
        goto error;
    }
    minPeriodFrames = (UINT32)((hnsRequestedDuration * sample_rate) / 10000000.0);
    printf("Minimum period: %u frames (%.2f ms)\n", minPeriodFrames,
           (double)minPeriodFrames * 1000.0 / sample_rate);

    /* Request buffer_frames as latency, but at least min period */
    hnsRequestedDuration = (REFERENCE_TIME)((10000.0 * 1000.0 * buffer_frames) / sample_rate);
    REFERENCE_TIME minDur = (REFERENCE_TIME)((10000.0 * 1000.0 * minPeriodFrames) / sample_rate);
    if (hnsRequestedDuration < minDur) hnsRequestedDuration = minDur;
    printf("Requested latency: %lld 100-ns units (%.2f ms)\n",
           (long long)hnsRequestedDuration,
           (double)hnsRequestedDuration / 10000.0);

    /* Try to initialize with our chosen format */
    hr = VTABLE(g_pAudioClient)->Initialize(g_pAudioClient,
                                            AUDCLNT_SHAREMODE_SHARED,
                                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                            hnsRequestedDuration,
                                            0,
                                            pwfx,
                                            NULL);
    if (FAILED(hr)) {
        print_hresult("Initialize (first attempt)", hr);
        /* If unsupported format and we're not already using the mix format, retry with it */
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT && !use_mix_format && pwfx_orig) {
            printf("Retrying with device mix format (fallback)...\n");
            use_mix_format = 1;
            pwfx = pwfx_orig;   /* restore original mix format */
            hr = VTABLE(g_pAudioClient)->Initialize(g_pAudioClient,
                                                    AUDCLNT_SHAREMODE_SHARED,
                                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                    hnsRequestedDuration,
                                                    0,
                                                    pwfx,
                                                    NULL);
            if (FAILED(hr)) {
                print_hresult("Initialize (mix-format fallback)", hr);
                goto error;
            }
            printf("Successfully initialized with device mix format.\n");
        } else {
            /* Try with default period (0) as last resort */
            printf("Retrying with default period...\n");
            hr = VTABLE(g_pAudioClient)->Initialize(g_pAudioClient,
                                                    AUDCLNT_SHAREMODE_SHARED,
                                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                    0, 0, pwfx, NULL);
            if (FAILED(hr)) {
                print_hresult("Initialize (default-period fallback)", hr);
                goto error;
            }
        }
    }

    hr = VTABLE(g_pAudioClient)->GetBufferSize(g_pAudioClient, &g_bufferFrameCount);
    if (FAILED(hr)) {
        print_hresult("GetBufferSize", hr);
        goto error;
    }
    printf("Buffer size: %u frames (%.2f ms)\n", g_bufferFrameCount,
           (double)g_bufferFrameCount * 1000.0 / sample_rate);

    /* Get actual period */
    hr = VTABLE(g_pAudioClient)->GetDevicePeriod(g_pAudioClient, NULL, &hnsRequestedDuration);
    if (FAILED(hr)) {
        print_hresult("GetDevicePeriod (2nd)", hr);
        goto error;
    }
    minPeriodFrames = (UINT32)((hnsRequestedDuration * sample_rate) / 10000000.0);
    g_periodFrames = g_bufferFrameCount;
    if (g_periodFrames > g_bufferFrameCount) g_periodFrames = g_bufferFrameCount;
    printf("Period frames: %u (will write this many per update)\n", g_periodFrames);

    g_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_hEvent) {
        printf("ERROR: CreateEvent failed (GLE=%lu)\n", (unsigned long)GetLastError());
        goto error;
    }

    hr = VTABLE(g_pAudioClient)->SetEventHandle(g_pAudioClient, g_hEvent);
    if (FAILED(hr)) {
        print_hresult("SetEventHandle", hr);
        goto error;
    }

    hr = VTABLE(g_pAudioClient)->GetService(g_pAudioClient, &IID_IAudioRenderClient,
                                            (void**)&g_pRenderClient);
    if (FAILED(hr)) {
        print_hresult("GetService(IAudioRenderClient)", hr);
        goto error;
    }

    /* Store context - note the actual sample rate used (from pwfx) */
    int actual_sample_rate = pwfx->nSamplesPerSec;
    g_actual_sample_rate = actual_sample_rate;   /* NEW */
    g_audio.sample_rate   = actual_sample_rate;
    g_audio.channels      = channels;
    g_audio.buffer_frames = buffer_frames;
    g_audio.mix_cb        = mix_cb;
    g_audio.userdata      = userdata;

    if (pEnumerator) VTABLE(pEnumerator)->Release(pEnumerator);
    if (pDevice) VTABLE(pDevice)->Release(pDevice);
    if (pwfx_orig && pwfx_orig != pwfx) CoTaskMemFree(pwfx_orig);
    else if (pwfx_orig) CoTaskMemFree(pwfx_orig);   /* if pwfx points to the original, free it now */
    /* If pwfx is our custom stack format, we don't free it */

    g_initialized = 1;
    printf("Audio backend: WASAPI initialized with %d Hz.\n", actual_sample_rate);
    return 0;

error:
    if (pEnumerator) VTABLE(pEnumerator)->Release(pEnumerator);
    if (pDevice) VTABLE(pDevice)->Release(pDevice);
    if (pwfx_orig) CoTaskMemFree(pwfx_orig);
    if (g_pAudioClient) VTABLE(g_pAudioClient)->Release(g_pAudioClient);
    if (g_pRenderClient) VTABLE(g_pRenderClient)->Release(g_pRenderClient);
    if (g_hEvent) CloseHandle(g_hEvent);
    CoUninitialize();
    return -1;
}

void ap_start(void)
{
    if (!g_initialized) return;
    if (!g_audio.running) {
        g_audio.running = 1;
        ap_update();
        VTABLE(g_pAudioClient)->Start(g_pAudioClient);
    }
}

void ap_stop(void)
{
    if (g_audio.running) {
        VTABLE(g_pAudioClient)->Stop(g_pAudioClient);
        g_audio.running = 0;
    }
}

void ap_update(void)
{
    HRESULT hr;
    UINT32 numFramesPadding;
    UINT32 numFramesAvailable;
    BYTE *pData;
    float *temp = NULL;
    int totalSamples;

    if (!g_audio.running || !g_pAudioClient || !g_pRenderClient) return;

    hr = VTABLE(g_pAudioClient)->GetCurrentPadding(g_pAudioClient, &numFramesPadding);
    if (FAILED(hr)) return;

    numFramesAvailable = g_bufferFrameCount - numFramesPadding;
    if (numFramesAvailable == 0) return;

    UINT32 framesToWrite = numFramesAvailable;
    if (framesToWrite > g_periodFrames) framesToWrite = g_periodFrames;

    hr = VTABLE(g_pRenderClient)->GetBuffer(g_pRenderClient, framesToWrite, &pData);
    if (FAILED(hr)) return;

    totalSamples = (int)(framesToWrite * g_audio.channels);
    temp = (float*)ap_malloc((size_t)totalSamples * sizeof(float));
    if (!temp) {
        VTABLE(g_pRenderClient)->ReleaseBuffer(g_pRenderClient, framesToWrite, 0);
        return;
    }

    if (g_audio.mix_cb)
        g_audio.mix_cb(temp, (int)framesToWrite, g_audio.channels, g_audio.userdata);

    memcpy(pData, temp, (size_t)totalSamples * sizeof(float));
    ap_free(temp);

    VTABLE(g_pRenderClient)->ReleaseBuffer(g_pRenderClient, framesToWrite, 0);
}

void ap_shutdown(void)
{
    if (g_pAudioClient) {
        VTABLE(g_pAudioClient)->Stop(g_pAudioClient);
        VTABLE(g_pAudioClient)->Release(g_pAudioClient);
        g_pAudioClient = NULL;
    }
    if (g_pRenderClient) {
        VTABLE(g_pRenderClient)->Release(g_pRenderClient);
        g_pRenderClient = NULL;
    }
    if (g_hEvent) {
        CloseHandle(g_hEvent);
        g_hEvent = NULL;
    }
    if (g_initialized) {
        CoUninitialize();
        g_initialized = 0;
    }
    memset(&g_audio, 0, sizeof(g_audio));
}

/* NEW: getter for actual sample rate */
int ap_get_actual_sample_rate(void)
{
    return g_actual_sample_rate;
}

#endif /* AP_WINDOWS */

/* ------------------------------------------------------------------ */
/* macOS - CoreAudio (unchanged, plus getter)                         */
/* ------------------------------------------------------------------ */
#ifdef AP_MACOS
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#define AP_RING_SIZE  (4096 * 4)

typedef struct {
    AudioUnit           au;
    ap_mix_cb           mix_cb;
    void               *userdata;
    int                 sample_rate;
    int                 channels;
    volatile int        running;
    float               ring[AP_RING_SIZE];
    volatile int        write_pos;
    volatile int        read_pos;
} ap_ctx;

static ap_ctx g_audio;
static int g_actual_sample_rate;   /* NEW */

static OSStatus render_callback(void *inRefCon,
                                AudioUnitRenderActionFlags *ioActionFlags,
                                const AudioTimeStamp *inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList *ioData)
{
    ap_ctx *ctx = (ap_ctx*)inRefCon;
    float *out = (float*)ioData->mBuffers[0].mData;
    int needed = (int)(inNumberFrames * ctx->channels);
    int w = ctx->write_pos;
    int r = ctx->read_pos;
    int avail;

    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;

    avail = (w - r) & (AP_RING_SIZE - 1);
    if (avail < needed) {
        memset(out, 0, needed * sizeof(float));
        return noErr;
    }
    {
        int first = AP_RING_SIZE - r;
        if (first >= needed) {
            memcpy(out, ctx->ring + r, needed * sizeof(float));
            ctx->read_pos = (r + needed) & (AP_RING_SIZE - 1);
        } else {
            memcpy(out, ctx->ring + r, first * sizeof(float));
            memcpy(out + first, ctx->ring, (needed - first) * sizeof(float));
            ctx->read_pos = needed - first;
        }
    }
    return noErr;
}

int ap_init(int sample_rate, int channels, int buffer_frames,
            ap_mix_cb mix_cb, void *userdata)
{
    AudioComponentDescription desc;
    AudioComponent comp;
    AudioStreamBasicDescription fmt;
    AURenderCallbackStruct cb;
    OSStatus err;

    (void)buffer_frames;
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.sample_rate = sample_rate;
    g_audio.channels = channels;
    g_audio.mix_cb = mix_cb;
    g_audio.userdata = userdata;
    g_actual_sample_rate = sample_rate;   /* NEW */

    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags        = 0;
    desc.componentFlagsMask    = 0;

    comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) return -1;
    err = AudioComponentInstanceNew(comp, &g_audio.au);
    if (err) return -1;

    fmt.mSampleRate       = (Float64)sample_rate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagsNativeFloatPacked;
    fmt.mBytesPerPacket   = (UInt32)(channels * sizeof(float));
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = (UInt32)(channels * sizeof(float));
    fmt.mChannelsPerFrame = (UInt32)channels;
    fmt.mBitsPerChannel   = 32;
    fmt.mReserved         = 0;

    err = AudioUnitSetProperty(g_audio.au, kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    if (err) { AudioComponentInstanceDispose(g_audio.au); return -1; }

    cb.inputProc       = render_callback;
    cb.inputProcRefCon = &g_audio;
    err = AudioUnitSetProperty(g_audio.au, kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (err) { AudioComponentInstanceDispose(g_audio.au); return -1; }

    AudioUnitInitialize(g_audio.au);
    printf("Audio backend: CoreAudio\n");
    return 0;
}

void ap_start(void)
{
    if (!g_audio.running) {
        AudioOutputUnitStart(g_audio.au);
        g_audio.running = 1;
    }
}

void ap_stop(void)
{
    if (g_audio.running) {
        AudioOutputUnitStop(g_audio.au);
        g_audio.running = 0;
    }
}

void ap_update(void)
{
    int free_space, avail, to_write;
    int w, r;

    if (!g_audio.running) return;
    w = g_audio.write_pos;
    r = g_audio.read_pos;
    avail = (w - r) & (AP_RING_SIZE - 1);
    free_space = AP_RING_SIZE - avail;

    if (free_space > 0) {
        to_write = (free_space < 1024 * g_audio.channels)
                        ? free_space : (1024 * g_audio.channels);
        if (g_audio.mix_cb) {
            float temp[2048];
            int frames = to_write / g_audio.channels;
            g_audio.mix_cb(temp, frames, g_audio.channels, g_audio.userdata);
            {
                int first = AP_RING_SIZE - w;
                if (first >= to_write) {
                    memcpy(g_audio.ring + w, temp, to_write * sizeof(float));
                    w = (w + to_write) & (AP_RING_SIZE - 1);
                } else {
                    memcpy(g_audio.ring + w, temp, first * sizeof(float));
                    memcpy(g_audio.ring, temp + first,
                           (to_write - first) * sizeof(float));
                    w = to_write - first;
                }
            }
            g_audio.write_pos = w;
        }
    }
}

void ap_shutdown(void)
{
    if (g_audio.au) {
        AudioOutputUnitStop(g_audio.au);
        AudioUnitUninitialize(g_audio.au);
        AudioComponentInstanceDispose(g_audio.au);
        g_audio.au = NULL;
    }
}

int ap_get_actual_sample_rate(void)   /* NEW */
{
    return g_actual_sample_rate;
}

#endif /* AP_MACOS */

/* ------------------------------------------------------------------ */
/* Linux - ALSA (unchanged, plus getter)                              */
/* ------------------------------------------------------------------ */
#ifdef AP_LINUX
#include <alsa/asoundlib.h>

typedef struct {
    snd_pcm_t           *pcm;
    ap_mix_cb            mix_cb;
    void                *userdata;
    int                  sample_rate;
    int                  channels;
    volatile int         running;
} ap_ctx;

static ap_ctx g_audio;
static int g_actual_sample_rate;   /* NEW */

int ap_init(int sample_rate, int channels, int buffer_frames,
            ap_mix_cb mix_cb, void *userdata)
{
    int err;
    snd_pcm_hw_params_t *hwparams;
    unsigned int rate = (unsigned int)sample_rate;
    unsigned int buffer_time_us = 100000;

    (void)buffer_frames;
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.sample_rate = sample_rate;
    g_audio.channels = channels;
    g_audio.mix_cb = mix_cb;
    g_audio.userdata = userdata;
    g_actual_sample_rate = sample_rate;   /* NEW */

    err = snd_pcm_open(&g_audio.pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) return -1;

    snd_pcm_hw_params_alloca(&hwparams);
    snd_pcm_hw_params_any(g_audio.pcm, hwparams);
    snd_pcm_hw_params_set_access(g_audio.pcm, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(g_audio.pcm, hwparams, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(g_audio.pcm, hwparams, (unsigned int)channels);
    snd_pcm_hw_params_set_rate_near(g_audio.pcm, hwparams, &rate, 0);
    snd_pcm_hw_params_set_buffer_time_near(g_audio.pcm, hwparams, &buffer_time_us, 0);
    err = snd_pcm_hw_params(g_audio.pcm, hwparams);
    if (err < 0) { snd_pcm_close(g_audio.pcm); return -1; }

    snd_pcm_nonblock(g_audio.pcm, 1);
    printf("Audio backend: ALSA\n");
    return 0;
}

void ap_start(void)
{
    if (!g_audio.running) {
        snd_pcm_prepare(g_audio.pcm);
        g_audio.running = 1;
    }
}

void ap_stop(void)
{
    if (g_audio.running) {
        snd_pcm_drop(g_audio.pcm);
        g_audio.running = 0;
    }
}

void ap_update(void)
{
    snd_pcm_sframes_t avail;
    int channels = g_audio.channels;
    short *buf;
    float *temp;
    int frames, i;

    if (!g_audio.running) return;
    avail = snd_pcm_avail(g_audio.pcm);
    if (avail <= 0) return;
    if (avail > 1024) avail = 1024;
    frames = (int)avail;

    buf  = (short*)ap_malloc(frames * channels * sizeof(short));
    temp = (float*)ap_malloc(frames * channels * sizeof(float));

    if (g_audio.mix_cb)
        g_audio.mix_cb(temp, frames, channels, g_audio.userdata);

    for (i = 0; i < frames * channels; i++) {
        float v = temp[i];
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        buf[i] = (short)(v * 32767.0f);
    }
    snd_pcm_writei(g_audio.pcm, buf, (snd_pcm_uframes_t)frames);

    ap_free(temp);
    ap_free(buf);
}

void ap_shutdown(void)
{
    if (g_audio.pcm) {
        snd_pcm_close(g_audio.pcm);
        g_audio.pcm = NULL;
    }
}

int ap_get_actual_sample_rate(void)   /* NEW */
{
    return g_actual_sample_rate;
}

#endif /* AP_LINUX */