/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native Windows GATT client. C++/WinRT requires Windows SDK 10.0.19041+.
 * No Python process, UDP bridge, LAN color command or automatic BLE scan.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include "GoveeBluetoothProtocol.h"
#ifndef GOVEE_BLE_CRYPTO_TEST
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include "GoveeBluetoothController_Windows.h"
#include "GoveeBluetoothSession.h"
#include "LogManager.h"
#endif
#include <algorithm>
#include <chrono>
#include <deque>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

namespace GoveeBluetooth
{
#ifndef GOVEE_BLE_CRYPTO_TEST
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
#endif

static void CheckCrypto(NTSTATUS result)
{
    if(result < 0) throw std::runtime_error("Govee BLE cryptographic operation failed");
}

static Packet Crypt(const Packet& packet, const Key& key, bool decrypt)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    Packet result{};
    std::vector<uint8_t> object;
    try
    {
        CheckCrypto(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0));
        CheckCrypto(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)), sizeof(BCRYPT_CHAIN_MODE_ECB), 0));
        DWORD length = 0, returned = 0;
        CheckCrypto(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &returned, 0));
        object.resize(length);
        CheckCrypto(BCryptGenerateSymmetricKey(algorithm, &handle, object.data(), length,
            const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0));
        NTSTATUS status = decrypt ?
            BCryptDecrypt(handle, const_cast<PUCHAR>(packet.data()), 16, nullptr, nullptr, 0, result.data(), 16, &returned, 0) :
            BCryptEncrypt(handle, const_cast<PUCHAR>(packet.data()), 16, nullptr, nullptr, 0, result.data(), 16, &returned, 0);
        CheckCrypto(status);
        if(returned != 16) throw std::runtime_error("Unexpected Govee BLE AES block length");
        BCryptDestroyKey(handle); handle = nullptr;
        SecureZeroMemory(object.data(), object.size());
        BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
    }
    catch(...)
    {
        if(handle) BCryptDestroyKey(handle);
        if(algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if(!object.empty()) SecureZeroMemory(object.data(), object.size());
        throw;
    }
    // The last four bytes are a fresh RC4 stream for every packet, not AES padding.
    std::array<uint8_t, 256> state{};
    for(unsigned int i = 0; i < state.size(); ++i) state[i] = static_cast<uint8_t>(i);
    unsigned int j = 0;
    for(unsigned int i = 0; i < state.size(); ++i)
    {
        j = (j + state[i] + key[i % key.size()]) & 255;
        std::swap(state[i], state[j]);
    }
    unsigned int i = 0; j = 0;
    for(unsigned int n = 16; n < 20; ++n)
    {
        i = (i + 1) & 255; j = (j + state[i]) & 255;
        std::swap(state[i], state[j]);
        result[n] = packet[n] ^ state[(state[i] + state[j]) & 255];
    }
    SecureZeroMemory(state.data(), state.size());
    return result;
}

#ifndef GOVEE_BLE_CRYPTO_TEST
struct Inbox
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Packet> packets;
};

class WindowsTransport : public Transport
{
public:
    WindowsTransport(const Configuration& configuration_, std::atomic<bool>& stop_) :
        configuration(configuration_), stop(stop_), inbox(std::make_shared<Inbox>()) {}
    ~WindowsTransport() override { Disconnect(); SecureZeroMemory(session_key.data(), session_key.size()); }

    bool Connected() const override
    {
        return authenticated && device && device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
    }

    void BeginRestore() { restoring = true; restore_deadline = Clock::now() + 5s; }

