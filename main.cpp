#include "MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <iostream>
#include <limits>
#include <type_traits>

extern "C" {
#include <pulse/simple.h>
#include <pulse/error.h>
#include <signal.h>
#include <math.h>
}

#define SAMPLERATE		48000
#define NROFCHANNELS		1 // todo: do per-channel handling ourselves, pulseaudio eats CPU resampling to a single channel
#define BUFFER_SIZE 64 // TODO value chosen a bit at random


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

// from https://github.com/marcoalkema/cpp-guitar_effects/blob/master/ringModulator.cpp
struct RingModulator {
    float Fc = 440;
    float Fs = 300;
    float mod_phase = 0;

    inline float modulate(float input) {
        input= 0.005 * input* (1 + Fs*sin(mod_phase) );
        mod_phase+=Fc*2*M_PI/SAMPLERATE;
        return input;
    }
};

// from https://github.com/marcoalkema/cpp-guitar_effects/blob/master/delay.cpp
#define DELAYBUFFERSIZE 44100
struct Delay {
    float delayBuffer[DELAYBUFFERSIZE]{};
    int delayTime = 5000;
    int input = 0;
    int output = 0;
    float feedback = 0.5;
    int bypass = 1;

    inline float process(float in) {
        if(input >= DELAYBUFFERSIZE){
            input = 0;
        }
        output = input - delayTime;
        while(output < 0){
            output = output + DELAYBUFFERSIZE;
        }
        delayBuffer[input] = in + (delayBuffer[output % DELAYBUFFERSIZE] * feedback);
        in = cos(delayBuffer[input] + 0.5);
        input++;
        return in;
    }
};

static bool s_running;
void sigintHandler(int sig) {
    signal(sig, SIG_DFL);
    s_running = false;
}

static void printUsage(const char *app)
{
    printf("Usage: %s effect1 [effect2] [effect3] [etc]\n", app);
    puts("Available effects:\n"
                "\tdistort\n"
                "\tfuzz\n"
                "\tringmodulator\n"
                "\tdelay\n"
            );

}

