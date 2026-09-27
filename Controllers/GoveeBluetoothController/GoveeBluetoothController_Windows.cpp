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
#include "GoveeBluetoothAuthentication.h"
#include "GoveeBluetoothCredentialCache.h"
#include "GoveeBluetoothQuery.h"
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
    unsigned int events = 0, wrong_length = 0, callback_errors = 0;
};

// Diagnostic metadata only. No packet bytes, keys, identities or addresses.
// Read from the owning MTA thread; callbacks touch only the locked Inbox.
struct TransportDiagnostics
{
    int connection_status = -1, session_status = -1;
    int subscribe_status = -1, unsubscribe_status = -1;
    int disconnect_connection_status = -1, disconnect_session_status = -1;
    int64_t subscribe_ms = -1, unsubscribe_ms = -1;
    unsigned int notification_events = 0, wrong_length = 0, callback_errors = 0;
    unsigned int query_timeouts = 0, query_retries = 0, query_recoveries = 0;
    bool authenticated = false;
    bool retained_session_verified = false;
};

// Only the standalone read-only probe supplies these comparison options.
// Production Controller construction keeps both false; no settings enable them.
struct TransportProbeOptions
{
    bool full_services = false;
    bool auth_write_response = false;
};

static std::shared_ptr<CredentialCache> ProcessCredentials()
{
    // Each transport keeps shared ownership. Static teardown cannot destroy the
    // cache while a controller's worker is still finishing its own cleanup.
    static const auto cache = std::make_shared<CredentialCache>();
    return cache;
}
static uint64_t CredentialTime()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}
static CredentialScope ConfigurationScope(const Configuration& configuration)
{
    CredentialScope scope;
    scope.address = configuration.address;
    scope.wifi_identity = configuration.wifi_mac;
    scope.profile = configuration.profile;
    scope.root = configuration.key;
    return scope;
}

class WindowsTransport : public Transport
{
public:
    WindowsTransport(const Configuration& configuration_, std::atomic<bool>& stop_, TransportProbeOptions probe_options_ = {}) :
        configuration(configuration_), stop(stop_), inbox(std::make_shared<Inbox>()), probe_options(probe_options_),
        authentication(configuration_.wifi_mac),
        credential_cache(configuration_.profile == Profile::H6008 ? ProcessCredentials() : nullptr),
        credential_scope(ConfigurationScope(configuration_))
    {
        if(credential_cache)
            if(auto candidates = credential_cache->Take(credential_scope, CredentialTime()))
                authentication.ImportCandidates(*candidates);
    }
    ~WindowsTransport() override
    {
        Disconnect();
        SecureZeroMemory(session_key.data(), session_key.size());
    }

    bool Connected() const override
    {
        return authenticated && device && session && session.SessionStatus() == GattSessionStatus::Active;
    }

    void BeginRestore() { restoring = true; restore_deadline = Clock::now() + 5s; }

    TransportDiagnostics Diagnostics() const
    {
        auto result = diagnostics;
        try { result.connection_status = device ? static_cast<int>(device.ConnectionStatus()) : -1; }
        catch(...) { result.connection_status = -2; }
        try { result.session_status = session ? static_cast<int>(session.SessionStatus()) : -1; }
        catch(...) { result.session_status = -2; }
        result.authenticated = authenticated;
        std::lock_guard<std::mutex> lock(inbox->mutex);
        result.notification_events = inbox->events;
        result.wrong_length = inbox->wrong_length;
        result.callback_errors = inbox->callback_errors;
        return result;
    }

    std::pair<int, int> ReadNotificationConfiguration()
    {
        if(!notify) throw std::runtime_error("Govee BLE notification characteristic unavailable");
        const auto result = Wait(notify.ReadClientCharacteristicConfigurationDescriptorAsync());
        return {static_cast<int>(result.Status()),
            result.Status() == GattCommunicationStatus::Success ?
            static_cast<int>(result.ClientCharacteristicConfigurationDescriptor()) : -1};
    }

