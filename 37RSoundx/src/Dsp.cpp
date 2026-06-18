#include "Dsp.h"

#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sx {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = 6.28318530717958647692f;

float DbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

}

void Biquad::Reset() {
    z1_ = 0.0f;
    z2_ = 0.0f;
}

void Biquad::Normalize(float b0, float b1, float b2, float a0, float a1, float a2) {
    if (std::abs(a0) < 1.0e-9f) {
        b0_ = 1.0f;
        b1_ = b2_ = a1_ = a2_ = 0.0f;
        Reset();
        return;
    }

    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
}

void Biquad::ConfigureLowShelf(float sampleRate, float frequency, float gainDb, float slope) {
    if (std::abs(gainDb) < 0.001f) {
        Normalize(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    const float a = std::sqrt(DbToLinear(gainDb));
    const float w0 = TwoPi * frequency / sampleRate;
    const float sinW0 = std::sin(w0);
    const float cosW0 = std::cos(w0);
    const float beta = std::sqrt(a) / std::max(0.05f, slope);

    const float b0 = a * ((a + 1.0f) - (a - 1.0f) * cosW0 + beta * sinW0);
    const float b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosW0);
    const float b2 = a * ((a + 1.0f) - (a - 1.0f) * cosW0 - beta * sinW0);
    const float a0 = (a + 1.0f) + (a - 1.0f) * cosW0 + beta * sinW0;
    const float a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosW0);
    const float a2 = (a + 1.0f) + (a - 1.0f) * cosW0 - beta * sinW0;
    Normalize(b0, b1, b2, a0, a1, a2);
}

void Biquad::ConfigureHighShelf(float sampleRate, float frequency, float gainDb, float slope) {
    if (std::abs(gainDb) < 0.001f) {
        Normalize(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    const float a = std::sqrt(DbToLinear(gainDb));
    const float w0 = TwoPi * frequency / sampleRate;
    const float sinW0 = std::sin(w0);
    const float cosW0 = std::cos(w0);
    const float beta = std::sqrt(a) / std::max(0.05f, slope);

    const float b0 = a * ((a + 1.0f) + (a - 1.0f) * cosW0 + beta * sinW0);
    const float b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW0);
    const float b2 = a * ((a + 1.0f) + (a - 1.0f) * cosW0 - beta * sinW0);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cosW0 + beta * sinW0;
    const float a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosW0);
    const float a2 = (a + 1.0f) - (a - 1.0f) * cosW0 - beta * sinW0;
    Normalize(b0, b1, b2, a0, a1, a2);
}