    void Connect() override
    {
        Disconnect();
        // A late callback from the old GATT connection may still run after its
        // event token is revoked. A fresh inbox isolates connection generations.
        inbox = std::make_shared<Inbox>();
        try
        {
            device = Wait(BluetoothLEDevice::FromBluetoothAddressAsync(configuration.address));
            if(!device) throw std::runtime_error("Configured Govee BLE device is unavailable");
            if(device.BluetoothAddress() != configuration.address)
                throw std::runtime_error("Govee BLE address does not match configuration");
            if(configuration.profile == Profile::H6159)
            {
                const std::string name = winrt::to_string(device.Name());
                if(name.find("H6159") == std::string::npos)
                    throw std::runtime_error("Configured BLE device name does not identify H6159");
            }
            const winrt::guid service_id(L"00010203-0405-0607-0809-0a0b0c0d1910");
            const winrt::guid notify_id(L"00010203-0405-0607-0809-0a0b0c0d2b10");
            const winrt::guid write_id(L"00010203-0405-0607-0809-0a0b0c0d2b11");
            auto services = Wait(device.GetGattServicesForUuidAsync(service_id, BluetoothCacheMode::Uncached));
            if(services.Status() != GattCommunicationStatus::Success || services.Services().Size() != 1)
                throw std::runtime_error("Govee BLE uncached service discovery failed");
            service = services.Services().GetAt(0);
            auto characteristics = Wait(service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached));
            if(characteristics.Status() != GattCommunicationStatus::Success)
                throw std::runtime_error("Govee BLE uncached characteristic discovery failed");
            for(const auto& characteristic : characteristics.Characteristics())
            {
                if(characteristic.Uuid() == notify_id) notify = characteristic;
                if(characteristic.Uuid() == write_id) write = characteristic;
            }
            if(!notify || !write) throw std::runtime_error("Govee BLE required GATT characteristics are missing");
            const auto np = notify.CharacteristicProperties();
            const auto wp = write.CharacteristicProperties();
            const auto required_write = configuration.profile == Profile::H6159 ?
                GattCharacteristicProperties::Write : GattCharacteristicProperties::WriteWithoutResponse;
            if((wp & required_write) == GattCharacteristicProperties::None)
                throw std::runtime_error("Govee BLE required GATT write property is missing");
            auto subscription = GattClientCharacteristicConfigurationDescriptorValue::None;
            if((np & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::None)
                subscription = GattClientCharacteristicConfigurationDescriptorValue::Notify;
            else if((np & GattCharacteristicProperties::Indicate) != GattCharacteristicProperties::None)
                subscription = GattClientCharacteristicConfigurationDescriptorValue::Indicate;
            else throw std::runtime_error("Govee BLE required GATT notification property is missing");
            // Capture only a shared inbox: late Windows callbacks cannot access a
            // destroyed controller/session during cancellation or reconnect.
            auto target = inbox;
            notification_token = notify.ValueChanged([target](const auto&, const GattValueChangedEventArgs& args)
            {
                try
                {
                    auto buffer = args.CharacteristicValue();
                    if(buffer.Length() != 20) return;
                    Packet packet{};
                    DataReader::FromBuffer(buffer).ReadBytes(winrt::array_view<uint8_t>(packet));
                    std::lock_guard<std::mutex> lock(target->mutex);
                    if(target->packets.size() == 64) target->packets.pop_front();
                    target->packets.push_back(packet);
                    target->changed.notify_one();
                }
                catch(...) { /* Invalid notification is ignored, never escapes into WinRT. */ }
            });
            registered = true;
            if(Wait(notify.WriteClientCharacteristicConfigurationDescriptorAsync(subscription)) != GattCommunicationStatus::Success)
                throw std::runtime_error("Govee BLE notification subscription failed");
            if(configuration.profile == Profile::H6008)
            {
                Packet response{};
                for(unsigned int attempt = 0; attempt < 2; ++attempt)
                {
                    ClearInbox();
                    WriteRaw(Crypt(Handshake(1), configuration.key, false));
                    try { response = Receive(0xE7, 1, &configuration.key); break; }
                    catch(const ReplyTimeout&) { if(attempt == 1) throw; }
                }
                Key candidate{};
                std::copy_n(response.begin() + 2, 16, candidate.begin());
                ClearInbox();
                WriteRaw(Crypt(Handshake(2), configuration.key, false));
                Receive(0xE7, 2, &configuration.key);
                session_key = candidate;
                authenticated = true;
                const Packet identity = Query(0x14);
                uint64_t reported = 0;
                for(unsigned int index = 2; index < 8; ++index) reported = (reported << 8) | identity[index];
                if(reported != configuration.wifi_mac)
                    throw std::runtime_error("Govee BLE authenticated AA14 identity mismatch");
            }
            else authenticated = true;
        }
        catch(...) { Disconnect(); throw; }
    }

