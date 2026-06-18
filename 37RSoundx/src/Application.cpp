#include "Application.h"

#include "Utils.h"
#include "../resources/resource.h"

#include <algorithm>
#include <cmath>
#include <dbt.h>
#include <iomanip>
#include <sstream>
#include <windowsx.h>

namespace sx {
namespace {

constexpr wchar_t WindowClassName[] = L"37RCodeCoffeeWindow";
constexpr float TargetFrameSeconds = 1.0f / 240.0f;

std::wstring DeviceLabel(const AudioDeviceInfo& info) {
    std::wstring label = TrimDeviceName(info.name);
    if (info.isDefault) {
        label += L" (Default)";
    }
    return label;
}

int FindDeviceIndex(const std::vector<AudioDeviceInfo>& devices, const std::wstring& id) {
    if (id.empty()) {
        return -1;
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int DefaultDeviceIndex(const std::vector<AudioDeviceInfo>& devices) {
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].isDefault) {
            return static_cast<int>(i);
        }
    }
    return devices.empty() ? -1 : 0;
}

std::vector<std::wstring> LabelsForDevices(const std::vector<AudioDeviceInfo>& devices) {
    std::vector<std::wstring> labels;
    labels.reserve(devices.size());
    for (const auto& device : devices) {
        labels.push_back(DeviceLabel(device));
    }
    return labels;
}

}

Application::Application() = default;

Application::~Application() = default;

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    if (!CreateDeviceIndependentResources()) {
        return false;
    }
    InitializeControls();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.lpfnWndProc = Application::WindowProc;
    wc.lpszClassName = WindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    wc.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassExW(&wc)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            SetLastErrorText(L"RegisterClassExW fehlgeschlagen: " + LastErrorToString(error));
            return false;
        }
    }

    if (!wc.hCursor) {
        SetLastErrorText(L"Windows-Cursor konnte nicht geladen werden.");
        return false;
    }

    RECT rect{ 0, 0, 1060, 720 };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    hwnd_ = CreateWindowExW(
        0,
        WindowClassName,
        L"37RCode - Coffee Smooth Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_) {
        SetLastErrorText(L"CreateWindowExW fehlgeschlagen: " + LastErrorToString(GetLastError()));
        return false;
    }

    RefreshDeviceLists(false);
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    running_ = true;
    lastFrameTime_ = std::chrono::steady_clock::now();
    lastDeviceRefresh_ = lastFrameTime_;
    return true;
}

const std::wstring& Application::LastError() const {
    return lastError_;
}

int Application::Run() {
    MSG message{};
    int exitCode = 0;
    auto nextFrame = std::chrono::steady_clock::now();

    while (running_) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                exitCode = static_cast<int>(message.wParam);
                running_ = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextFrame) {
            Render();
            nextFrame = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<float>(TargetFrameSeconds));
        } else {
            Sleep(1);
        }
    }

    return exitCode;
}

LRESULT CALLBACK Application::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        if (app) {
            app->hwnd_ = hwnd;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCREATE:
        return TRUE;
    case WM_CREATE:
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_SIZE:
        if (renderTarget_) {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            renderTarget_->Resize(D2D1::SizeU(width, height));
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 560;
        info->ptMinTrackSize.y = 430;
        return 0;
    }
    case WM_MOUSEMOVE:
        OnMouseMove(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        dragging_ = true;
        OnMouseDown(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;
    case WM_LBUTTONUP:
        if (dragging_) {
            ReleaseCapture();
        }
        dragging_ = false;
        OnMouseUp();
        return 0;
    case WM_MOUSEWHEEL:
        if (headsetDropdown_.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)) ||
            microphoneDropdown_.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)) ||
            routerDropdown_.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam))) {
            return 0;
        }
        if (maxScroll_ > 0.0f) {
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            targetScroll_ = Clamp(targetScroll_ - (static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA)) * 72.0f, 0.0f, maxScroll_);
            return 0;
        }
        break;
    case WM_DEVICECHANGE:
        RefreshDeviceLists(true);
        return 0;
    case WM_PAINT:
        Render();
        ValidateRect(hwnd_, nullptr);
        return 0;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

