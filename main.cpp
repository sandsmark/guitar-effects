#include "MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <iostream>

extern "C" {
#include <pulse/simple.h>
#include <pulse/error.h>
#include <signal.h>
#include <math.h>
}

// From https://github.com/PortAudio/portaudio/blob/master/examples/pa_fuzz.c
struct Fuzz {
    /* Non-linear amplifier with soft distortion curve. */

    inline float CubicAmplifier(const float input) const {
        const float offset = copysignf(1.f, input);
        const float temp = input - offset;
        return (temp * temp * temp) + offset;
    }
    inline float fuzz(const float input) {
        return volume * CubicAmplifier(CubicAmplifier(CubicAmplifier(CubicAmplifier(input))));
    }
    float volume = 0.125;
};

// from https://github.com/marcoalkema/cpp-guitar_effects/blob/master/distortion.cpp
struct Distortion {
    Distortion() {
        timbreInverse = (1 - (timbre * 0.099)) * 10; //inverse scaling from timbre
    }

    inline float distort(float input) const {
        input = input * depth;
        input = tanh((input * (timbre + 1)));
        input = (input * ((0.1 + timbre) * timbreInverse));
        input = cos((input + (timbre + 0.25)));
        input = tanh(input * (timbre + 1));
        input = input * 0.125;

        return input;
    }

    void setTimbre(const float t) {
        timbre = t;
        timbreInverse = (1 - (timbre * 0.099)) * 10; //inverse scaling from timbre
    }
private:
    float timbre = 1.f;
    float timbreInverse = 0.f;
    float depth = 1.f;
};

static bool s_running;
void sigintHandler(int sig) {
    signal(sig, SIG_DFL);
    s_running = false;
}


#define FRAMESPERBUFFER 64
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    s_running = true;
    signal(SIGINT, &sigintHandler);
    signal(SIGTERM, &sigintHandler);

    float buffer[FRAMESPERBUFFER];

    pa_sample_spec inType;
    inType.format = PA_SAMPLE_FLOAT32LE;
    inType.channels = 1;
    inType.rate = 44100;

    // TODO: figure out what the optimal is for latency, ignoring CPU usage
    pa_buffer_attr buffering;
    buffering.fragsize = uint32_t(-1);
//    buffering.fragsize = 10;
    buffering.maxlength = uint32_t(-1);
//    buffering.maxlength = FRAMESPERBUFFER;
    buffering.minreq = uint32_t(-1);
    buffering.prebuf = uint32_t(-1);
//    buffering.tlength = uint32_t(-1);
    buffering.tlength = FRAMESPERBUFFER;

    int error = 0;
    pa_simple *input = pa_simple_new(nullptr, "qguitarfuzz", PA_STREAM_RECORD, nullptr, "input", &inType, nullptr, &buffering, &error);

    if (!input) {
        qWarning() << "Failed to open input stream" << pa_strerror(error);
        return 1;
    }

    // TODO: optimize to lower latency
    pa_sample_spec outType = inType;
    pa_simple *output = pa_simple_new(nullptr, "qguitarfuzz", PA_STREAM_PLAYBACK, nullptr, "output", &outType, nullptr, &buffering, &error);
    if (!output) {
        qWarning() << "Failed to open output stream" << pa_strerror(error);
        return 1;
    }

    bool distort = !(argc > 1 && strcmp(argv[1], "fuzz") == 0);
    if (distort) {
        puts("Distorting");
    } else {
        puts("Fuzzing");
    }
    distort = false;

    Distortion dist;
    dist.setTimbre(0.5);

    Fuzz fuzz;

    int loops = 0;
    pa_simple_flush(input, &error);
    pa_simple_flush(output, &error);
    while (s_running) {
        int ret = pa_simple_read(input, buffer, sizeof buffer, &error);
        if (ret < 0) {
            qWarning() << "Failed to read from input stream" << pa_strerror(error);
            return 1;
        }

        if (distort) {
            for(int i=0; i<FRAMESPERBUFFER; i++) buffer[i] = dist.distort(buffer[i]);
        } else {
            for(int i=0; i<FRAMESPERBUFFER; i++) buffer[i] = fuzz.fuzz(buffer[i]);
        }

        ret = pa_simple_write(output, buffer, sizeof buffer, &error);
        if (ret < 0) {
            qWarning() << "Failed to write to output stream" << pa_strerror(error);
            return 1;
        }

        if (++loops > 100) {
            loops = 0;
            std::cout << "\033[2K\rinput latency: " << (pa_simple_get_latency(input, &error)/1000) << " ms, output latency: " << (pa_simple_get_latency(output, &error)/1000) << " ms" << std::flush;
        }
    }
    puts("");
    return 0;
//    QApplication a(argc, argv);
//    MainWindow w;
//    w.show();
//    return a.exec();
}
