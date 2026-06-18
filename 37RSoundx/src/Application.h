#pragma once

#include "AudioDeviceManager.h"
#include "AudioEngine.h"
#include "Controls.h"

#include <chrono>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace sx {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    const std::wstring& LastError() const;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();
    void InitializeControls();
    void RefreshDeviceLists(bool preserveSelection);
    void ApplySelectedDevices();
    void UpdateDspFromControls();
    void LayoutControls(float width, float height);
    void Render();
    void DrawPanel(RenderContext& context, const RectF& rect, const std::wstring& title);
    void OnMouseDown(float x, float y);
    void OnMouseMove(float x, float y);
    void OnMouseUp();
    void CloseDropdownsExcept(DropdownControl* keep);
    void SetLastErrorText(const std::wstring& text);
    float LogicalMouseY(float y) const;

    std::wstring SelectedHeadsetId() const;
    std::wstring SelectedMicrophoneId() const;
    std::wstring SelectedRouterId() const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool running_ = false;
    bool dragging_ = false;
    float mouseX_ = -1000.0f;
    float mouseY_ = -1000.0f;
    float contentScroll_ = 0.0f;
    float targetScroll_ = 0.0f;
    float maxScroll_ = 0.0f;
    float contentHeight_ = 0.0f;
    std::wstring lastError_;

    Theme theme_ = CoffeeTheme();
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> strokeBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> sectionFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;

    AudioDeviceManager deviceManager_;
    AudioEngine audioEngine_;
    std::vector<AudioDeviceInfo> renderDevices_;
    std::vector<AudioDeviceInfo> captureDevices_;

    DropdownControl headsetDropdown_;
    DropdownControl microphoneDropdown_;
    DropdownControl routerDropdown_;
    SliderControl volumeSlider_;
    SliderControl bassSlider_;
    SliderControl midSlider_;
    SliderControl trebleSlider_;
    SliderControl pitchSlider_;
    ToggleControl micMonitoringToggle_;
    ToggleControl pcMonitoringToggle_;

    RectF devicesPanel_{};
    RectF micPanel_{};
    RectF monitoringPanel_{};
    RectF statusPanel_{};
    std::chrono::steady_clock::time_point lastFrameTime_;
    std::chrono::steady_clock::time_point lastDeviceRefresh_;
};

}