void Biquad::ConfigurePeak(float sampleRate, float frequency, float gainDb, float q) {
    if (std::abs(gainDb) < 0.001f) {
        Normalize(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    const float a = std::sqrt(DbToLinear(gainDb));
    const float w0 = TwoPi * frequency / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * std::max(0.05f, q));
    const float cosW0 = std::cos(w0);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cosW0;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha / a;
    Normalize(b0, b1, b2, a0, a1, a2);
}

float Biquad::Process(float sample) {
    const float out = b0_ * sample + z1_;
    z1_ = b1_ * sample - a1_ * out + z2_;
    z2_ = b2_ * sample - a2_ * out;
    if (!std::isfinite(out)) {
        Reset();
        return 0.0f;
    }
    return out;
}

void Equalizer::Configure(uint32_t sampleRate, uint16_t channels) {
    sampleRate_ = sampleRate ? sampleRate : 48000;
    channels_ = std::max<uint16_t>(1, channels);
    filters_.assign(channels_, ChannelFilters{});
    UpdateSettings(settings_);
}

void Equalizer::UpdateSettings(const DspSettings& settings) {
    settings_ = settings;
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    const float subFreq = std::min(58.0f, nyquist * 0.24f);
    const float punchFreq = std::min(96.0f, nyquist * 0.34f);
    const float lowFreq = std::min(175.0f, nyquist * 0.45f);
    const float midFreq = std::min(950.0f, nyquist * 0.65f);
    const float highFreq = std::min(4200.0f, nyquist * 0.85f);

    for (auto& filter : filters_) {
        const float bassDb = Clamp(settings.bassDb, -36.0f, 36.0f);
        filter.sub.ConfigureLowShelf(static_cast<float>(sampleRate_), subFreq, bassDb * 0.62f, 0.55f);
        filter.punch.ConfigurePeak(static_cast<float>(sampleRate_), punchFreq, bassDb * 0.48f, 0.72f);
        filter.low.ConfigureLowShelf(static_cast<float>(sampleRate_), lowFreq, bassDb * 0.88f, 0.50f);
        filter.mid.ConfigurePeak(static_cast<float>(sampleRate_), midFreq, Clamp(settings.midDb, -12.0f, 12.0f));
        filter.high.ConfigureHighShelf(static_cast<float>(sampleRate_), highFreq, Clamp(settings.trebleDb, -12.0f, 12.0f));
    }
}

void Equalizer::Process(float* interleaved, uint32_t frames) {
    if (!interleaved || filters_.empty()) {
        return;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint16_t channel = 0; channel < channels_; ++channel) {
            float sample = interleaved[frame * channels_ + channel];
            sample = filters_[channel].sub.Process(sample);
            sample = filters_[channel].punch.Process(sample);
            sample = filters_[channel].low.Process(sample);
            sample = filters_[channel].mid.Process(sample);
            sample = filters_[channel].high.Process(sample);
            interleaved[frame * channels_ + channel] = Clamp(sample, -8.0f, 8.0f);
        }
    }
}

void Equalizer::Reset() {
    for (auto& filter : filters_) {
        filter.sub.Reset();
        filter.punch.Reset();
        filter.low.Reset();
        filter.mid.Reset();
        filter.high.Reset();
    }
}

void PitchShifter::Configure(uint32_t sampleRate, uint16_t channels) {
    sampleRate_ = sampleRate ? sampleRate : 48000;
    channels_ = std::max<uint16_t>(1, channels);
    channelsState_.assign(channels_, ChannelState{});
    for (auto& state : channelsState_) {
        ConfigureChannel(state);
    }
}

void PitchShifter::ConfigureChannel(ChannelState& state) {
    const int halfFrame = fftFrameSize_ / 2;
    state.latency = fftFrameSize_ - (fftFrameSize_ / overlap_);
    state.rover = state.latency;
    state.inFifo.assign(fftFrameSize_, 0.0f);
    state.outFifo.assign(fftFrameSize_, 0.0f);
    state.outputAccum.assign(fftFrameSize_ * 2, 0.0f);
    state.lastPhase.assign(halfFrame + 1, 0.0f);
    state.sumPhase.assign(halfFrame + 1, 0.0f);
    state.analysisMagnitude.assign(halfFrame + 1, 0.0f);
    state.analysisFrequency.assign(halfFrame + 1, 0.0f);
    state.synthesisMagnitude.assign(halfFrame + 1, 0.0f);
    state.synthesisFrequency.assign(halfFrame + 1, 0.0f);
    state.fftWorkspace.assign(fftFrameSize_ * 2, 0.0f);
}

void PitchShifter::SetSemitones(float semitones) {
    pitchFactor_ = std::pow(2.0f, Clamp(semitones, -12.0f, 12.0f) / 12.0f);
}

void PitchShifter::Reset() {
    for (auto& state : channelsState_) {
        ConfigureChannel(state);
    }
}

void PitchShifter::Process(float* interleaved, uint32_t frames) {
    if (!interleaved || channelsState_.empty() || std::abs(pitchFactor_ - 1.0f) < 0.0025f) {
        return;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint16_t channel = 0; channel < channels_; ++channel) {
            const size_t index = static_cast<size_t>(frame) * channels_ + channel;
            interleaved[index] = ProcessSample(channelsState_[channel], interleaved[index]);
        }
    }
}