bool Application::CreateDeviceIndependentResources() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_));
    if (FAILED(hr)) {
        SetLastErrorText(L"D2D1CreateFactory fehlgeschlagen: " + HResultToString(hr));
        return false;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        SetLastErrorText(L"DWriteCreateFactory fehlgeschlagen: " + HResultToString(hr));
        return false;
    }

    auto makeFormat = [&](float size, DWRITE_FONT_WEIGHT weight, Microsoft::WRL::ComPtr<IDWriteTextFormat>& target) {
        HRESULT formatHr = dwriteFactory_->CreateTextFormat(
            L"Segoe UI Variable",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"",
            &target);
        if (FAILED(formatHr)) {
            formatHr = dwriteFactory_->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"",
                &target);
        }
        if (SUCCEEDED(formatHr)) {
            target->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            target->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        return formatHr;
    };

    hr = makeFormat(32.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, titleFormat_);
    if (FAILED(hr)) {
        SetLastErrorText(L"Title-Schrift konnte nicht erstellt werden: " + HResultToString(hr));
        return false;
    }
    hr = makeFormat(18.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, sectionFormat_);
    if (FAILED(hr)) {
        SetLastErrorText(L"Section-Schrift konnte nicht erstellt werden: " + HResultToString(hr));
        return false;
    }
    hr = makeFormat(15.0f, DWRITE_FONT_WEIGHT_MEDIUM, labelFormat_);
    if (FAILED(hr)) {
        SetLastErrorText(L"Label-Schrift konnte nicht erstellt werden: " + HResultToString(hr));
        return false;
    }
    hr = makeFormat(13.0f, DWRITE_FONT_WEIGHT_NORMAL, smallFormat_);
    if (FAILED(hr)) {
        SetLastErrorText(L"Small-Schrift konnte nicht erstellt werden: " + HResultToString(hr));
        return false;
    }
    return true;
}

HRESULT Application::CreateDeviceResources() {
    if (renderTarget_) {
        return S_OK;
    }

    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rect.right - rect.left), static_cast<UINT32>(rect.bottom - rect.top));

    HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &renderTarget_);
    if (FAILED(hr)) {
        hr = d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_NONE),
            &renderTarget_);
    }
    if (FAILED(hr)) {
        return hr;
    }

    hr = renderTarget_->CreateSolidColorBrush(theme_.text, &fillBrush_);
    if (FAILED(hr)) {
        return hr;
    }
    return renderTarget_->CreateSolidColorBrush(theme_.panelStroke, &strokeBrush_);
}

void Application::DiscardDeviceResources() {
    fillBrush_.Reset();
    strokeBrush_.Reset();
    renderTarget_.Reset();
}

void Application::InitializeControls() {
    headsetDropdown_.Configure(L"Headset");
    microphoneDropdown_.Configure(L"Microphone");
    routerDropdown_.Configure(L"Output / Router");

    headsetDropdown_.SetOnChanged([this](int) { ApplySelectedDevices(); });
    microphoneDropdown_.SetOnChanged([this](int) { ApplySelectedDevices(); });
    routerDropdown_.SetOnChanged([this](int) { ApplySelectedDevices(); });

    volumeSlider_.Configure(L"Volume", 0.0f, 2.0f, 1.0f, L"%");
    bassSlider_.Configure(L"Bass", -36.0f, 36.0f, 0.0f, L"dB");
    midSlider_.Configure(L"Mid", -12.0f, 12.0f, 0.0f, L"dB");
    trebleSlider_.Configure(L"Treble", -12.0f, 12.0f, 0.0f, L"dB");
    pitchSlider_.Configure(L"Pitch", -12.0f, 12.0f, 0.0f, L"st");

    auto sliderChanged = [this](float) { UpdateDspFromControls(); };
    volumeSlider_.SetOnChanged(sliderChanged);
    bassSlider_.SetOnChanged(sliderChanged);
    midSlider_.SetOnChanged(sliderChanged);
    trebleSlider_.SetOnChanged(sliderChanged);
    pitchSlider_.SetOnChanged(sliderChanged);

    micMonitoringToggle_.Configure(L"Mic Monitoring", false);
    pcMonitoringToggle_.Configure(L"PC-Sound Monitoring", false);
    micMonitoringToggle_.SetOnChanged([this](bool enabled) { audioEngine_.SetMicMonitoring(enabled); });
    pcMonitoringToggle_.SetOnChanged([this](bool enabled) { audioEngine_.SetPcMonitoring(enabled); });

    UpdateDspFromControls();
}

