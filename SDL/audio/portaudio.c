#include "audio.h"
#include <math.h>
#include <portaudio.h>

#define AUDIO_FREQUENCY 96000
#define AUDIO_BUFFER_SIZE 512

static PaStream *stream;
static unsigned buffer_pos = 0;
static GB_sample_t audio_buffer[AUDIO_BUFFER_SIZE];
static unsigned sample_rate = 0;

#define PA_ERR_STRINGIFY(x) #x
#define PA_ERR_TOSTRING(x) PA_ERR_STRINGIFY(x)
#define PA_ERROR(err, msg) check_pa_error(err, msg, PA_ERR_TOSTRING(__LINE__))

static bool check_pa_error(const PaError error, const char *user_msg, const char *line) {
    if (error == paNoError) {
        return false;
    }

    const char *description = Pa_GetErrorText(error);

    if (user_msg != NULL) {
        fprintf(stderr, "[PortAudio:%s] %s: %s\n", line, user_msg, description);
    }
    else {
        fprintf(stderr, "[PortAudio:%s] %s\n", line, description);
    }

    return true;
}

static void _audio_deinit(void) {
    PaError err = Pa_StopStream(stream);
    PA_ERROR(err, "Failed to stop stream");

    err = Pa_CloseStream(stream);
    PA_ERROR(err, "Failed to close stream");

    stream = NULL;

    // Pa_Terminate() must *always* be called before exiting a program
    err = Pa_Terminate();
    PA_ERROR(err, "Failed to terminate PortAudio");
}

static bool _audio_is_playing(void)
{
    return stream && Pa_IsStreamActive(stream);
}

static void _audio_set_paused(bool paused)
{
    if (!stream) return;

    PaError err;
    if (paused) {
        err = Pa_StopStream(stream);
    }
    else {
        err = Pa_StartStream(stream);
    }

    PA_ERROR(err, NULL);
}

static void _audio_clear_queue(void)
{
    buffer_pos = 0;
}

static unsigned _audio_get_frequency(void)
{
    return sample_rate;
}

static size_t _audio_get_queue_length(void)
{
    return buffer_pos * sizeof(GB_sample_t);
}

static void _audio_queue_sample(GB_sample_t *sample)
{
    if (!stream) return;

    audio_buffer[buffer_pos++] = *sample;

    if (buffer_pos == AUDIO_BUFFER_SIZE) {
        buffer_pos = 0;

        PaError err = Pa_WriteStream(stream, audio_buffer, sizeof(audio_buffer) / sizeof(audio_buffer[0]));
        PA_ERROR(err, "Failed to queue samples");
    }
}

static bool _audio_init(void)
{
    PaError err = Pa_Initialize();
    if (PA_ERROR(err, "Failed to initialize PortAudio")) {
        return false;
    }

    const PaDeviceIndex output_device = Pa_GetDefaultOutputDevice();
    if (PA_ERROR(err, "Failed to get the default output device")) {
        return false;
    }

    PaTime latency = 0;
    const PaDeviceInfo *device_info = Pa_GetDeviceInfo(output_device);
    if (device_info != NULL) {
        latency = device_info->defaultLowOutputLatency;
    }

    PaStreamParameters output = {
        .device = output_device,
        .channelCount = 2,
        .sampleFormat = paInt16,
        .suggestedLatency = latency,
        .hostApiSpecificStreamInfo = NULL
    };

    err = Pa_OpenStream(
        &stream,
        NULL,
        &output,
        AUDIO_FREQUENCY,
        sizeof(audio_buffer) / sizeof(audio_buffer[0]),
        paClipOff,
        NULL,
        NULL
    );
    if (PA_ERROR(err, "Failed to open output stream")) {
        return false;
    }

    const PaStreamInfo *stream_info = Pa_GetStreamInfo(stream);
    if (stream_info == NULL) {
        printf("Failed to query stream info\n");
        return false;
    }

    sample_rate = round(stream_info->sampleRate);

    return true;
}

GB_AUDIO_DRIVER(PortAudio);