    void Send(const Packet& packet) override
    {
        if(!Connected()) throw std::runtime_error("Govee BLE connection is unavailable");
        if(!ValidPacket(packet)) throw std::invalid_argument("Invalid Govee BLE packet checksum");
        WriteRaw(configuration.profile == Profile::H6008 ? Crypt(packet, session_key, false) : packet);
    }

    Packet Query(uint8_t command) override
    {
        const unsigned int attempts = configuration.profile == Profile::H6159 ? 2 : 1;
        for(unsigned int attempt = 0; attempt < attempts; ++attempt)
        {
            ClearInbox();
            Send(configuration.profile == Profile::H6008 && command == 5 ? MakePacket(0xAA, 5, {1}) : MakePacket(0xAA, command));
            try { return Receive(0xAA, command, configuration.profile == Profile::H6008 ? &session_key : nullptr); }
            catch(const ReplyTimeout&) { if(attempt + 1 == attempts) throw; }
        }
        throw std::runtime_error("Govee BLE query did not execute");
    }

    void Disconnect() noexcept override
    {
        authenticated = false;
        if(registered && notify)
        {
            try { notify.ValueChanged(notification_token); } catch(...) {}
        }
        registered = false;
        notify = nullptr; write = nullptr;
        if(service) { try { service.Close(); } catch(...) {} service = nullptr; }
        if(device) { try { device.Close(); } catch(...) {} device = nullptr; }
        SecureZeroMemory(session_key.data(), session_key.size());
        ClearInbox();
    }

private:
    struct ReplyTimeout : std::runtime_error { ReplyTimeout() : std::runtime_error("Govee BLE state/authentication reply timed out") {} };

    bool Interrupted() const { return restoring ? Clock::now() >= restore_deadline : stop.load(); }

    template<class T> auto Wait(const T& operation) -> decltype(operation.GetResults())
    {
        const auto deadline = Clock::now() + 8s;
        while(operation.Status() == AsyncStatus::Started)
        {
            if(Interrupted() || Clock::now() >= deadline)
            {
                operation.Cancel();
                throw std::runtime_error("Govee BLE operation cancelled or timed out");
            }
            std::this_thread::sleep_for(10ms);
        }
        return operation.GetResults();
    }