float PitchShifter::ProcessSample(ChannelState& state, float sample) {
    state.inFifo[state.rover] = sample;
    const float output = state.outFifo[state.rover - state.latency];
    ++state.rover;

    if (state.rover >= fftFrameSize_) {
        state.rover = state.latency;
        ProcessFrame(state);
    }

    return Clamp(output, -1.5f, 1.5f);
}

void PitchShifter::ProcessFrame(ChannelState& state) {
    const int stepSize = fftFrameSize_ / overlap_;
    const int halfFrame = fftFrameSize_ / 2;
    const float frequencyPerBin = static_cast<float>(sampleRate_) / static_cast<float>(fftFrameSize_);
    const float expectedPhase = TwoPi * static_cast<float>(stepSize) / static_cast<float>(fftFrameSize_);

    for (int i = 0; i < fftFrameSize_; ++i) {
        const float window = -0.5f * std::cos(TwoPi * static_cast<float>(i) / static_cast<float>(fftFrameSize_)) + 0.5f;
        state.fftWorkspace[2 * i] = state.inFifo[i] * window;
        state.fftWorkspace[2 * i + 1] = 0.0f;
    }

    Fft(state.fftWorkspace, fftFrameSize_, false);

    for (int k = 0; k <= halfFrame; ++k) {
        const float real = state.fftWorkspace[2 * k];
        const float imag = state.fftWorkspace[2 * k + 1];
        const float magnitude = 2.0f * std::sqrt(real * real + imag * imag);
        const float phase = std::atan2(imag, real);
        float deltaPhase = phase - state.lastPhase[k];
        state.lastPhase[k] = phase;

        deltaPhase -= static_cast<float>(k) * expectedPhase;
        int quadrant = static_cast<int>(deltaPhase / Pi);
        if (quadrant >= 0) {
            quadrant += quadrant & 1;
        } else {
            quadrant -= quadrant & 1;
        }
        deltaPhase -= Pi * static_cast<float>(quadrant);
        deltaPhase = overlap_ * deltaPhase / TwoPi;

        state.analysisMagnitude[k] = magnitude;
        state.analysisFrequency[k] = (static_cast<float>(k) + deltaPhase) * frequencyPerBin;
    }

    std::fill(state.synthesisMagnitude.begin(), state.synthesisMagnitude.end(), 0.0f);
    std::fill(state.synthesisFrequency.begin(), state.synthesisFrequency.end(), 0.0f);

    for (int k = 0; k <= halfFrame; ++k) {
        const int targetIndex = static_cast<int>(static_cast<float>(k) * pitchFactor_);
        if (targetIndex <= halfFrame) {
            state.synthesisMagnitude[targetIndex] += state.analysisMagnitude[k];
            state.synthesisFrequency[targetIndex] = state.analysisFrequency[k] * pitchFactor_;
        }
    }

    std::fill(state.fftWorkspace.begin(), state.fftWorkspace.end(), 0.0f);

    for (int k = 0; k <= halfFrame; ++k) {
        const float magnitude = state.synthesisMagnitude[k];
        float trueFrequency = state.synthesisFrequency[k];
        trueFrequency -= static_cast<float>(k) * frequencyPerBin;
        trueFrequency /= frequencyPerBin;
        trueFrequency = TwoPi * trueFrequency / static_cast<float>(overlap_);
        trueFrequency += static_cast<float>(k) * expectedPhase;
        state.sumPhase[k] += trueFrequency;

        state.fftWorkspace[2 * k] = magnitude * std::cos(state.sumPhase[k]);
        state.fftWorkspace[2 * k + 1] = magnitude * std::sin(state.sumPhase[k]);
    }

    Fft(state.fftWorkspace, fftFrameSize_, true);

    for (int i = 0; i < fftFrameSize_; ++i) {
        const float window = -0.5f * std::cos(TwoPi * static_cast<float>(i) / static_cast<float>(fftFrameSize_)) + 0.5f;
        state.outputAccum[i] += 2.0f * window * state.fftWorkspace[2 * i] / (static_cast<float>(halfFrame) * static_cast<float>(overlap_));
    }

    for (int i = 0; i < stepSize; ++i) {
        state.outFifo[i] = state.outputAccum[i];
    }

    std::memmove(state.outputAccum.data(), state.outputAccum.data() + stepSize, sizeof(float) * static_cast<size_t>(fftFrameSize_));
    std::fill(state.outputAccum.begin() + fftFrameSize_, state.outputAccum.end(), 0.0f);
    std::memmove(state.inFifo.data(), state.inFifo.data() + stepSize, sizeof(float) * static_cast<size_t>(state.latency));
}

