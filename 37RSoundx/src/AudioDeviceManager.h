#pragma once

#include <mmdeviceapi.h>
#include <string>
#include <vector>

namespace sx {

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    EDataFlow flow = eRender;
    bool isDefault = false;
};

class AudioDeviceManager {
public:
    std::vector<AudioDeviceInfo> Enumerate(EDataFlow flow) const;
    std::wstring GetDefaultDeviceId(EDataFlow flow) const;
    bool DeviceExists(const std::wstring& id) const;
};

}