    void Connect() override
    {
        Disconnect();
        // A failed AA14/fresh handshake must not leave an unchecked entry in
        // the process handoff cache. This transport may retain its own bounded
        // candidates to distinguish a lost response from an expired session.
        if(credential_cache) credential_cache->Erase(credential_scope);
        diagnostics = TransportDiagnostics{};
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
            // Match the explicit client-owned lifetime used by Bleak/WinRT.
            // Uncached discovery also initiates a connection; maintaining this
            // session additionally keeps ownership between notifications/writes.
            session = Wait(GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()));
            if(!session || !session.CanMaintainConnection())
                throw std::runtime_error("Govee BLE GATT session cannot maintain a connection");
            session.MaintainConnection(true);
            const winrt::guid service_id(L"00010203-0405-0607-0809-0a0b0c0d1910");
            const winrt::guid notify_id(L"00010203-0405-0607-0809-0a0b0c0d2b10");
            const winrt::guid write_id(L"00010203-0405-0607-0809-0a0b0c0d2b11");
            // The tested H6159 returned Success with zero entries for a
            // UUID-filtered query. Bleak enumerates the allowlisted device's
            // complete service table and selects the Govee UUID afterwards.
            auto services = (configuration.profile == Profile::H6159 || probe_options.full_services) ?
                Wait(device.GetGattServicesAsync(BluetoothCacheMode::Uncached)) :
                Wait(device.GetGattServicesForUuidAsync(service_id, BluetoothCacheMode::Uncached));
            const unsigned int service_count = services.Status() == GattCommunicationStatus::Success ? services.Services().Size() : 0;
            if(services.Status() != GattCommunicationStatus::Success)
                throw std::runtime_error("Govee BLE uncached service discovery failed: status=" +
                    std::to_string(static_cast<int>(services.Status())) + " count=" + std::to_string(service_count));
            unsigned int matched_services = 0;
            for(const auto& candidate : services.Services())
            {
                discovered_services.push_back(candidate);
                if(candidate.Uuid() == service_id)
                {
                    service = candidate;
                    ++matched_services;
                }
            }
            if(matched_services != 1)
                throw std::runtime_error("Govee BLE required service discovery failed: status=0 count=" +
                    std::to_string(service_count) + " matches=" + std::to_string(matched_services));
            auto characteristics = Wait(service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached));
            if(characteristics.Status() != GattCommunicationStatus::Success)
                throw std::runtime_error("Govee BLE uncached characteristic discovery failed: status=" +
                    std::to_string(static_cast<int>(characteristics.Status())));
            for(const auto& characteristic : characteristics.Characteristics())
            {
                if(characteristic.Uuid() == notify_id) notify = characteristic;
                if(characteristic.Uuid() == write_id) write = characteristic;
            }
            if(!notify || !write) throw std::runtime_error("Govee BLE required GATT characteristics are missing");
            const auto np = notify.CharacteristicProperties();
            const auto wp = write.CharacteristicProperties();
            if(!HasRequiredWriteProperty(configuration.profile,
                (wp & GattCharacteristicProperties::Write) != GattCharacteristicProperties::None,
                (wp & GattCharacteristicProperties::WriteWithoutResponse) != GattCharacteristicProperties::None))
                throw std::runtime_error("Govee BLE required GATT write property is missing");
            const auto session_deadline = Clock::now() + 8s;
            while(session.SessionStatus() != GattSessionStatus::Active)
            {
                if(Interrupted() || Clock::now() >= session_deadline)
                    throw std::runtime_error("Govee BLE GATT session did not become active");
                std::this_thread::sleep_for(10ms);
            }
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
                    std::lock_guard<std::mutex> lock(target->mutex);
                    ++target->events;
                    auto buffer = args.CharacteristicValue();
                    if(buffer.Length() != 20) { ++target->wrong_length; return; }
                    Packet packet{};
                    DataReader::FromBuffer(buffer).ReadBytes(winrt::array_view<uint8_t>(packet));
                    if(target->packets.size() == 64) target->packets.pop_front();
                    target->packets.push_back(packet);
                    target->changed.notify_one();
                }
                catch(...)
                {
                    std::lock_guard<std::mutex> lock(target->mutex);
                    ++target->callback_errors; // Never log the rejected notification.
                }
            });
            registered = true;
            const auto subscription_start = Clock::now();
            diagnostics.subscribe_status = -2;
            const auto subscription_status = Wait(notify.WriteClientCharacteristicConfigurationDescriptorAsync(subscription));
            diagnostics.subscribe_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - subscription_start).count();
            diagnostics.subscribe_status = static_cast<int>(subscription_status);
            if(subscription_status != GattCommunicationStatus::Success)
                throw std::runtime_error("Govee BLE notification subscription failed: status=" +
                    std::to_string(static_cast<int>(subscription_status)));
            subscribed = true;
            if(configuration.profile == Profile::H6008)
            {
                const auto result = authentication.Connect(configuration.key,
                    [this](uint8_t prefix, uint8_t command, const Key& key)
                    {
                        ClearInbox();
                        const Packet request = prefix == 0xE7 ? Handshake(command) : MakePacket(prefix, command);
                        WriteRaw(Crypt(request, key, false), prefix == 0xE7);
                        return Receive(prefix, command, &key);
                    });
                session_key = authentication.VerifiedKey();
                authenticated = true;
                diagnostics.retained_session_verified = result == H6008Authentication::Result::Resumed;
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
        unsigned int attempts = 0;
        try
        {
            const auto result = QueryWithRetry<ReplyTimeout>(configuration.profile, command,
                [this, command, &attempts](unsigned int attempt)
                {
                    attempts = attempt;
                    if(attempt > 1) ++diagnostics.query_retries;
                    ClearInbox();
                    Send(configuration.profile == Profile::H6008 && command == 5 ? MakePacket(0xAA, 5, {1}) : MakePacket(0xAA, command));
                    try { return Receive(0xAA, command, configuration.profile == Profile::H6008 ? &session_key : nullptr); }
                    catch(const ReplyTimeout&) { ++diagnostics.query_timeouts; throw; }
                });
            if(result.attempts > 1)
            {
                ++diagnostics.query_recoveries;
                LOG_WARNING("[Govee BLE] %s: AA%02X reply recovered on the same connection after one retry (timeouts=%u retries=%u recoveries=%u)",
                    configuration.name.c_str(), static_cast<unsigned int>(command), diagnostics.query_timeouts,
                    diagnostics.query_retries, diagnostics.query_recoveries);
            }
            return result.reply;
        }
        catch(const ReplyTimeout& error)
        {
            const auto status = Diagnostics();
            throw std::runtime_error(std::string(error.what()) + " attempts=" + std::to_string(attempts) +
                " connection_status=" + std::to_string(status.connection_status) +
                " session_status=" + std::to_string(status.session_status) +
                " notification_events=" + std::to_string(status.notification_events) +
                " query_timeouts=" + std::to_string(status.query_timeouts));
        }
    }

    void Disconnect() noexcept override
    {
        const auto before = Diagnostics();
        if(device || session)
        {
            diagnostics.disconnect_connection_status = before.connection_status;
            diagnostics.disconnect_session_status = before.session_status;
        }
        if(authenticated && credential_cache)
        {
            try { credential_cache->Store(credential_scope, authentication.ExportCandidates(), CredentialTime()); }
            catch(...) { /* Cache allocation failure must not prevent GATT cleanup. */ }
        }
        authenticated = false;
        // Match stop_notify before revoking the callback. This is best effort:
        // an unavailable radio must not add the normal eight-second GATT wait
        // to destruction or exceed the shared restoration deadline.
        if(subscribed && notify && !Interrupted())
        {
            const auto unsubscribe_start = Clock::now();
            diagnostics.unsubscribe_status = -2;
            try
            {
                auto operation = notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::None);
                const auto deadline = Clock::now() + 250ms;
                while(operation.Status() == AsyncStatus::Started && !Interrupted() && Clock::now() < deadline)
                    std::this_thread::sleep_for(5ms);
                if(operation.Status() == AsyncStatus::Started)
                {
                    operation.Cancel();
                    diagnostics.unsubscribe_status = -3;
                }
                else if(operation.Status() == AsyncStatus::Completed)
                    diagnostics.unsubscribe_status = static_cast<int>(operation.GetResults());
            }
            catch(...) { /* A failed unsubscribe must not prevent object cleanup. */ }
            diagnostics.unsubscribe_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - unsubscribe_start).count();
        }
        subscribed = false;
        if(registered && notify)
        {
            try { notify.ValueChanged(notification_token); } catch(...) {}
        }
        registered = false;
        notify = nullptr; write = nullptr;
        // Bleak's WinRT disconnect allows 100 ms for notifications/operations
        // to settle before closing services (Windows Close can otherwise hang).
        // Keep this interruptible; synchronous WinRT Close itself is not an
        // operation whose cancellation or maximum duration we can guarantee.
        if(!discovered_services.empty())
        {
            const auto settle_deadline = Clock::now() + 100ms;
            while(!Interrupted() && Clock::now() < settle_deadline)
                std::this_thread::sleep_for(5ms);
        }
        service = nullptr;
        for(auto& discovered : discovered_services) { try { discovered.Close(); } catch(...) {} }
        discovered_services.clear();
        if(session) { try { session.MaintainConnection(false); } catch(...) {} }
        if(session) { try { session.Close(); } catch(...) {} session = nullptr; }
        if(device) { try { device.Close(); } catch(...) {} device = nullptr; }
        SecureZeroMemory(session_key.data(), session_key.size());
        ClearInbox();
    }

