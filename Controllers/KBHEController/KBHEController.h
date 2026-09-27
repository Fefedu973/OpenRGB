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
    bool KeepBlackOnExit();        // Existing live ownership only; no EEPROM writes.
    const std::string& GetVersion() const { return version; }
    const std::string& GetSerial() const { return serial; }
    std::string GetLocation() const { return "HID: " + path; }
    const std::string& GetLastError() const { return last_error; }
    // Distinguishes restoring a saved hardware effect from adopting/restoring
    // an already-live image whose earlier hardware effect is unknowable.
    const std::string& GetRestorationResult() const { return restoration_result; }

private:
    bool Exchange(const KBHEProtocol::Report& report, KBHEProtocol::Reply& reply);
    bool ExpectOK(const KBHEProtocol::Report& report, KBHEProtocol::Reply& reply);
    bool RestoreUnlocked();
    bool ReadFrame(KBHEProtocol::Frame& frame);
    bool WriteFrame(const KBHEProtocol::Frame& frame);
    void ClearOwnership();

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
    unsigned char mode_snapshot = 0;
    bool mode_snapshot_valid = false;
    KBHEProtocol::Frame live_snapshot{};
    bool live_snapshot_valid = false;
    bool restore_pending_enabled = false;
    unsigned char restored_mode = 0;
    std::string restoration_result = "not_acquired";
};
