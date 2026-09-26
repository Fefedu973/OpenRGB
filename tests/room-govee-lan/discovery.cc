// SPDX-License-Identifier: GPL-2.0-or-later
#include "GoveeDiscovery.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace GoveeDiscovery;
    const std::string mac = "AABBCCDDEEFF0011";
    nlohmann::json data = {{"device", "aa:bb:cc:dd:ee:ff:00:11"}, {"ip", "192.0.2.80"}, {"sku", "H61E1"}};
    assert(Matches(data, "192.0.2.20", mac)); // DHCP changed
    assert(!Matches(data, "192.0.2.80", "1122334455667788")); // old IP reused
    assert(Matches(data, "192.0.2.80", "")); // existing IP-only settings
    assert(!Matches(data, "192.0.2.20", ""));
    data["ip"] = "999.1.2.3"; assert(!Matches(data, "999.1.2.3", mac));
    data["ip"] = 1; assert(!Matches(data, "", mac));
    assert(!Matches(nullptr, "", mac));
    assert(NormalizeMac("AA-BB-CC-DD-EE-FF") == "AABBCCDDEEFF");
    assert(NormalizeMac("AABBCCZDEEFF").empty());
    for(const char* ip : {"1.2.3", "1.2.3.4.5", "1..2.3", "-1.2.3.4", "host.example", "1.2.3.4:4003", "1.2.3.0000"}) assert(!ValidIPv4(ip));
    std::cout << "Govee stable identity / stale IP / malformed scan tests: PASS\n";
}
