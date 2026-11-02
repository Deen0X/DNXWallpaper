// audio.h — Captura de audio del sistema (WASAPI loopback) + FFT.
//
// Observado en .rdata: WASAPI ("WASAPI processor requires 32 bit per
// sample.", "WASAPI unexpected block align: %i * %i != %i."),
// FFTS ("D:\dev\we\windows\src\ffts\src\ffts_static.c" — FFTS es
// open-source), propiedades de escena:
//   audioinputdevice, audioinputthreshold, audioinputvolume,
//   audioprocessing, audioprocessingbounds, audioprocessingexponent,
//   audioprocessingfrequencyend, audioprocessingfrequencystart,
//   audioprocessingmode. Mensaje: "Your audio driver crashed ...".
// Clases decoradas: AudioEventHandler / AudioProcessor (WASAPICallbackType).
#pragma once

#include <windows.h>
#include <vector>

struct AudioSpectrum {
    static constexpr int kBins = 64;
    float bins[kBins] = {};
    float volume = 0.0f; // audioinputvolume
};

// Captura loopback WASAPI y expone espectro (FFT externa).
class AudioProcessor {
public:
    AudioProcessor() = default;
    ~AudioProcessor() { Stop(); }

    bool Start();
    void Stop();

    // Rellena el espectro actual (thread-safe para el hilo de render).
    void GetSpectrum(AudioSpectrum& out);

    void SetThreshold(float t) { threshold_ = t; } // audioinputthreshold
    void SetMode(int mode) { mode_ = mode; }       // audioprocessingmode

private:
    float threshold_ = 0.0f;
    int mode_ = 0;
    bool running_ = false;
    AudioSpectrum last_ = {};
};
