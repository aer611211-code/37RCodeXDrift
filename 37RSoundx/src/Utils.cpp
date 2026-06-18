#include "Utils.h"

#include <algorithm>
#include <sstream>

namespace sx {

std::wstring LastErrorToString(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return L"OK";
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = length && buffer ? std::wstring(buffer, length) : L"Unknown Windows error";
    if (buffer) {
        LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring HResultToString(HRESULT hr) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << static_cast<unsigned long>(hr) << L" - " << LastErrorToString(static_cast<DWORD>(hr));
    return stream.str();
}

std::wstring TrimDeviceName(const std::wstring& name) {
    std::wstring result = name;
    const std::wstring suffix = L" (";
    const size_t suffixPosition = result.find(suffix);
    if (suffixPosition != std::wstring::npos && result.size() > 44) {
        result = result.substr(0, suffixPosition);
    }
    if (result.size() > 64) {
        result = result.substr(0, 61) + L"...";
    }
    return result;
}

float Lerp(float from, float to, float amount) {
    amount = Clamp(amount, 0.0f, 1.0f);
    return from + (to - from) * amount;
}

}
