#pragma once

#include "AudioDeviceManager.h"
#include "Dsp.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace sx {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void SetDevices(const std::wstring& headsetRenderId, const std::wstring& microphoneId, const std::wstring& routerRenderId);
    void SetMicMonitoring(bool enabled);
    void SetPcMonitoring(bool enabled);
    bool IsMicMonitoringEnabled() const;
    bool IsPcMonitoringEnabled() const;

    void SetDspSettings(const DspSettings& settings);
    DspSettings GetDspSettings() const;

    std::wstring StatusText() const;

private:
    class AudioPipe;

    void RestartMicPipeLocked();
    void RestartPcPipeLocked();
    void StopMicPipeLocked();
    void StopPcPipeLocked();
    void ReportStatus(const std::wstring& text);

    AudioDeviceManager deviceManager_;
    mutable std::mutex configMutex_;
    mutable std::mutex settingsMutex_;
    mutable std::mutex statusMutex_;

    std::wstring headsetRenderId_;
    std::wstring microphoneId_;
    std::wstring routerRenderId_;

    bool micMonitoring_ = false;
    bool pcMonitoring_ = false;
    DspSettings settings_{};
    std::wstring statusText_ = L"Bereit";

    std::unique_ptr<AudioPipe> micPipe_;
    std::unique_ptr<AudioPipe> pcPipe_;
};

}
