#pragma once

#include <string>
#include <windows.h>

namespace sx {

std::wstring HResultToString(HRESULT hr);
std::wstring LastErrorToString(DWORD error);
std::wstring TrimDeviceName(const std::wstring& name);

template <typename T>
T Clamp(T value, T minValue, T maxValue) {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

float Lerp(float from, float to, float amount);

}
