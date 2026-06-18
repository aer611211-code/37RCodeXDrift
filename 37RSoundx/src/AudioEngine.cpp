#include "AudioEngine.h"

#include "AudioFormat.h"
#include "Utils.h"

#include <Audioclient.h>
#include <avrt.h>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <wrl/client.h>

namespace sx {
namespace {

using Microsoft::WRL::ComPtr;

struct CoTaskMemWaveFormatDeleter {
    void operator()(WAVEFORMATEX* format) const {
        if (format) {
            CoTaskMemFree(format);
        }
    }
};

using WaveFormatPtr = std::unique_ptr<WAVEFORMATEX, CoTaskMemWaveFormatDeleter>;

struct AudioFailure final : std::runtime_error {
    AudioFailure(HRESULT result, std::wstring where)
        : std::runtime_error("audio failure"), hr(result), step(std::move(where)) {
    }

    HRESULT hr;
    std::wstring step;
};

void ThrowIfFailed(HRESULT hr, const wchar_t* step) {
    if (FAILED(hr)) {
        throw AudioFailure(hr, step);
    }
}

WaveFormatPtr GetMixFormat(IAudioClient* client) {
    WAVEFORMATEX* rawFormat = nullptr;
    ThrowIfFailed(client->GetMixFormat(&rawFormat), L"GetMixFormat");
    return WaveFormatPtr(rawFormat);
}

}

class AudioEngine::AudioPipe {
public:
    struct Config {
        std::wstring name;
        std::wstring captureDeviceId;
        std::wstring renderDeviceId;
        bool loopbackCapture = false;
        bool applyDsp = false;
        std::function<DspSettings()> settingsProvider;
        std::function<void(const std::wstring&)> statusCallback;
    };

    ~AudioPipe() {
        Stop();
    }

    void Start(Config config) {
        Stop();
        config_ = std::move(config);
        running_.store(true);
        worker_ = std::thread([this]() { Run(); });
    }

    void Stop() {
        running_.store(false);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void Run() {
        HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitializeCom = SUCCEEDED(coResult);

        HANDLE mmcssHandle = nullptr;
        DWORD taskIndex = 0;

        try {
            if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
                ThrowIfFailed(coResult, L"CoInitializeEx");
            }

            mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
            RunWasapiLoop();

            if (config_.statusCallback) {
                config_.statusCallback(config_.name + L": gestoppt");
            }
        } catch (const AudioFailure& failure) {
            if (config_.statusCallback) {
                config_.statusCallback(config_.name + L": " + failure.step + L" fehlgeschlagen (" + HResultToString(failure.hr) + L")");
            }
        } catch (const std::exception&) {
            if (config_.statusCallback) {
                config_.statusCallback(config_.name + L": unerwarteter Audiofehler");
            }
        }

        if (mmcssHandle) {
            AvRevertMmThreadCharacteristics(mmcssHandle);
        }
        if (uninitializeCom) {
            CoUninitialize();
        }
        running_.store(false);
    }

    void RunWasapiLoop() {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ThrowIfFailed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), L"MMDeviceEnumerator");

        ComPtr<IMMDevice> captureDevice;
        ComPtr<IMMDevice> renderDevice;
        ThrowIfFailed(enumerator->GetDevice(config_.captureDeviceId.c_str(), &captureDevice), L"Capture device");
        ThrowIfFailed(enumerator->GetDevice(config_.renderDeviceId.c_str(), &renderDevice), L"Render device");

