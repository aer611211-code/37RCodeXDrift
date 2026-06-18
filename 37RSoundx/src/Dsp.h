#pragma once

#include <cstdint>
#include <vector>

namespace sx {

struct DspSettings {
    float volume = 1.0f;
    float bassDb = 0.0f;
    float midDb = 0.0f;
    float trebleDb = 0.0f;
    float pitchSemitones = 0.0f;
};

class Biquad {
public:
    void Reset();
    void ConfigureLowShelf(float sampleRate, float frequency, float gainDb, float slope = 0.75f);
    void ConfigureHighShelf(float sampleRate, float frequency, float gainDb, float slope = 0.75f);
    void ConfigurePeak(float sampleRate, float frequency, float gainDb, float q = 0.85f);
    float Process(float sample);

private:
    void Normalize(float b0, float b1, float b2, float a0, float a1, float a2);

    float b0_ = 1.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

class Equalizer {
public:
    void Configure(uint32_t sampleRate, uint16_t channels);
    void UpdateSettings(const DspSettings& settings);
    void Process(float* interleaved, uint32_t frames);
    void Reset();

private:
    struct ChannelFilters {
        Biquad sub;
        Biquad punch;
        Biquad low;
        Biquad mid;
        Biquad high;
    };

    uint32_t sampleRate_ = 48000;
    uint16_t channels_ = 1;
    DspSettings settings_{};
    std::vector<ChannelFilters> filters_;
};

class PitchShifter {
public:
    void Configure(uint32_t sampleRate, uint16_t channels);
    void SetSemitones(float semitones);
    void Process(float* interleaved, uint32_t frames);
    void Reset();

private:
    struct ChannelState {
        int rover = 0;
        int latency = 0;
        std::vector<float> inFifo;
        std::vector<float> outFifo;
        std::vector<float> outputAccum;
        std::vector<float> lastPhase;
        std::vector<float> sumPhase;
        std::vector<float> analysisMagnitude;
        std::vector<float> analysisFrequency;
        std::vector<float> synthesisMagnitude;
        std::vector<float> synthesisFrequency;
        std::vector<float> fftWorkspace;
    };

    void ConfigureChannel(ChannelState& state);
    float ProcessSample(ChannelState& state, float sample);
    void ProcessFrame(ChannelState& state);
    static void Fft(std::vector<float>& data, int fftFrameSize, bool inverse);

    uint32_t sampleRate_ = 48000;
    uint16_t channels_ = 1;
    float pitchFactor_ = 1.0f;
    int fftFrameSize_ = 1024;
    int overlap_ = 8;
    std::vector<ChannelState> channelsState_;
};

class DspProcessor {
public:
    void Configure(uint32_t sampleRate, uint16_t channels);
    void UpdateSettings(const DspSettings& settings);
    void Process(float* interleaved, uint32_t frames);
    void Reset();

private:
    uint16_t channels_ = 1;
    DspSettings settings_{};
    Equalizer equalizer_;
    PitchShifter pitchShifter_;
};

}
