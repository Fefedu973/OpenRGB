/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <hidapi.h>
#include <mutex>
#include <string>
#include "KBHEProtocol.h"

class KBHEController
{
public:
    KBHEController(hid_device* handle, const std::string& path, const std::string& serial, bool legacy_75he);
    ~KBHEController();

    bool Probe();                  // GET commands only; no lighting takeover.
    bool EnterDirectMode();
    bool SendFrame(const KBHEProtocol::Frame& frame);
    bool RestoreHardware();
    const std::string& GetVersion() const { return version; }
    const std::string& GetSerial() const { return serial; }
    std::string GetLocation() const { return "HID: " + path; }
    const std::string& GetLastError() const { return last_error; }

private:
    bool Exchange(const KBHEProtocol::Report& report, KBHEProtocol::Reply& reply);
    bool ExpectOK(const KBHEProtocol::Report& report, KBHEProtocol::Reply& reply);
    bool RestoreUnlocked();

    hid_device* dev;
    std::string path;
    std::string serial;
    std::string version;
    std::string last_error;
    std::mutex io_mutex;
    bool legacy_75he;
    bool compatible = false;
    bool owns_mode = false;
    bool enabled_snapshot_valid = false;
    unsigned char enabled_snapshot = 0;
};
