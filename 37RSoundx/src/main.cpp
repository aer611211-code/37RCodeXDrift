#include "Application.h"
#include "Utils.h"

#include <filesystem>
#include <fstream>
#include <objbase.h>
#include <windows.h>
#include <mmsystem.h>

namespace {

void WriteStartupLog(const std::wstring& message) {
    wchar_t modulePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
        return;
    }

    try {
        std::filesystem::path logPath(modulePath);
        logPath = logPath.parent_path() / L"37RCode-startup.log";
        std::wofstream log(logPath, std::ios::out | std::ios::trunc);
        if (log) {
            log << message << L"\n";
        }
    } catch (...) {
    }
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        const std::wstring message = L"COM konnte nicht initialisiert werden:\n" + sx::HResultToString(comResult);
        WriteStartupLog(message);
        MessageBoxW(nullptr, message.c_str(), L"37RCode", MB_ICONERROR);
        return 1;
    }

    timeBeginPeriod(1);
    int result = 0;
    {
        sx::Application app;
        if (!app.Initialize(instance, showCommand)) {
            std::wstring message = L"37RCode konnte nicht gestartet werden.";
            if (!app.LastError().empty()) {
                message += L"\n\n" + app.LastError();
            }
            WriteStartupLog(message);
            MessageBoxW(nullptr, message.c_str(), L"37RCode", MB_ICONERROR);
            result = 1;
        } else {
            result = app.Run();
        }
    }
    timeEndPeriod(1);

    if (uninitializeCom) {
        CoUninitialize();
    }
    return result;
}
