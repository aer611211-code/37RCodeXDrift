#include "AudioFormat.h"

#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>
#include <mmreg.h>

namespace sx {
namespace {

SampleEncoding ResolveEncoding(const WAVEFORMATEX* format, uint16_t& validBits) {
    if (!format) {
        return SampleEncoding::Unknown;
    }

    WORD tag = format->wFormatTag;
    GUID subFormat{};
    validBits = format->wBitsPerSample;

    if (tag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        subFormat = extensible->SubFormat;
        validBits = extensible->Samples.wValidBitsPerSample ? extensible->Samples.wValidBitsPerSample : format->wBitsPerSample;

        if (subFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            tag = WAVE_FORMAT_IEEE_FLOAT;
        } else if (subFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            tag = WAVE_FORMAT_PCM;
        }
    }

    if (tag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        return SampleEncoding::Float32;
    }
    if (tag == WAVE_FORMAT_PCM) {
        if (format->wBitsPerSample == 16) {
            return SampleEncoding::Pcm16;
        }
        if (format->wBitsPerSample == 24) {
            return SampleEncoding::Pcm24;
        }
        if (format->wBitsPerSample == 32) {
            return SampleEncoding::Pcm32;
        }
    }

    return SampleEncoding::Unknown;
}

float ReadPcm24(const BYTE* source) {
    int value = source[0] | (source[1] << 8) | (source[2] << 16);
    if (value & 0x800000) {
        value |= ~0xFFFFFF;
    }
    return static_cast<float>(value) / 8388608.0f;
}

float SampleAt(const float* input, uint32_t frames, uint16_t channels, float position, uint16_t channel) {
    if (!input || frames == 0 || channels == 0) {
        return 0.0f;
    }

    const uint32_t index = static_cast<uint32_t>(std::floor(position));
    const uint32_t nextIndex = std::min(index + 1, frames - 1);
    const float fraction = position - static_cast<float>(index);

    auto readMapped = [&](uint32_t frame, uint16_t outChannel) {
        if (outChannel < channels) {
            return input[static_cast<size_t>(frame) * channels + outChannel];
        }

        float sum = 0.0f;
        for (uint16_t inputChannel = 0; inputChannel < channels; ++inputChannel) {
            sum += input[static_cast<size_t>(frame) * channels + inputChannel];
        }
        return sum / static_cast<float>(channels);
    };

    const float a = readMapped(index, channel);
    const float b = readMapped(nextIndex, channel);
    return a + (b - a) * fraction;
}

float MixedSampleAt(const float* input, uint32_t frames, uint16_t inputChannels, float position, uint16_t outputChannel, uint16_t outputChannels) {
    if (inputChannels == outputChannels) {
        return SampleAt(input, frames, inputChannels, position, outputChannel);
    }
    if (inputChannels == 1) {
        return SampleAt(input, frames, inputChannels, position, 0);
    }
    if (outputChannels == 1) {
        float sum = 0.0f;
        for (uint16_t channel = 0; channel < inputChannels; ++channel) {
            sum += SampleAt(input, frames, inputChannels, position, channel);
        }
        return sum / static_cast<float>(inputChannels);
    }
    if (outputChannel < inputChannels) {
        return SampleAt(input, frames, inputChannels, position, outputChannel);
    }
    return MixedSampleAt(input, frames, inputChannels, position, 0, 1);
}

}

bool InspectWaveFormat(const WAVEFORMATEX* format, WaveFormatInfo& info) {
    if (!format || format->nChannels == 0 || format->nSamplesPerSec == 0 || format->nBlockAlign == 0) {
        return false;
    }

    uint16_t validBits = format->wBitsPerSample;
    const SampleEncoding encoding = ResolveEncoding(format, validBits);
    if (encoding == SampleEncoding::Unknown) {
        return false;
    }

    info.sampleRate = format->nSamplesPerSec;
    info.channels = format->nChannels;
    info.bitsPerSample = format->wBitsPerSample;
    info.validBitsPerSample = validBits;
    info.blockAlign = format->nBlockAlign;
    info.encoding = encoding;
    return true;
}

bool ReadFramesToFloat(const BYTE* source, uint32_t frames, const WAVEFORMATEX* format, bool silent, std::vector<float>& output) {
    WaveFormatInfo info;
    if (!InspectWaveFormat(format, info)) {
        return false;
    }

    output.assign(static_cast<size_t>(frames) * info.channels, 0.0f);
    if (silent || !source || frames == 0) {
        return true;
    }

    const uint16_t bytesPerSample = info.bitsPerSample / 8;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const BYTE* frameSource = source + static_cast<size_t>(frame) * info.blockAlign;
        for (uint16_t channel = 0; channel < info.channels; ++channel) {
            const BYTE* sample = frameSource + static_cast<size_t>(channel) * bytesPerSample;
            float value = 0.0f;

            switch (info.encoding) {
            case SampleEncoding::Float32:
                std::memcpy(&value, sample, sizeof(float));
                break;
            case SampleEncoding::Pcm16: {
                int16_t pcm = 0;
                std::memcpy(&pcm, sample, sizeof(pcm));
                value = static_cast<float>(pcm) / 32768.0f;
                break;
            }
            case SampleEncoding::Pcm24:
                value = ReadPcm24(sample);
                break;
            case SampleEncoding::Pcm32: {
                int32_t pcm = 0;
                std::memcpy(&pcm, sample, sizeof(pcm));
                value = static_cast<float>(pcm) / 2147483648.0f;
                break;
            }
            default:
                return false;
            }

            output[static_cast<size_t>(frame) * info.channels + channel] = std::isfinite(value) ? Clamp(value, -1.0f, 1.0f) : 0.0f;
        }
    }
    return true;
}

