/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "KBHEController.h"
#include <chrono>
#include <cstring>

using namespace KBHEProtocol;

KBHEController::KBHEController(hid_device* handle, const std::string& device_path,
                               const std::string& device_serial, bool is_legacy_75he)
    : dev(handle), path(device_path), serial(device_serial), legacy_75he(is_legacy_75he)
{
}

KBHEController::~KBHEController()
{
    RestoreHardware();
    hid_close(dev);
}

bool KBHEController::Exchange(const Report& report, Reply& reply)
{
    /* Bound stale traffic and every reply wait. Never inspect another HID interface. */
    unsigned char incoming[65]{};
    for(unsigned int i = 0; i < 16; ++i)
    {
        if(hid_read_timeout(dev, incoming, sizeof(incoming), 0) <= 0)
        {
            break;
        }
    }
    if(hid_write(dev, report.data(), report.size()) != static_cast<int>(report.size()))
    {
        last_error = "RAW HID write failed or was incomplete";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    for(unsigned int count = 0; count < 32 && std::chrono::steady_clock::now() < deadline; ++count)
    {
        const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        const int size = hid_read_timeout(dev, incoming, sizeof(incoming), std::max(1, left));
        if(size < 0)
        {
            last_error = "RAW HID read failed";
            return false;
        }
        if(size == 0)
        {
            break;
        }
        const unsigned char* payload = incoming;
        if(size == 65 && incoming[0] == 0)
        {
            payload++;
        }
        else if(size != 64)
        {
            continue;
        }
        if(payload[0] != report[1])
        {
            continue;
        }
        /* All five frame chunks share an opcode, so correlate the chunk index too. */
        if((report[1] == 0x6A || report[1] == 0x68) && payload[2] != report[3])
        {
            continue;
        }
        std::copy_n(payload, reply.size(), reply.begin());
        return true;
    }
    last_error = "Timed out waiting for a matching RAW HID reply";
    return false;
}

bool KBHEController::ExpectOK(const Report& report, Reply& reply)
{
    if(!Exchange(report, reply))
    {
        return false;
    }
    if(reply[1] != 0)
    {
        last_error = "Firmware rejected command " + std::to_string(report[1]) + " (status " + std::to_string(reply[1]) + ")";
        return false;
    }
    return true;
}

bool KBHEController::Probe()
{
    std::lock_guard<std::mutex> lock(io_mutex);
    Reply reply{};
    if(!ExpectOK(Command(0x00), reply) || reply[2] == 0)
    {
        return false;
    }
    version = std::to_string(reply[2]) + "." + std::to_string(reply[3]) + "." + std::to_string(reply[4]);
    if(!Exchange(Command(0x7F), reply))
    {
        return false;
    }
    if(reply[1] == 0)
    {
        compatible = CompatibleCapabilities(reply);
    }
    else
    {
        /* Released 75HE firmware predates the optional descriptor. Do not apply
         * this fallback to a receiver, gamepad, prototype, or unidentified PID. */
        compatible = reply[1] == 0x02 && legacy_75he;
    }
    if(!compatible)
    {
        last_error = "Unsupported KBHE RGB capabilities or unrecognized legacy personality";
    }
    return compatible;
}

bool KBHEController::EnterDirectMode()
{
    std::lock_guard<std::mutex> lock(io_mutex);
    if(!compatible)
    {
        return false;
    }
    if(owns_mode)
    {
        return true;
    }
    Reply reply{};
    if(!ExpectOK(Command(0x60), reply) || reply[2] > 1)
    {
        return false;
    }
    enabled_snapshot = reply[2];
    enabled_snapshot_valid = true;
    if(!ExpectOK(Command(0x6E), reply))
    {
        return false;
    }
    mode_snapshot = reply[2];
    mode_snapshot_valid = true;
    live_snapshot_valid = false;
    if(mode_snapshot == LIVE_MODE)
    {
        // GET_ALL returns pixels_runtime, including a previous owner's live
        // image. Rewriting mode 7 does not create the firmware restore token.
        if(!ReadFrame(live_snapshot) || !ExpectOK(Command(0x6E), reply))
        {
            return false;
        }
        if(reply[2] != LIVE_MODE)
        {
            last_error = "Hardware mode changed while reading the initial live image";
            return false;
        }
        live_snapshot_valid = true;
    }
    restore_pending_enabled = false;
    restoration_result = "pending";
    /* Mark possible ownership before the mutating write: a lost ACK does not
     * prove the command failed to reach the keyboard. Cleanup remains possible. */
    owns_mode = true;
    bool acquired = ExpectOK(Command(0x6F, {LIVE_MODE}), reply);
    if(acquired && reply[2] != LIVE_MODE)
    {
        acquired = false;
        last_error = "Firmware did not enter live RGB mode";
    }
    if(acquired)
    {
        acquired = ExpectOK(Command(0x61, {1}), reply);
        if(acquired && reply[2] != 1)
        {
            acquired = false;
            last_error = "Firmware did not enable RGB";
        }
    }
    if(!acquired)
    {
        const std::string failure = last_error;
        RestoreUnlocked();
        last_error = failure;
        return false;
    }
    last_error.clear();
    return true;
}

bool KBHEController::SendFrame(const Frame& frame)
{
    std::lock_guard<std::mutex> lock(io_mutex);
    if(!compatible || !owns_mode)
    {
        return false;
    }
    Reply reply{};
    /* Outside live mode 0x6A edits saved pixels. Stop instead of writing them if
     * another application or the user has changed the hardware effect. */
    if(!ExpectOK(Command(0x6E), reply))
    {
        return false;
    }
    if(reply[2] != LIVE_MODE)
    {
        ClearOwnership();
        restoration_result = "external_mode_preserved";
        last_error = "Hardware mode changed externally; select Direct again to reacquire";
        return false;
    }
    return WriteFrame(frame);
}

bool KBHEController::ReadFrame(Frame& frame)
{
    Reply reply{};
    for(std::size_t chunk = 0; chunk < CHUNK_COUNT; ++chunk)
    {
        const std::size_t offset = chunk * CHUNK_BYTES;
        const std::size_t size = std::min(CHUNK_BYTES, FRAME_BYTES - offset);
        if(!ExpectOK(Command(0x68, {static_cast<unsigned char>(chunk)}), reply))
        {
            return false;
        }
        if(reply[3] != size)
        {
            last_error = "Firmware returned an unexpected RGB snapshot chunk size";
            return false;
        }
        std::copy_n(reply.begin() + 4, size, frame.begin() + offset);
    }
    return true;
}

bool KBHEController::WriteFrame(const Frame& frame)
{
    Reply reply{};
    for(const Report& report : FrameReports(frame))
    {
        if(!ExpectOK(report, reply))
        {
            return false;
        }
        if(reply[3] != report[4])
        {
            last_error = "Firmware acknowledged an unexpected RGB chunk size";
            return false;
        }
    }
    last_error.clear();
    return true;
}

void KBHEController::ClearOwnership()
{
    owns_mode = false;
    enabled_snapshot_valid = false;
    mode_snapshot_valid = false;
    live_snapshot_valid = false;
    restore_pending_enabled = false;
}

bool KBHEController::RestoreUnlocked()
{
    if(!owns_mode)
    {
        return true;
    }
    Reply reply{};
    /* Restore only a live mode we still own; an externally selected hardware
     * effect takes precedence over our startup snapshot. */
    if(!ExpectOK(Command(0x6E), reply))
    {
        return false;
    }
    const unsigned char current_mode = reply[2];
    if(restore_pending_enabled && current_mode != restored_mode)
    {
        ClearOwnership();
        restoration_result = "external_mode_preserved";
        return true;
    }
    if(!restore_pending_enabled && current_mode != LIVE_MODE)
    {
        ClearOwnership();
        restoration_result = "external_mode_preserved";
        return true;
    }
    if(!restore_pending_enabled)
    {
        if(!Exchange(Command(0x76), reply))
        {
            return false;
        }
        if(reply[1] == 0 && reply[2] != LIVE_MODE)
        {
            restored_mode = reply[2];
            restoration_result = "firmware_previous_effect_restored";
        }
        else if(reply[1] == 1 && reply[2] == LIVE_MODE && mode_snapshot_valid)
        {
            // Status 1 means the firmware has no restore token. It happens
            // when mode 7 was already active before this controller acquired it.
            // Only return to a mode/image actually observed before our writes.
            if(mode_snapshot != LIVE_MODE)
            {
                if(!ExpectOK(Command(0x6F, {mode_snapshot}), reply) || reply[2] != mode_snapshot)
                {
                    if(last_error.empty()) last_error = "Firmware did not restore the observed hardware mode";
                    return false;
                }
                restored_mode = mode_snapshot;
                restoration_result = "observed_hardware_mode_restored";
            }
            else
            {
                if(!live_snapshot_valid || !ExpectOK(Command(0x6E), reply) || reply[2] != LIVE_MODE)
                {
                    if(last_error.empty()) last_error = "Cannot safely restore the initial live image";
                    return false;
                }
                Frame verification{};
                if(!WriteFrame(live_snapshot) || !ReadFrame(verification)) return false;
                if(verification != live_snapshot)
                {
                    last_error = "Initial live image restoration readback differs";
                    return false;
                }
                restored_mode = LIVE_MODE;
                restoration_result = "initial_live_image_restored_prior_effect_unknown";
            }
        }
        else
        {
            last_error = "Firmware rejected restore command 118 (status " + std::to_string(reply[1]) + ")";
            return false;
        }
        if(!ExpectOK(Command(0x6E), reply) || reply[2] != restored_mode)
        {
            if(last_error.empty()) last_error = "Restored hardware mode readback differs";
            return false;
        }
        // If only restoring the enable bit fails, retry that operation without
        // mistaking our just-restored non-live mode for an external takeover.
        restore_pending_enabled = true;
    }
    if(enabled_snapshot_valid)
    {
        if(!ExpectOK(Command(0x61, {enabled_snapshot}), reply) || reply[2] != enabled_snapshot ||
           !ExpectOK(Command(0x60), reply) || reply[2] != enabled_snapshot)
        {
            if(last_error.empty()) last_error = "Restored RGB enable state readback differs";
            return false;
        }
    }
    ClearOwnership();
    last_error.clear();
    return true;
}

bool KBHEController::RestoreHardware()
{
    std::lock_guard<std::mutex> lock(io_mutex);
    return RestoreUnlocked();
}
