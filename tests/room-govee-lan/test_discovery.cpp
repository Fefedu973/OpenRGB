/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "GoveeController.h"
#include <nlohmann/json.hpp>
#include <future>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;
using namespace std::chrono_literals;
static unsigned checks = 0;
static void Check(bool value, const char* label)
{
    ++checks;
    if(!value) throw std::runtime_error(label);
}
struct Socket
{
    SOCKET value = INVALID_SOCKET;
    Socket(const char* address, unsigned short port)
    {
        value = socket(AF_INET, SOCK_DGRAM, 0);
        Check(value != INVALID_SOCKET, "create fixture socket");
        BOOL exclusive = TRUE;
        setsockopt(value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in bind_to{}; bind_to.sin_family = AF_INET; bind_to.sin_port = htons(port);
        inet_pton(AF_INET, address, &bind_to.sin_addr);
        if(bind(value, reinterpret_cast<const sockaddr*>(&bind_to), sizeof(bind_to)) != 0)
            throw std::runtime_error("fixture loopback port unavailable (no reuse or production process stopped)");
        DWORD timeout = 2000;
        setsockopt(value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    }
    ~Socket() { if(value != INVALID_SOCKET) closesocket(value); }
    json Receive(sockaddr_in& sender)
    {
        char bytes[2048]; int length = sizeof(sender);
        const int count = recvfrom(value, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&sender), &length);
        if(count <= 0) throw std::runtime_error("fixture receive timeout");
        const int payload = bytes[count-1] == '\0' ? count-1 : count;
        return json::parse(bytes, bytes+payload);
    }
    void Reply(const sockaddr_in& recipient, const json& message)
    {
        const auto bytes = message.dump();
        if(sendto(value, bytes.data(), int(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&recipient), sizeof(recipient)) != int(bytes.size()))
            throw std::runtime_error("fixture reply failed");
    }
};

int main()
{
    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2), &wsa);
    try
    {
        // Exercise the production bind-failure helper. It must fail before the
        // detector creates a receiver/controller or waits five seconds.
        {
            Socket occupied("0.0.0.0", 4002);
            const auto start = std::chrono::steady_clock::now();
            Check(!GoveeController::OpenDiscoverySocket(), "occupied discovery socket rejected");
            Check(GoveeController::broadcast_port.sock == INVALID_SOCKET, "failed bind handle closed");
            Check(std::chrono::steady_clock::now()-start < 500ms, "bind failure is immediate");
        }
        Socket configured("127.0.0.1",4001);
        Socket multicast_route("127.0.0.2",4001);
        Socket colors("127.0.0.3",4003);
        // Redirect only the multicast destination to a second loopback address.
        // The controller and net_port are the unmodified production classes.
        Check(GoveeController::broadcast_port.udp_client("127.0.0.2","4001","4002"), "bind discovery fixture");
        GoveeController::ReceiveThreadRun = true;
        std::thread receiver(&GoveeController::ReceiveBroadcastThreadFunction);
        auto creating = std::async(std::launch::async, [] { return new GoveeController("127.0.0.1","AA:BB:CC:DD:EE:01"); });
        sockaddr_in broadcast_sender{}, unicast_sender{};
        const json multicast = multicast_route.Receive(broadcast_sender);
        const json unicast = configured.Receive(unicast_sender);
        Check(multicast == unicast, "both scan payloads identical");
        Check(unicast["msg"]["cmd"] == "scan" && unicast["msg"]["data"]["account_topic"] == "reserve", "scan protocol");
        Check(ntohs(unicast_sender.sin_port) == 4002 && unicast_sender.sin_port == broadcast_sender.sin_port, "same discovery source socket 4002");
        json response = {{"msg",{{"cmd","scan"},{"data",{{"ip","127.0.0.1"},{"sku","H6008"},{"device","AA:BB:CC:DD:EE:99"}}}}}};
        configured.Reply(unicast_sender,response);
        Check(creating.wait_for(100ms) == std::future_status::timeout, "wrong MAC at configured IP rejected");
        response["msg"]["data"]["device"] = "AA:BB:CC:DD:EE:01";
        response["msg"]["data"]["sku"] = 6008;
        configured.Reply(unicast_sender,response);
        Check(creating.wait_for(100ms) == std::future_status::timeout, "malformed SKU rejected");
        response["msg"]["data"]["sku"] = "H6008";
        response["msg"]["data"]["ip"] = "127.0.0.3";
        response["msg"]["data"]["wifiVersionSoft"] = "test-firmware";
        configured.Reply(unicast_sender,response);
        Check(creating.wait_for(1000ms) == std::future_status::ready, "matching stable identity discovered");
        GoveeController* controller = creating.get();
        Check(controller->IsDiscovered() && controller->GetSku() == "H6008", "SKU accepted");
        Check(controller->GetSerial() == "AABBCCDDEE01" && controller->GetLocation() == "IP: 127.0.0.3", "MAC identity updates stale address");
        Check(controller->GetVersion().find("test-firmware") != std::string::npos, "metadata published");
        controller->SetColor(1,2,3); // Loopback only: prove sender uses discovered IP.
        sockaddr_in color_sender{};
        const json color = colors.Receive(color_sender);
        Check(color["msg"]["cmd"] == "colorwc" && color["msg"]["data"]["color"]["r"] == 1, "control uses discovered loopback address");
        delete controller;
        Check(GoveeController::callbacks.empty(), "destructor unregisters receiver");
        // Exclusive bind succeeds only after the controller's actual UDP socket
        // is closed. Before this fix net_port's destructor leaked that socket.
        Socket rebound("0.0.0.0",ntohs(color_sender.sin_port));
        Check(rebound.value != INVALID_SOCKET, "owned send socket released");
        GoveeController::ReceiveThreadRun = false;
        receiver.join();
        GoveeController::broadcast_port.tcp_close();
        GoveeController::broadcast_port.sock = INVALID_SOCKET;
        std::cout << "PASS " << checks << " assertions: production controller/net_port, loopback only\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        // End the isolated test process on failure; no hardware was contacted.
        std::exit(2);
    }
}