void PitchShifter::Fft(std::vector<float>& data, int fftFrameSize, bool inverse) {
    int j = 0;
    for (int i = 0; i < fftFrameSize - 1; ++i) {
        if (i < j) {
            std::swap(data[2 * i], data[2 * j]);
            std::swap(data[2 * i + 1], data[2 * j + 1]);
        }
        int bit = fftFrameSize >> 1;
        while (bit <= j) {
            j -= bit;
            bit >>= 1;
        }
        j += bit;
    }

    for (int length = 2; length <= fftFrameSize; length <<= 1) {
        const float angle = (inverse ? TwoPi : -TwoPi) / static_cast<float>(length);
        const float wLengthReal = std::cos(angle);
        const float wLengthImag = std::sin(angle);

        for (int i = 0; i < fftFrameSize; i += length) {
            float wReal = 1.0f;
            float wImag = 0.0f;

            for (int k = 0; k < length / 2; ++k) {
                const int even = 2 * (i + k);
                const int odd = 2 * (i + k + length / 2);
                const float oddReal = data[odd] * wReal - data[odd + 1] * wImag;
                const float oddImag = data[odd] * wImag + data[odd + 1] * wReal;
                const float evenReal = data[even];
                const float evenImag = data[even + 1];

                data[even] = evenReal + oddReal;
                data[even + 1] = evenImag + oddImag;
                data[odd] = evenReal - oddReal;
                data[odd + 1] = evenImag - oddImag;

                const float nextReal = wReal * wLengthReal - wImag * wLengthImag;
                wImag = wReal * wLengthImag + wImag * wLengthReal;
                wReal = nextReal;
            }
        }
    }

    (void)inverse;
}

void DspProcessor::Configure(uint32_t sampleRate, uint16_t channels) {
    channels_ = std::max<uint16_t>(1, channels);
    equalizer_.Configure(sampleRate, channels_);
    pitchShifter_.Configure(sampleRate, channels_);
    UpdateSettings(settings_);
}

void DspProcessor::UpdateSettings(const DspSettings& settings) {
    settings_ = settings;
    equalizer_.UpdateSettings(settings_);
    pitchShifter_.SetSemitones(settings_.pitchSemitones);
}

void DspProcessor::Process(float* interleaved, uint32_t frames) {
    if (!interleaved || frames == 0) {
        return;
    }

    const float volume = Clamp(settings_.volume, 0.0f, 2.5f);
    const uint32_t totalSamples = frames * channels_;
    for (uint32_t i = 0; i < totalSamples; ++i) {
        interleaved[i] *= volume;
    }

    equalizer_.Process(interleaved, frames);
    pitchShifter_.Process(interleaved, frames);

    for (uint32_t i = 0; i < totalSamples; ++i) {
        const float driven = Clamp(interleaved[i], -6.0f, 6.0f);
        interleaved[i] = std::tanh(driven * 1.28f) / std::tanh(1.28f);
        interleaved[i] = Clamp(interleaved[i], -1.0f, 1.0f);
    }
}

void DspProcessor::Reset() {
    equalizer_.Reset();
    pitchShifter_.Reset();
}

}
