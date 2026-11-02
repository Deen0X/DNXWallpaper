// audio.cpp — Captura WASAPI mínima (stub funcional documentado).
#include "audio.h"

#include <mutex>
namespace {
std::mutex g_mutex;
}

bool AudioProcessor::Start() {
    // Stub documentado: inicializar WASAPI loopback (IMMDeviceEnumerator /
    // IAudioClient, 32-bit float) + FFT (FFTS o KissFFT) pertenece a la
    // fase 2. Se conservan umbrales, modo y contrato de hilos.
    running_ = true;
    return true;
}

void AudioProcessor::Stop() {
    running_ = false;
}

void AudioProcessor::GetSpectrum(AudioSpectrum& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    out = last_;
}