void Application::RefreshDeviceLists(bool preserveSelection) {
    const std::wstring previousHeadset = preserveSelection ? SelectedHeadsetId() : L"";
    const std::wstring previousMic = preserveSelection ? SelectedMicrophoneId() : L"";
    const std::wstring previousRouter = preserveSelection ? SelectedRouterId() : L"";

    renderDevices_ = deviceManager_.Enumerate(eRender);
    captureDevices_ = deviceManager_.Enumerate(eCapture);

    int headsetIndex = FindDeviceIndex(renderDevices_, previousHeadset);
    if (headsetIndex < 0) {
        headsetIndex = DefaultDeviceIndex(renderDevices_);
    }

    int micIndex = FindDeviceIndex(captureDevices_, previousMic);
    if (micIndex < 0) {
        micIndex = DefaultDeviceIndex(captureDevices_);
    }

    int routerIndex = FindDeviceIndex(renderDevices_, previousRouter);
    if (routerIndex < 0) {
        routerIndex = DefaultDeviceIndex(renderDevices_);
        if (renderDevices_.size() > 1 && routerIndex == headsetIndex) {
            routerIndex = routerIndex == 0 ? 1 : 0;
        }
    }

    headsetDropdown_.SetItems(LabelsForDevices(renderDevices_), headsetIndex);
    microphoneDropdown_.SetItems(LabelsForDevices(captureDevices_), micIndex);
    routerDropdown_.SetItems(LabelsForDevices(renderDevices_), routerIndex);
    ApplySelectedDevices();
}

void Application::ApplySelectedDevices() {
    audioEngine_.SetDevices(SelectedHeadsetId(), SelectedMicrophoneId(), SelectedRouterId());
}

void Application::UpdateDspFromControls() {
    DspSettings settings;
    settings.volume = volumeSlider_.Value();
    settings.bassDb = bassSlider_.Value();
    settings.midDb = midSlider_.Value();
    settings.trebleDb = trebleSlider_.Value();
    settings.pitchSemitones = pitchSlider_.Value();
    audioEngine_.SetDspSettings(settings);
}

void Application::LayoutControls(float width, float height) {
    const float margin = 30.0f;
    const float gap = 22.0f;
    const float headerH = 70.0f;
    const float top = headerH + 14.0f;
    const bool compact = width < 900.0f;

    if (compact) {
        const float panelW = std::max(360.0f, width - margin * 2.0f);
        devicesPanel_ = { margin, top, panelW, 326.0f };
        micPanel_ = { margin, devicesPanel_.y + devicesPanel_.h + gap, panelW, 426.0f };
        monitoringPanel_ = { margin, micPanel_.y + micPanel_.h + gap, panelW, 178.0f };
        statusPanel_ = { margin, monitoringPanel_.y + monitoringPanel_.h + gap, panelW, 132.0f };
    } else {
        const float leftW = std::min(340.0f, width * 0.36f);
        const float rightX = margin + leftW + gap;
        const float rightW = std::max(420.0f, width - rightX - margin);
        const float usableH = std::max(520.0f, height - headerH - margin * 1.5f);

        devicesPanel_ = { margin, top, leftW, 326.0f };
        monitoringPanel_ = { margin, devicesPanel_.y + devicesPanel_.h + gap, leftW, std::max(178.0f, usableH - devicesPanel_.h - gap) };
        micPanel_ = { rightX, top, rightW, 426.0f };
        statusPanel_ = { rightX, micPanel_.y + micPanel_.h + gap, rightW, 132.0f };
    }

    contentHeight_ = std::max(monitoringPanel_.y + monitoringPanel_.h, statusPanel_.y + statusPanel_.h) + margin;
    maxScroll_ = std::max(0.0f, contentHeight_ - height);
    targetScroll_ = Clamp(targetScroll_, 0.0f, maxScroll_);
    contentScroll_ = Clamp(contentScroll_, 0.0f, maxScroll_);

    const float controlX = devicesPanel_.x + 22.0f;
    const float controlW = devicesPanel_.w - 44.0f;
    headsetDropdown_.SetRect({ controlX, devicesPanel_.y + 74.0f, controlW, 46.0f });
    microphoneDropdown_.SetRect({ controlX, devicesPanel_.y + 158.0f, controlW, 46.0f });
    routerDropdown_.SetRect({ controlX, devicesPanel_.y + 242.0f, controlW, 46.0f });

    const float sliderX = micPanel_.x + 28.0f;
    const float sliderW = micPanel_.w - 56.0f;
    const float sliderY = micPanel_.y + 72.0f;
    volumeSlider_.SetRect({ sliderX, sliderY, sliderW, 56.0f });
    bassSlider_.SetRect({ sliderX, sliderY + 68.0f, sliderW, 56.0f });
    midSlider_.SetRect({ sliderX, sliderY + 136.0f, sliderW, 56.0f });
    trebleSlider_.SetRect({ sliderX, sliderY + 204.0f, sliderW, 56.0f });
    pitchSlider_.SetRect({ sliderX, sliderY + 272.0f, sliderW, 56.0f });

    micMonitoringToggle_.SetRect({ monitoringPanel_.x + 22.0f, monitoringPanel_.y + 76.0f, monitoringPanel_.w - 44.0f, 34.0f });
    pcMonitoringToggle_.SetRect({ monitoringPanel_.x + 22.0f, monitoringPanel_.y + 126.0f, monitoringPanel_.w - 44.0f, 34.0f });
}