bool WriteFloatToFrames(const float* source, uint32_t frames, const WAVEFORMATEX* format, BYTE* destination) {
    WaveFormatInfo info;
    if (!InspectWaveFormat(format, info) || !destination) {
        return false;
    }

    const uint16_t bytesPerSample = info.bitsPerSample / 8;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        BYTE* frameDestination = destination + static_cast<size_t>(frame) * info.blockAlign;
        for (uint16_t channel = 0; channel < info.channels; ++channel) {
            BYTE* sample = frameDestination + static_cast<size_t>(channel) * bytesPerSample;
            const float value = source ? Clamp(source[static_cast<size_t>(frame) * info.channels + channel], -1.0f, 1.0f) : 0.0f;

            switch (info.encoding) {
            case SampleEncoding::Float32:
                std::memcpy(sample, &value, sizeof(value));
                break;
            case SampleEncoding::Pcm16: {
                const int16_t pcm = static_cast<int16_t>(Clamp(value, -1.0f, 0.999969f) * 32768.0f);
                std::memcpy(sample, &pcm, sizeof(pcm));
                break;
            }
            case SampleEncoding::Pcm24: {
                const int32_t pcm = static_cast<int32_t>(Clamp(value, -1.0f, 0.999999f) * 8388608.0f);
                sample[0] = static_cast<BYTE>(pcm & 0xFF);
                sample[1] = static_cast<BYTE>((pcm >> 8) & 0xFF);
                sample[2] = static_cast<BYTE>((pcm >> 16) & 0xFF);
                break;
            }
            case SampleEncoding::Pcm32: {
                const int32_t pcm = static_cast<int32_t>(Clamp(value, -1.0f, 0.999999f) * 2147483647.0f);
                std::memcpy(sample, &pcm, sizeof(pcm));
                break;
            }
            default:
                return false;
            }
        }
    }
    return true;
}

std::vector<float> ResampleAndMix(
    const float* input,
    uint32_t inputFrames,
    uint16_t inputChannels,
    uint32_t inputSampleRate,
    uint16_t outputChannels,
    uint32_t outputSampleRate) {

    if (!input || inputFrames == 0 || inputChannels == 0 || outputChannels == 0 || inputSampleRate == 0 || outputSampleRate == 0) {
        return {};
    }

    const double ratio = static_cast<double>(outputSampleRate) / static_cast<double>(inputSampleRate);
    const uint32_t outputFrames = std::max<uint32_t>(1, static_cast<uint32_t>(std::llround(static_cast<double>(inputFrames) * ratio)));
    std::vector<float> output(static_cast<size_t>(outputFrames) * outputChannels, 0.0f);

    for (uint32_t frame = 0; frame < outputFrames; ++frame) {
        const float inputPosition = static_cast<float>(static_cast<double>(frame) / ratio);
        for (uint16_t channel = 0; channel < outputChannels; ++channel) {
            output[static_cast<size_t>(frame) * outputChannels + channel] =
                MixedSampleAt(input, inputFrames, inputChannels, inputPosition, channel, outputChannels);
        }
    }

    return output;
}

}