        ComPtr<IAudioClient> captureClient;
        ComPtr<IAudioClient> renderClient;
        ThrowIfFailed(captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(captureClient.GetAddressOf())), L"Capture Activate");
        ThrowIfFailed(renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(renderClient.GetAddressOf())), L"Render Activate");

        WaveFormatPtr captureFormat = GetMixFormat(captureClient.Get());
        WaveFormatPtr renderFormat = GetMixFormat(renderClient.Get());

        WaveFormatInfo captureInfo;
        WaveFormatInfo renderInfo;
        if (!InspectWaveFormat(captureFormat.get(), captureInfo)) {
            throw AudioFailure(AUDCLNT_E_UNSUPPORTED_FORMAT, L"Capture format");
        }
        if (!InspectWaveFormat(renderFormat.get(), renderInfo)) {
            throw AudioFailure(AUDCLNT_E_UNSUPPORTED_FORMAT, L"Render format");
        }

        constexpr REFERENCE_TIME bufferDuration = 20 * 10000;
        const DWORD captureFlags = config_.loopbackCapture ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        ThrowIfFailed(captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED, captureFlags, bufferDuration, 0, captureFormat.get(), nullptr), L"Capture Initialize");
        ThrowIfFailed(renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, renderFormat.get(), nullptr), L"Render Initialize");

        ComPtr<IAudioCaptureClient> capture;
        ComPtr<IAudioRenderClient> render;
        ThrowIfFailed(captureClient->GetService(IID_PPV_ARGS(&capture)), L"Capture service");
        ThrowIfFailed(renderClient->GetService(IID_PPV_ARGS(&render)), L"Render service");

        UINT32 renderBufferFrames = 0;
        ThrowIfFailed(renderClient->GetBufferSize(&renderBufferFrames), L"Render buffer size");

        BYTE* silence = nullptr;
        if (SUCCEEDED(render->GetBuffer(renderBufferFrames, &silence))) {
            render->ReleaseBuffer(renderBufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        DspProcessor dsp;
        if (config_.applyDsp) {
            dsp.Configure(captureInfo.sampleRate, captureInfo.channels);
            if (config_.settingsProvider) {
                dsp.UpdateSettings(config_.settingsProvider());
            }
        }

        ThrowIfFailed(renderClient->Start(), L"Render Start");
        ThrowIfFailed(captureClient->Start(), L"Capture Start");

        if (config_.statusCallback) {
            config_.statusCallback(config_.name + L": aktiv");
        }

        std::vector<float> captured;
        while (running_.load()) {
            UINT32 packetFrames = 0;
            HRESULT hr = capture->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                throw AudioFailure(hr, L"GetNextPacketSize");
            }

            if (packetFrames == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            while (packetFrames > 0 && running_.load()) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    throw AudioFailure(hr, L"Capture GetBuffer");
                }

                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const bool discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
                if (!ReadFramesToFloat(data, frames, captureFormat.get(), silent, captured)) {
                    capture->ReleaseBuffer(frames);
                    throw AudioFailure(AUDCLNT_E_UNSUPPORTED_FORMAT, L"Capture convert");
                }

                if (config_.applyDsp) {
                    if (discontinuity) {
                        dsp.Reset();
                    }
                    if (config_.settingsProvider) {
                        dsp.UpdateSettings(config_.settingsProvider());
                    }
                    dsp.Process(captured.data(), frames);
                }

                const std::vector<float> rendered = ResampleAndMix(
                    captured.data(),
                    frames,
                    captureInfo.channels,
                    captureInfo.sampleRate,
                    renderInfo.channels,
                    renderInfo.sampleRate);

                const uint32_t outputFrames = static_cast<uint32_t>(rendered.size() / renderInfo.channels);
                if (outputFrames > 0) {
                    UINT32 padding = 0;
                    hr = renderClient->GetCurrentPadding(&padding);
                    if (FAILED(hr)) {
                        capture->ReleaseBuffer(frames);
                        throw AudioFailure(hr, L"Render padding");
                    }

                    const UINT32 available = renderBufferFrames > padding ? renderBufferFrames - padding : 0;
                    const UINT32 framesToWrite = std::min<UINT32>(available, outputFrames);
                    if (framesToWrite > 0) {
                        const UINT32 startFrame = outputFrames - framesToWrite;
                        BYTE* renderData = nullptr;
                        hr = render->GetBuffer(framesToWrite, &renderData);
                        if (FAILED(hr)) {
                            capture->ReleaseBuffer(frames);
                            throw AudioFailure(hr, L"Render GetBuffer");
                        }

                        const float* outputStart = rendered.data() + static_cast<size_t>(startFrame) * renderInfo.channels;
                        if (!WriteFloatToFrames(outputStart, framesToWrite, renderFormat.get(), renderData)) {
                            render->ReleaseBuffer(framesToWrite, AUDCLNT_BUFFERFLAGS_SILENT);
                            capture->ReleaseBuffer(frames);
                            throw AudioFailure(AUDCLNT_E_UNSUPPORTED_FORMAT, L"Render convert");
                        }
                        render->ReleaseBuffer(framesToWrite, 0);
                    }
                }

                hr = capture->ReleaseBuffer(frames);
                if (FAILED(hr)) {
                    throw AudioFailure(hr, L"Capture ReleaseBuffer");
                }

                hr = capture->GetNextPacketSize(&packetFrames);
                if (FAILED(hr)) {
                    throw AudioFailure(hr, L"GetNextPacketSize");
                }
            }
        }

        captureClient->Stop();
        renderClient->Stop();
    }

    Config config_;
    std::atomic<bool> running_{ false };
    std::thread worker_;
};

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    std::lock_guard<std::mutex> lock(configMutex_);
    StopMicPipeLocked();
    StopPcPipeLocked();
}

