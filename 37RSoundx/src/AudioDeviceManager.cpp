#include "AudioDeviceManager.h"

#include "Utils.h"

#include <algorithm>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

namespace sx {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring DeviceIdFromDevice(IMMDevice* device) {
    LPWSTR rawId = nullptr;
    if (!device || FAILED(device->GetId(&rawId)) || !rawId) {
        return {};
    }

    std::wstring id(rawId);
    CoTaskMemFree(rawId);
    return id;
}

std::wstring DeviceNameFromDevice(IMMDevice* device) {
    if (!device) {
        return L"Unknown device";
    }

    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
        return L"Unknown device";
    }

    PROPVARIANT friendlyName;
    PropVariantInit(&friendlyName);
    std::wstring result = L"Unknown device";
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &friendlyName)) && friendlyName.vt == VT_LPWSTR && friendlyName.pwszVal) {
        result = friendlyName.pwszVal;
    }
    PropVariantClear(&friendlyName);
    return result;
}

}

std::vector<AudioDeviceInfo> AudioDeviceManager::Enumerate(EDataFlow flow) const {
    std::vector<AudioDeviceInfo> devices;

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        return devices;
    }

    const std::wstring defaultId = GetDefaultDeviceId(flow);

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || !collection) {
        return devices;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return devices;
    }

    devices.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device)) || !device) {
            continue;
        }

        AudioDeviceInfo info;
        info.id = DeviceIdFromDevice(device.Get());
        info.name = DeviceNameFromDevice(device.Get());
        info.flow = flow;
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        if (!info.id.empty()) {
            devices.push_back(std::move(info));
        }
    }

    std::stable_sort(devices.begin(), devices.end(), [](const AudioDeviceInfo& a, const AudioDeviceInfo& b) {
        if (a.isDefault != b.isDefault) {
            return a.isDefault;
        }
        return a.name < b.name;
    });

    return devices;
}

std::wstring AudioDeviceManager::GetDefaultDeviceId(EDataFlow flow) const {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        return {};
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    if (FAILED(hr) || !device) {
        return {};
    }

    return DeviceIdFromDevice(device.Get());
}

bool AudioDeviceManager::DeviceExists(const std::wstring& id) const {
    if (id.empty()) {
        return false;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr) || !device) {
        return false;
    }

    DWORD state = 0;
    return SUCCEEDED(device->GetState(&state)) && (state & DEVICE_STATE_ACTIVE);
}

}
