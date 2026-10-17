// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "RGBController.h"
#include <array>
#include <string>

namespace govee_configuration
{
// These are the four fields emitted by GoveeController::GetVersion(), not a
// general firmware-version wildcard. Unknown, repeated or empty fields fail.
inline bool ParseVersion(const std::string& text, std::array<std::string, 4>& fields)
{
    static const std::array<std::string, 4> labels = {{
        "BLE Hardware Version: ", "BLE Software Version: ",
        "WiFi Hardware Version: ", "WiFI Software Version: "
    }};
    if(text.empty() || text.size() > 256) return false;
    std::array<bool, 4> seen = {{false, false, false, false}};
    std::size_t start = 0;
    unsigned count = 0;
    while(start < text.size())
    {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? end : end - start);
        if(!line.empty() && line.back() == '\r') line.pop_back();
        unsigned field = 0;
        while(field < labels.size() && line.compare(0, labels[field].size(), labels[field]) != 0) ++field;
        if(field == labels.size() || seen[field]) return false;
        const std::string value = line.substr(labels[field].size());
        if(value.empty() || value.size() > 32) return false;
        unsigned dots = 0;
        bool digit = false;
        for(char ch : value)
        {
            if(ch >= '0' && ch <= '9') digit = true;
            else if(ch == '.' && digit) { ++dots; digit = false; }
            else return false;
        }
        if(!digit || dots != 2) return false;
        fields[field] = value;
        seen[field] = true;
        ++count;
        if(end == std::string::npos) break;
        start = end + 1;
    }
    return count == fields.size();
}

inline bool StableSerial(const std::string& serial)
{
    if(serial.size() != 12 && serial.size() != 16) return false;
    bool nonzero = false;
    for(char ch : serial)
    {
        if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))) return false;
        nonzero = nonzero || ch != '0';
    }
    return nonzero;
}

inline bool SoftwareUpdateCompatible(RGBController* saved, RGBController* detected)
{
    if(!saved || !detected || saved->GetVendor() != "Govee" || detected->GetVendor() != "Govee"
    || saved->GetDeviceType() != detected->GetDeviceType()
    || saved->GetName() != detected->GetName()
    || saved->GetDescription() != detected->GetDescription()
    || saved->GetSerial() != detected->GetSerial() || !StableSerial(saved->GetSerial())) return false;

    std::array<std::string, 4> old_version, new_version;
    if(!ParseVersion(saved->GetVersion(), old_version) || !ParseVersion(detected->GetVersion(), new_version)
    || old_version[0] != new_version[0] || old_version[2] != new_version[2]) return false;

    // Require a restorable topology BEFORE applying device-specific settings.
    // Segments, configured size and matrix mapping are the payload to restore,
    // so they intentionally need not equal the newly detected defaults.
    if(saved->GetZoneCount() == 0 || saved->GetZoneCount() != detected->GetZoneCount()) return false;
    for(unsigned z = 0; z < saved->GetZoneCount(); ++z)
    {
        const auto saved_flags = saved->GetZoneFlags(z);
        const auto detected_flags = detected->GetZoneFlags(z);
        const bool custom_name = (saved_flags & ZONE_FLAG_MANUALLY_CONFIGURED_NAME)
                              || (detected_flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_NAME);
        const bool custom_type = (saved_flags & ZONE_FLAG_MANUALLY_CONFIGURED_TYPE)
                              || (detected_flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_TYPE);
        if(saved->GetZoneLEDsMin(z) != detected->GetZoneLEDsMin(z)
        || saved->GetZoneLEDsMax(z) != detected->GetZoneLEDsMax(z)
        || (!custom_name && saved->GetZoneName(z) != detected->GetZoneName(z))
        || (!custom_type && saved->GetZoneType(z) != detected->GetZoneType(z))) return false;
    }
    return true;
}

inline bool VersionsMatch(RGBController* saved, RGBController* detected)
{
    return saved->GetVersion() == detected->GetVersion() || SoftwareUpdateCompatible(saved, detected);
}
}