void AudioEngine::SetDevices(const std::wstring& headsetRenderId, const std::wstring& microphoneId, const std::wstring& routerRenderId) {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (headsetRenderId_ == headsetRenderId && microphoneId_ == microphoneId && routerRenderId_ == routerRenderId) {
        return;
    }

    headsetRenderId_ = headsetRenderId;
    microphoneId_ = microphoneId;
    routerRenderId_ = routerRenderId;

    if (micMonitoring_) {
        RestartMicPipeLocked();
    }
    if (pcMonitoring_) {
        RestartPcPipeLocked();
    }
}

void AudioEngine::SetMicMonitoring(bool enabled) {
    std::lock_guard<std::mutex> lock(configMutex_);
    micMonitoring_ = enabled;
    if (enabled) {
        RestartMicPipeLocked();
    } else {
        StopMicPipeLocked();
        ReportStatus(L"Mic Monitoring: aus");
    }
}

void AudioEngine::SetPcMonitoring(bool enabled) {
    std::lock_guard<std::mutex> lock(configMutex_);
    pcMonitoring_ = enabled;
    if (enabled) {
        RestartPcPipeLocked();
    } else {
        StopPcPipeLocked();
        ReportStatus(L"PC-Sound Monitoring: aus");
    }
}

bool AudioEngine::IsMicMonitoringEnabled() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return micMonitoring_;
}

bool AudioEngine::IsPcMonitoringEnabled() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return pcMonitoring_;
}

void AudioEngine::SetDspSettings(const DspSettings& settings) {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_ = settings;
}

DspSettings AudioEngine::GetDspSettings() const {
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

std::wstring AudioEngine::StatusText() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return statusText_;
}

void AudioEngine::RestartMicPipeLocked() {
    StopMicPipeLocked();

    if (microphoneId_.empty() || headsetRenderId_.empty()) {
        ReportStatus(L"Mic Monitoring: Mikrofon oder Headset fehlt");
        return;
    }
    if (!deviceManager_.DeviceExists(microphoneId_) || !deviceManager_.DeviceExists(headsetRenderId_)) {
        ReportStatus(L"Mic Monitoring: Gerät nicht verfügbar");
        return;
    }

    auto pipe = std::make_unique<AudioPipe>();
    AudioPipe::Config config;
    config.name = L"Mic Monitoring";
    config.captureDeviceId = microphoneId_;
    config.renderDeviceId = headsetRenderId_;
    config.applyDsp = true;
    config.loopbackCapture = false;
    config.settingsProvider = [this]() { return GetDspSettings(); };
    config.statusCallback = [this](const std::wstring& text) { ReportStatus(text); };
    pipe->Start(std::move(config));
    micPipe_ = std::move(pipe);
}

void AudioEngine::RestartPcPipeLocked() {
    StopPcPipeLocked();

    const std::wstring sourceRenderId = deviceManager_.GetDefaultDeviceId(eRender);
    if (sourceRenderId.empty() || routerRenderId_.empty()) {
        ReportStatus(L"PC-Sound Monitoring: Quelle oder Router fehlt");
        return;
    }
    if (sourceRenderId == routerRenderId_) {
        ReportStatus(L"PC-Sound Monitoring: Quelle und Router sind identisch, Route verhindert");
        return;
    }
    if (!deviceManager_.DeviceExists(sourceRenderId) || !deviceManager_.DeviceExists(routerRenderId_)) {
        ReportStatus(L"PC-Sound Monitoring: Gerät nicht verfügbar");
        return;
    }

    auto pipe = std::make_unique<AudioPipe>();
    AudioPipe::Config config;
    config.name = L"PC-Sound Monitoring";
    config.captureDeviceId = sourceRenderId;
    config.renderDeviceId = routerRenderId_;
    config.applyDsp = false;
    config.loopbackCapture = true;
    config.statusCallback = [this](const std::wstring& text) { ReportStatus(text); };
    pipe->Start(std::move(config));
    pcPipe_ = std::move(pipe);
}

void AudioEngine::StopMicPipeLocked() {
    if (micPipe_) {
        micPipe_->Stop();
        micPipe_.reset();
    }
}

void AudioEngine::StopPcPipeLocked() {
    if (pcPipe_) {
        pcPipe_->Stop();
        pcPipe_.reset();
    }
}

void AudioEngine::ReportStatus(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    statusText_ = text;
}

}
