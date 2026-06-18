#pragma once

#include <Audioclient.h>
#include <cstdint>
#include <vector>

namespace sx {

enum class SampleEncoding {
    Unknown,
    Float32,
    Pcm16,
    Pcm24,
    Pcm32
};

struct WaveFormatInfo {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
    uint16_t bitsPerSample = 32;
    uint16_t validBitsPerSample = 32;
    uint16_t blockAlign = 8;
    SampleEncoding encoding = SampleEncoding::Unknown;
};

bool InspectWaveFormat(const WAVEFORMATEX* format, WaveFormatInfo& info);
bool ReadFramesToFloat(const BYTE* source, uint32_t frames, const WAVEFORMATEX* format, bool silent, std::vector<float>& output);
bool WriteFloatToFrames(const float* source, uint32_t frames, const WAVEFORMATEX* format, BYTE* destination);
std::vector<float> ResampleAndMix(
    const float* input,
    uint32_t inputFrames,
    uint16_t inputChannels,
    uint32_t inputSampleRate,
    uint16_t outputChannels,
    uint32_t outputSampleRate);

}