private:
    struct ReplyTimeout : AuthenticationReplyTimeout
    {
        ReplyTimeout(uint8_t prefix, uint8_t command, unsigned int received, unsigned int invalid, unsigned int unrelated) :
            AuthenticationReplyTimeout(Message(prefix, command, received, invalid, unrelated)) {}
        static std::string Message(uint8_t prefix, uint8_t command, unsigned int received, unsigned int invalid, unsigned int unrelated)
        {
            std::ostringstream text;
            text << "Govee BLE reply timeout prefix=0x" << std::hex << std::uppercase << static_cast<unsigned int>(prefix)
                 << " command=0x" << static_cast<unsigned int>(command) << std::dec
                 << " received=" << received << " invalid_checksum=" << invalid << " unrelated=" << unrelated;
            return text.str(); // No payload, address, key, identity or nonce is logged.
        }
    };

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

    void WriteRaw(const Packet& packet, bool authentication = false)
    {
        if(Interrupted()) throw std::runtime_error("Govee BLE operation cancelled");
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(packet));
        auto option = configuration.profile == Profile::H6159 || (authentication && probe_options.auth_write_response) ?
            GattWriteOption::WriteWithResponse : GattWriteOption::WriteWithoutResponse;
        const auto status = Wait(write.WriteValueAsync(writer.DetachBuffer(), option));
        if(status != GattCommunicationStatus::Success)
            throw std::runtime_error("Govee BLE GATT write failed: status=" + std::to_string(static_cast<int>(status)));
    }

    Packet Receive(uint8_t prefix, uint8_t command, const Key* key)
    {
        const auto deadline = Clock::now() + 3s;
        unsigned int received = 0, invalid = 0, unrelated = 0;
        while(Clock::now() < deadline)
        {
            if(Interrupted()) throw std::runtime_error("Govee BLE reply wait cancelled");
            // Session closure is a connection failure, not a missing reply.
            // Abort before another 25ms wait so Query cannot retry a dead link.
            // Initial E7/AA14 authentication runs with authenticated=false.
            if(authenticated && (!session || session.SessionStatus() == GattSessionStatus::Closed))
                throw std::runtime_error("Govee BLE GATT session closed while waiting for a reply");
            Packet packet{};
            {
                std::unique_lock<std::mutex> lock(inbox->mutex);
                if(inbox->packets.empty()) inbox->changed.wait_for(lock, 25ms);
                if(inbox->packets.empty()) continue;
                packet = inbox->packets.front(); inbox->packets.pop_front();
            }
            ++received;
            if(key) packet = Crypt(packet, *key, true);
            if(ValidPacket(packet) && packet[0] == prefix && packet[1] == command) return packet;
            if(!ValidPacket(packet)) ++invalid;
            else ++unrelated;
        }
        throw ReplyTimeout(prefix, command, received, invalid, unrelated);
    }

    const Configuration& configuration;
    std::atomic<bool>& stop;
    std::shared_ptr<Inbox> inbox;
    BluetoothLEDevice device{nullptr};
    GattSession session{nullptr};
    GattDeviceService service{nullptr};
    std::vector<GattDeviceService> discovered_services;
    GattCharacteristic notify{nullptr}, write{nullptr};
    winrt::event_token notification_token{};
    bool registered = false, subscribed = false, authenticated = false, restoring = false;
    Clock::time_point restore_deadline{};
    Key session_key{};
    TransportDiagnostics diagnostics;
    const TransportProbeOptions probe_options;
    H6008Authentication authentication;
    std::shared_ptr<CredentialCache> credential_cache;
    CredentialScope credential_scope;
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
        Session session(configuration.profile, transport, configuration.PowerOnAcquire());
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