    Packet Handshake(uint8_t command)
    {
        Packet packet = MakePacket(0xE7, command);
        CheckCrypto(BCryptGenRandom(nullptr, packet.data() + 2, 17, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        packet[19] = 0;
        for(unsigned int i = 0; i < 19; ++i) packet[19] ^= packet[i];
        return packet;
    }

    void ClearInbox()
    {
        std::lock_guard<std::mutex> lock(inbox->mutex);
        inbox->packets.clear();
    }

    void WriteRaw(const Packet& packet)
    {
        if(Interrupted()) throw std::runtime_error("Govee BLE operation cancelled");
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(packet));
        auto option = configuration.profile == Profile::H6159 ? GattWriteOption::WriteWithResponse : GattWriteOption::WriteWithoutResponse;
        if(Wait(write.WriteValueAsync(writer.DetachBuffer(), option)) != GattCommunicationStatus::Success)
            throw std::runtime_error("Govee BLE GATT write failed");
    }

    Packet Receive(uint8_t prefix, uint8_t command, const Key* key)
    {
        const auto deadline = Clock::now() + 3s;
        while(Clock::now() < deadline)
        {
            if(Interrupted()) throw std::runtime_error("Govee BLE reply wait cancelled");
            Packet packet{};
            {
                std::unique_lock<std::mutex> lock(inbox->mutex);
                if(inbox->packets.empty()) inbox->changed.wait_for(lock, 25ms);
                if(inbox->packets.empty()) continue;
                packet = inbox->packets.front(); inbox->packets.pop_front();
            }
            if(key) packet = Crypt(packet, *key, true);
            if(ValidPacket(packet) && packet[0] == prefix && packet[1] == command) return packet;
        }
        throw ReplyTimeout();
    }

    const Configuration& configuration;
    std::atomic<bool>& stop;
    std::shared_ptr<Inbox> inbox;
    BluetoothLEDevice device{nullptr};
    GattDeviceService service{nullptr};
    GattCharacteristic notify{nullptr}, write{nullptr};
    winrt::event_token notification_token{};
    bool registered = false, authenticated = false, restoring = false;
    Clock::time_point restore_deadline{};
    Key session_key{};
};

Controller::Controller(Configuration configuration_) : configuration(std::move(configuration_))
{
    worker = std::thread(&Controller::Run, this);
}
Controller::~Controller()
{
    stopping = true;
    wake.notify_all();
    if(worker.joinable()) worker.join();
    SecureZeroMemory(configuration.key.data(), configuration.key.size());
}
void Controller::Submit(Frame frame)
{
    if(frame.brightness > 100) frame.brightness = 100;
    { std::lock_guard<std::mutex> lock(mutex); latest = frame; have_frame = true; }
    wake.notify_one();
}

void Controller::Run()
{
    // Keep all WinRT objects, GATT calls and packet queries in this MTA worker.
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    catch(...) { LOG_ERROR("[Govee BLE] Could not initialize Windows MTA"); return; }
    std::ostringstream lock_name;
    lock_name << "Local\\OpenRGB-Govee-BLE-" << std::hex << configuration.address;
    HANDLE ownership = CreateMutexA(nullptr, FALSE, lock_name.str().c_str());
    const DWORD acquired = ownership ? WaitForSingleObject(ownership, 0) : WAIT_FAILED;
    if(acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED)
    {
        LOG_ERROR("[Govee BLE] This configured device already has an OpenRGB BLE owner");
        if(ownership) CloseHandle(ownership);
        winrt::uninit_apartment();
        return;
    }
    {
        WindowsTransport transport(configuration, stopping);
        Session session(configuration.profile, transport, configuration.power_on_acquire);
        unsigned int failures = 0;
        auto next_step = Clock::now();
        std::string previous_state;
        while(!stopping)
        {
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if(!have_frame) wake.wait_for(lock, 100ms, [this] { return stopping || have_frame; });
                if(stopping) break;
                if(!have_frame) continue;
                if(Clock::now() < next_step)
                {
                    // New frame notifications do not bypass the hardware cadence.
                    wake.wait_until(lock, next_step, [this] { return stopping.load(); });
                    if(stopping) break;
                }
                frame = latest;
            }
            try
            {
                const auto now = Clock::now();
                session.Step(frame, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()));
                failures = 0;
                if(previous_state != session.State())
                {
                    previous_state = session.State();
                    LOG_INFO("[Govee BLE] %s: %s%s", configuration.name.c_str(), previous_state.c_str(),
                        session.RecoveredBaseline() ? " (new requested RGB recovery baseline)" : "");
                }
                next_step = Clock::now() + (configuration.profile == Profile::H6008 ? 50ms : 100ms);
            }
            catch(const std::exception& error)
            {
                if(!stopping) LOG_WARNING("[Govee BLE] %s: %s", configuration.name.c_str(), error.what());
                if(!stopping) transport.Disconnect();
                next_step = Clock::now() + (++failures >= 3 ? 30s : 1s);
            }
            catch(const winrt::hresult_error& error)
            {
                if(!stopping) LOG_WARNING("[Govee BLE] %s: Windows GATT error 0x%08X", configuration.name.c_str(), static_cast<unsigned int>(error.code().value));
                if(!stopping) transport.Disconnect();
                next_step = Clock::now() + (++failures >= 3 ? 30s : 1s);
            }
            catch(...)
            {
                LOG_ERROR("[Govee BLE] Unexpected worker failure; releasing configured device");
                break;
            }
        }
        transport.BeginRestore();
        try { session.Release(); }
        catch(...) { LOG_WARNING("[Govee BLE] Shutdown restoration could not be completed; no reconnect attempted"); }
        transport.Disconnect();
    }
    ReleaseMutex(ownership);
    CloseHandle(ownership);
    winrt::uninit_apartment();
}
#endif
}