void Application::Render() {
    if (!hwnd_ || FAILED(CreateDeviceResources())) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(0.05f, std::chrono::duration<float>(now - lastFrameTime_).count());
    lastFrameTime_ = now;

    if (now - lastDeviceRefresh_ > std::chrono::seconds(2) &&
        !headsetDropdown_.IsOpen() && !microphoneDropdown_.IsOpen() && !routerDropdown_.IsOpen()) {
        RefreshDeviceLists(true);
        lastDeviceRefresh_ = now;
    }

    D2D1_SIZE_F size = renderTarget_->GetSize();
    LayoutControls(size.width, size.height);
    contentScroll_ = Lerp(contentScroll_, targetScroll_, 1.0f - std::exp(-10.5f * dt));
    const float logicalMouseY = LogicalMouseY(mouseY_);

    headsetDropdown_.Update(dt, mouseX_, logicalMouseY);
    microphoneDropdown_.Update(dt, mouseX_, logicalMouseY);
    routerDropdown_.Update(dt, mouseX_, logicalMouseY);
    volumeSlider_.Update(dt, mouseX_, logicalMouseY);
    bassSlider_.Update(dt, mouseX_, logicalMouseY);
    midSlider_.Update(dt, mouseX_, logicalMouseY);
    trebleSlider_.Update(dt, mouseX_, logicalMouseY);
    pitchSlider_.Update(dt, mouseX_, logicalMouseY);
    micMonitoringToggle_.Update(dt, mouseX_, logicalMouseY);
    pcMonitoringToggle_.Update(dt, mouseX_, logicalMouseY);

    RenderContext context;
    context.target = renderTarget_.Get();
    context.fill = fillBrush_.Get();
    context.stroke = strokeBrush_.Get();
    context.titleFormat = titleFormat_.Get();
    context.sectionFormat = sectionFormat_.Get();
    context.labelFormat = labelFormat_.Get();
    context.smallFormat = smallFormat_.Get();
    context.theme = theme_;

    renderTarget_->BeginDraw();
    renderTarget_->Clear(theme_.background);

    context.FillRect({ 0.0f, 0.0f, size.width, 68.0f }, Color(0.145f, 0.100f, 0.070f));
    context.Text(L"37RCode", titleFormat_.Get(), { 30.0f, 17.0f, 260.0f, 42.0f }, theme_.text);
    context.Text(L"Coffee Smooth Audio Monitor", labelFormat_.Get(), { 276.0f, 31.0f, 360.0f, 28.0f }, theme_.gold);

    renderTarget_->PushAxisAlignedClip(D2D1::RectF(0.0f, 68.0f, size.width, size.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -contentScroll_));

    DrawPanel(context, devicesPanel_, L"Devices");
    DrawPanel(context, micPanel_, L"Mic Settings");
    DrawPanel(context, monitoringPanel_, L"Monitoring");
    DrawPanel(context, statusPanel_, L"Status");

    headsetDropdown_.DrawBase(context);
    microphoneDropdown_.DrawBase(context);
    routerDropdown_.DrawBase(context);

    volumeSlider_.Draw(context);
    bassSlider_.Draw(context);
    midSlider_.Draw(context);
    trebleSlider_.Draw(context);
    pitchSlider_.Draw(context);

    micMonitoringToggle_.Draw(context);
    pcMonitoringToggle_.Draw(context);

    const std::wstring status = audioEngine_.StatusText();
    context.Text(status, labelFormat_.Get(), { statusPanel_.x + 24.0f, statusPanel_.y + 70.0f, statusPanel_.w - 48.0f, 34.0f }, theme_.text);
    context.Text(L"Default PC sound source is the current Windows default output. Routes with identical source and router are blocked.", smallFormat_.Get(),
        { statusPanel_.x + 24.0f, statusPanel_.y + 98.0f, statusPanel_.w - 48.0f, 24.0f }, theme_.mutedText);

    headsetDropdown_.DrawMenu(context);
    microphoneDropdown_.DrawMenu(context);
    routerDropdown_.DrawMenu(context);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->PopAxisAlignedClip();

    if (maxScroll_ > 1.0f) {
        const RectF track{ size.width - 10.0f, 78.0f, 4.0f, std::max(40.0f, size.height - 92.0f) };
        const float thumbH = std::max(32.0f, track.h * (size.height / std::max(size.height, contentHeight_)));
        const float thumbY = track.y + (track.h - thumbH) * (contentScroll_ / maxScroll_);
        context.FillRounded(track, Color(0.260f, 0.185f, 0.125f, 0.70f), 2.0f);
        context.FillRounded({ track.x - 1.0f, thumbY, 6.0f, thumbH }, Color(theme_.gold.r, theme_.gold.g, theme_.gold.b, 0.82f), 3.0f);
    }

    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void Application::DrawPanel(RenderContext& context, const RectF& rect, const std::wstring& title) {
    context.FillRounded(rect, theme_.panel, 8.0f);
    context.StrokeRounded(rect, theme_.panelStroke, 8.0f);
    context.Text(title, sectionFormat_.Get(), { rect.x + 22.0f, rect.y + 22.0f, rect.w - 44.0f, 28.0f }, theme_.gold);
}

void Application::OnMouseDown(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    const float logicalY = LogicalMouseY(y);

    DropdownControl* dropdowns[] = { &routerDropdown_, &microphoneDropdown_, &headsetDropdown_ };
    for (auto* dropdown : dropdowns) {
        if (dropdown->IsOpen() && dropdown->OnMouseDown(x, logicalY)) {
            CloseDropdownsExcept(dropdown);
            return;
        }
    }

    if (headsetDropdown_.OnMouseDown(x, logicalY)) {
        CloseDropdownsExcept(headsetDropdown_.IsOpen() ? &headsetDropdown_ : nullptr);
        return;
    }
    if (microphoneDropdown_.OnMouseDown(x, logicalY)) {
        CloseDropdownsExcept(microphoneDropdown_.IsOpen() ? &microphoneDropdown_ : nullptr);
        return;
    }
    if (routerDropdown_.OnMouseDown(x, logicalY)) {
        CloseDropdownsExcept(routerDropdown_.IsOpen() ? &routerDropdown_ : nullptr);
        return;
    }

    CloseDropdownsExcept(nullptr);

    if (volumeSlider_.OnMouseDown(x, logicalY) ||
        bassSlider_.OnMouseDown(x, logicalY) ||
        midSlider_.OnMouseDown(x, logicalY) ||
        trebleSlider_.OnMouseDown(x, logicalY) ||
        pitchSlider_.OnMouseDown(x, logicalY) ||
        micMonitoringToggle_.OnMouseDown(x, logicalY) ||
        pcMonitoringToggle_.OnMouseDown(x, logicalY)) {
        return;
    }
}

void Application::OnMouseMove(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    const float logicalY = LogicalMouseY(y);
    volumeSlider_.OnMouseMove(x, logicalY);
    bassSlider_.OnMouseMove(x, logicalY);
    midSlider_.OnMouseMove(x, logicalY);
    trebleSlider_.OnMouseMove(x, logicalY);
    pitchSlider_.OnMouseMove(x, logicalY);
}

void Application::OnMouseUp() {
    volumeSlider_.OnMouseUp();
    bassSlider_.OnMouseUp();
    midSlider_.OnMouseUp();
    trebleSlider_.OnMouseUp();
    pitchSlider_.OnMouseUp();
}

void Application::CloseDropdownsExcept(DropdownControl* keep) {
    if (&headsetDropdown_ != keep) {
        headsetDropdown_.Close();
    }
    if (&microphoneDropdown_ != keep) {
        microphoneDropdown_.Close();
    }
    if (&routerDropdown_ != keep) {
        routerDropdown_.Close();
    }
}

void Application::SetLastErrorText(const std::wstring& text) {
    lastError_ = text;
}

float Application::LogicalMouseY(float y) const {
    return y >= 68.0f ? y + contentScroll_ : y;
}

std::wstring Application::SelectedHeadsetId() const {
    const int index = headsetDropdown_.SelectedIndex();
    return index >= 0 && index < static_cast<int>(renderDevices_.size()) ? renderDevices_[index].id : L"";
}

std::wstring Application::SelectedMicrophoneId() const {
    const int index = microphoneDropdown_.SelectedIndex();
    return index >= 0 && index < static_cast<int>(captureDevices_.size()) ? captureDevices_[index].id : L"";
}

std::wstring Application::SelectedRouterId() const {
    const int index = routerDropdown_.SelectedIndex();
    return index >= 0 && index < static_cast<int>(renderDevices_.size()) ? renderDevices_[index].id : L"";
}

}
