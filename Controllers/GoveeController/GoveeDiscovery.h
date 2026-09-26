/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace GoveeDiscovery
{
inline std::string NormalizeMac(const std::string& value)
{
    std::string result;
    for(unsigned char c : value)
    {
        if(c == ':' || c == '-') continue;
        if(c >= 'a' && c <= 'f') c -= 'a' - 'A';
        if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return "";
        result.push_back(static_cast<char>(c));
    }
    return result.size() == 12 || result.size() == 16 ? result : "";
}

inline bool ValidIPv4(const std::string& value)
{
    int parts = 0, digits = 0, number = 0;
    for(char c : value + '.')
    {
        if(c == '.')
        {
            if(!digits || number > 255 || ++parts > 4) return false;
            digits = 0; number = 0;
        }
        else if(c >= '0' && c <= '9' && ++digits <= 3) number = number * 10 + c - '0';
        else return false;
    }
    return parts == 4;
}

// A supplied stable identity must match, even if another device reuses its old IP.
inline bool Matches(const nlohmann::json& data, const std::string& ip, const std::string& mac)
{
    if(!data.is_object() || !data.contains("ip") || !data["ip"].is_string() ||
       !ValidIPv4(data["ip"].get<std::string>()) || !data.contains("sku") || !data["sku"].is_string()) return false;
    if(!mac.empty())
        return data.contains("device") && data["device"].is_string() &&
               NormalizeMac(data["device"].get<std::string>()) == mac;
    return data["ip"] == ip;
}
}