int main(int argc, char *argv[])
{
    s_running = true;
    signal(SIGINT, &sigintHandler);
    signal(SIGTERM, &sigintHandler);

    // fucking SFINAE, can't get it to work
    float bufferF[BUFFER_SIZE];
    uint8_t buffer8[BUFFER_SIZE];
    uint16_t buffer16[BUFFER_SIZE];
    uint32_t buffer32[BUFFER_SIZE];

    pa_sample_spec inType;
    inType.format = PA_SAMPLE_FLOAT32LE;
    inType.channels = NROFCHANNELS;
    inType.rate = SAMPLERATE;

    // TODO: figure out what the optimal is for latency, ignoring CPU usage
    pa_buffer_attr buffering;
    buffering.maxlength = uint32_t(-1);
    buffering.prebuf = uint32_t(-1);
    buffering.minreq = uint32_t(-1);
    buffering.fragsize = uint32_t(-1);
    buffering.tlength = uint32_t(-1);

    buffering.tlength = BUFFER_SIZE * 2; // output: let the server buffer at least one chunk ahead, should keep latency down while avoiding underruns
    buffering.fragsize = BUFFER_SIZE; // input: hand us one chunk at the time


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

    Distortion dist;
    dist.setTimbre(0.5);
    Fuzz fuzz;
    RingModulator modulator;
    Delay delay;

    int loops = 0;

    enum EffectType {
        Distort,
        Fuzz,
        RingModulator,
        Delay,
    };

    std::vector<EffectType> effects;

    for (int i=1; i<argc; i++) {
        const std::string type = argv[i];
        if (type == "distort") {
            effects.push_back(Distort);
        } else if (type == "fuzz") {
            effects.push_back(Fuzz);
        } else if (type == "ringmodulator (kind of slow, so might not work in realtime for you)") {
            effects.push_back(RingModulator);
        } else if (type == "delay") {
            effects.push_back(Delay);
        } else if (type == "-help" || type == "--help") {
            printUsage(argv[0]);
            return 1;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }

    if (effects.empty()) {
        puts("No effects specified, defaulting to just distort");
        effects.push_back(Distort);
        printUsage(argv[0]);
    }

    int delayedInput = 0;
    int delayedOutput = 0;

    std::cout <<
         " - Buffer size: " << BUFFER_SIZE << "\n" <<
         " - Output buffer length: " << buffering.tlength << "\n" <<
         " - Input fragment size: " << buffering.fragsize <<
        std::endl;

    puts("Running, press ctrl+c to stop");
    pa_simple_flush(input, &error);
    pa_simple_flush(output, &error);
    while (s_running) {
        int ret = - 1;
        switch (inType.format) {
        case PA_SAMPLE_U8:
            ret = pa_simple_read(input, buffer8, sizeof buffer8, &error);
            break;
        case PA_SAMPLE_S16LE:
            ret = pa_simple_read(input, buffer16, sizeof buffer16, &error);
            break;
        case PA_SAMPLE_S32LE:
            ret = pa_simple_read(input, buffer32, sizeof buffer32, &error);
            break;
        case PA_SAMPLE_FLOAT32LE:
            ret = pa_simple_read(input, bufferF, sizeof bufferF, &error);
            break;
        default:
            return 1;
        }
        if (ret < 0) {
            qWarning() << "Failed to read from input stream" << pa_strerror(error);
            break;
        }

        for (const EffectType type : effects) {
            switch (inType.format) {
            case PA_SAMPLE_U8: {
                switch(type) {
                case Distort:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer8[i] = dist.distort(buffer8[i] / 127.f) * 127.f;
                    break;
                case Fuzz:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer8[i] = fuzz.fuzz(buffer8[i] / 127.f) * 127.f;
                    break;
                case RingModulator:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer8[i] = modulator.modulate(buffer8[i] / 127.f) * 127.f;
                    break;
                case Delay:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer8[i] = delay.process(buffer8[i] / 127.f) * 127.f;
                    break;
                default:
                    break;
                }
                break;
            }
            case PA_SAMPLE_S16LE:
                switch(type) {
                case Distort:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer16[i] = dist.distort(buffer16[i] / 32767.0f) * 32767.0f;
                    break;
                case Fuzz:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer16[i] = fuzz.fuzz(buffer16[i] / 32767.0f) * 32767.0f;
                    break;
                case RingModulator:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer16[i] = modulator.modulate(buffer16[i] / 32767.0f) * 32767.0f;
                    break;
                case Delay:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer16[i] = delay.process(buffer16[i] / 32767.0f) * 32767.0f;
                    break;
                default:
                    break;
                }
                break;
            case PA_SAMPLE_S32LE:
                switch(type) {
                case Distort:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer32[i] = dist.distort(buffer32[i] / 2147483647.0f) * 2147483647.0f;
                    break;
                case Fuzz:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer32[i] = fuzz.fuzz(buffer32[i] / 2147483647.0f) * 2147483647.0f;
                    break;
                case RingModulator:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer32[i] = modulator.modulate(buffer32[i] / 2147483647.0f) * 2147483647.0f;
                    break;
                case Delay:
                    for(int i=0; i<BUFFER_SIZE; i++) buffer32[i] = delay.process(buffer32[i] / 2147483647.0f) * 2147483647.0f;
                    break;
                default:
                    break;
                }
                break;
            case PA_SAMPLE_FLOAT32LE:
                switch(type) {
                case Distort:
                    for(int i=0; i<BUFFER_SIZE; i++) bufferF[i] = dist.distort(bufferF[i]);
                    break;
                case Fuzz:
                    for(int i=0; i<BUFFER_SIZE; i++) bufferF[i] = fuzz.fuzz(bufferF[i]);
                    break;
                case RingModulator:
                    for(int i=0; i<BUFFER_SIZE; i++) bufferF[i] = modulator.modulate(bufferF[i]);
                    break;
                case Delay:
                    for(int i=0; i<BUFFER_SIZE; i++) bufferF[i] = delay.process(bufferF[i]);
                    break;
                default:
                    break;
                }
                break;
            default:
                return 1;
            }
        }

        switch (outType.format) {
        case PA_SAMPLE_U8:
            ret = pa_simple_write(output, buffer8, sizeof buffer8, &error);
            break;
        case PA_SAMPLE_S16LE:
            ret = pa_simple_write(output, buffer16, sizeof buffer16, &error);
            break;
        case PA_SAMPLE_S32LE:
            ret = pa_simple_write(output, buffer32, sizeof buffer32, &error);
            break;
        case PA_SAMPLE_FLOAT32LE:
            ret = pa_simple_write(output, bufferF, sizeof bufferF, &error);
            break;
        default:
            return 1;
        }
        if (ret < 0) {
            qWarning() << "Failed to write to output stream" << pa_strerror(error);
            break;
        }

        if (++loops > 100) {
            loops = 0;
            std::cout << "\033[2K\rInput latency: " << (pa_simple_get_latency(input, &error)/1000) << " ms, output latency: " << (pa_simple_get_latency(output, &error)/1000) << " ms       " << std::flush;

            // This usually happens when the input we are using was suspended when we started, and the simple API doesn't allow us to force drop the entire chain.
            if (pa_simple_get_latency(input, &error)/10000 > 1) {
                delayedInput++;
                if (delayedInput > 10) {
                    delayedInput = 0;
                    puts("\n ! High latency on input, reconnecting...");

                    pa_simple_free(input);
                    input = nullptr;
                    input = pa_simple_new(nullptr, "qguitarfuzz", PA_STREAM_RECORD, nullptr, "input", &inType, nullptr, &buffering, &error);

                    if (!input) {
                        qWarning() << "Failed to re-open input stream" << pa_strerror(error);
                        break;
                    }
                }
            } else {
                delayedInput = 0 ;
            }

            if (pa_simple_get_latency(output, &error)/10000 > 1) {
                delayedOutput++;
                if (delayedOutput > 10) {
                    delayedOutput = 0;
                    puts("\n ! High latency on output, reconnecting...");

                    pa_simple_free(output);
                    output = nullptr;
                    pa_simple *output = pa_simple_new(nullptr, "qguitarfuzz", PA_STREAM_PLAYBACK, nullptr, "output", &outType, nullptr, &buffering, &error);
                    if (!output) {
                        qWarning() << "Failed to re-open output stream" << pa_strerror(error);
                        break;
                    }
                }
            }
        }
    }
    puts("");
    if (output) {
        pa_simple_free(output);
    }
    if (input) {
        pa_simple_free(input);
    }
    return 0;
//    QApplication a(argc, argv);
//    MainWindow w;
//    w.show();
//    return a.exec();
}
