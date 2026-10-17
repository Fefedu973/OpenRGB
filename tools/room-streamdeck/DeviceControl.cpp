// SPDX-License-Identifier: GPL-2.0-or-later
// One-shot localhost SDK control switch. Never writes colors or changes profiles.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../dependencies/json/nlohmann/json.hpp"
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"shell32.lib")

namespace {
constexpr uint32_t Magic=0x4247524f; // ORGB on the wire, little endian.
struct Header { uint32_t magic,device,packet,size; };
static_assert(sizeof(Header)==16,"SDK header");
struct Socket {
    SOCKET value=INVALID_SOCKET;
    ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
};
struct Winsock {
    WSADATA data{};
    Winsock(){if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock init failed");}
    ~Winsock(){WSACleanup();}
};
struct UniqueLock {
    HANDLE handle=nullptr;
    explicit UniqueLock(unsigned port){
        auto name=L"Local\\OpenRGBRoom.DeviceControl."+std::to_wstring(port);
        handle=CreateMutexW(nullptr,TRUE,name.c_str());
        if(!handle)throw std::runtime_error("Unable to serialize device control");
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(handle);handle=nullptr;throw std::runtime_error("A device control is already in progress");}
    }
    ~UniqueLock(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
};
std::string Utf8(const std::wstring& input){
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),int(input.size()),nullptr,0,nullptr,nullptr);
    if(bytes<=0||bytes>1024)throw std::runtime_error("Invalid profile name");
    std::string value(bytes,'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),int(input.size()),value.data(),bytes,nullptr,nullptr))throw std::runtime_error("Invalid Unicode profile name");
    return value;
}
uint32_t Number(const std::wstring& value){
    if(value.empty()||value.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Invalid numeric option");
    auto number=std::stoull(value);if(number>0xffffffffu)throw std::runtime_error("Numeric option out of range");return uint32_t(number);
}
class Client {
public:
    Client(unsigned port,unsigned timeout):deadline(GetTickCount64()+timeout){
        sock.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(sock.value==INVALID_SOCKET)throw std::runtime_error("Socket creation failed");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<u_short>(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        // Loopback only. Nonblocking connect respects the same total deadline.
        u_long nonblocking=1;ioctlsocket(sock.value,FIONBIO,&nonblocking);
        if(connect(sock.value,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR){
            if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("OpenRGB is not listening on localhost");
            fd_set write,errors;FD_ZERO(&write);FD_ZERO(&errors);FD_SET(sock.value,&write);FD_SET(sock.value,&errors);
            timeval wait{long(timeout/1000),long((timeout%1000)*1000)};
            if(select(0,nullptr,&write,&errors,&wait)<=0||FD_ISSET(sock.value,&errors))throw std::runtime_error("OpenRGB connection failed");
        }
        nonblocking=0;ioctlsocket(sock.value,FIONBIO,&nonblocking);
    }
    std::vector<uint8_t> Request(uint32_t packet,const std::vector<uint8_t>& payload,uint32_t reply=0xffffffffu,uint32_t device=0){
        Header header{Magic,device,packet,uint32_t(payload.size())};Send(&header,sizeof(header));if(!payload.empty())Send(payload.data(),payload.size());
        bool ack=false,answered=reply==0xffffffffu;std::vector<uint8_t> answer;
        while(!ack||!answered){
            Header incoming{};Read(&incoming,sizeof(incoming));
            if(incoming.magic!=Magic||incoming.size>1024*1024)throw std::runtime_error("Malformed SDK response");
            std::vector<uint8_t> body(incoming.size);if(!body.empty())Read(body.data(),body.size());
            if(incoming.device!=device)continue;
            if(incoming.packet==10){
                if(body.size()!=8)throw std::runtime_error("Malformed SDK acknowledgement");
                uint32_t id,status;std::memcpy(&id,body.data(),4);std::memcpy(&status,body.data()+4,4);
                if(id==packet){if(status)throw std::runtime_error("SDK rejected the device command");ack=true;}
            }
            if(incoming.packet==reply){
                answer=std::move(body);answered=true;
                if(packet==40){
                    uint32_t version=0;if(answer.size()!=4)throw std::runtime_error("Malformed SDK version");std::memcpy(&version,answer.data(),4);
                    if(version!=7)throw std::runtime_error("Device control requires Room SDK7");
                }
            }
        }
        return answer;
    }
private:
    Socket sock;ULONGLONG deadline;
    void Timeout(){
        auto now=GetTickCount64();if(now>=deadline)throw std::runtime_error("SDK device command timed out");
        DWORD remaining=DWORD(deadline-now);setsockopt(sock.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&remaining),sizeof(remaining));setsockopt(sock.value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&remaining),sizeof(remaining));
    }
    void Send(const void* pointer,size_t bytes){auto data=static_cast<const char*>(pointer);while(bytes){Timeout();int count=send(sock.value,data,int(bytes),0);if(count<=0)throw std::runtime_error("SDK send failed");data+=count;bytes-=count;}}
    void Read(void* pointer,size_t bytes){auto data=static_cast<char*>(pointer);while(bytes){Timeout();int count=recv(sock.value,data,int(bytes),0);if(count<=0)throw std::runtime_error("SDK response failed or timed out");data+=count;bytes-=count;}}
};
std::vector<uint8_t> U32(uint32_t number){std::vector<uint8_t> result(4);std::memcpy(result.data(),&number,4);return result;}

class Cursor {
public:
    const std::vector<uint8_t>& bytes;size_t offset=0;
    explicit Cursor(const std::vector<uint8_t>& value):bytes(value){}
    void Skip(size_t n){if(n>bytes.size()-offset)throw std::runtime_error("Truncated descriptor");offset+=n;}
    template<class T>T Take(){T value;size_t at=offset;Skip(sizeof(T));std::memcpy(&value,bytes.data()+at,sizeof(T));return value;}
    std::string String(size_t n){size_t at=offset;Skip(n);if(!n||bytes[offset-1]!=0)throw std::runtime_error("Invalid descriptor string");std::string s(bytes.begin()+at,bytes.begin()+offset-1);if(s.find('\0')!=std::string::npos)throw std::runtime_error("Embedded NUL");return s;}
    std::string String(){return String(Take<uint16_t>());}
    void Mode(){String();Skip(44);Skip(size_t(Take<uint16_t>())*4);}
};
struct Device {uint32_t id=0,flags=0;std::string serial,vendor,version,display; nlohmann::json configuration;};
Device Describe(uint32_t id,const std::vector<uint8_t>& body,const std::string& serial){
    Cursor c(body);if(c.Take<uint32_t>()!=body.size())throw std::runtime_error("Invalid descriptor length");
    c.Skip(4);c.String();Device d;d.id=id;d.vendor=c.String();c.String();d.version=c.String();d.serial=c.String();
    if(d.serial!=serial)return d; // No need to interpret another device's zones.
    if(d.vendor!="Govee" || (d.version!="h6008-realtime-v1"&&d.version!="h6159-classic-v1"))throw std::runtime_error("Unexpected controller identity");
    c.String();auto modes=c.Take<uint16_t>();c.Skip(4);while(modes--)c.Mode();
    auto zones=c.Take<uint16_t>();while(zones--){
        c.String();c.Skip(16);c.Skip(c.Take<uint16_t>());
        auto segments=c.Take<uint16_t>();while(segments--){c.String();c.Skip(12);c.Skip(c.Take<uint16_t>());c.Skip(4);}
        c.Skip(8);auto zone_modes=c.Take<uint16_t>();while(zone_modes--)c.Mode();c.String();
    }
    auto leds=c.Take<uint16_t>();while(leds--)c.String();
    c.Skip(size_t(c.Take<uint16_t>())*4);auto names=c.Take<uint16_t>();while(names--)c.String();
    d.flags=c.Take<uint32_t>();d.display=c.String();d.configuration=nlohmann::json::parse(c.String(c.Take<uint32_t>()));
    if(c.offset!=body.size()||!(d.flags&(1u<<17)) || !d.configuration.contains("schema") ||
       !d.configuration["schema"].contains("control_enabled") ||
       d.configuration["schema"]["control_enabled"].value("type","")!="bool" ||
       !d.configuration["configuration"]["control_enabled"].is_boolean())throw std::runtime_error("Control option unavailable");
    return d;
}
std::vector<uint8_t> Persist(const Device& d){
    const uint32_t size=uint32_t(11+d.display.size()), flags=d.flags|(1u<<25);const uint16_t length=uint16_t(d.display.size()+1);
    std::vector<uint8_t> data(size);std::memcpy(data.data(),&size,4);std::memcpy(data.data()+4,&flags,4);std::memcpy(data.data()+8,&length,2);std::memcpy(data.data()+10,d.display.c_str(),length);return data;
}
int Run(int argc,wchar_t** argv){
    std::wstring serial,action=L"toggle";unsigned port=6742,timeout=30000;
    for(int index=1;index<argc;++index){
        std::wstring argument=argv[index];if(index+1>=argc)throw std::runtime_error("Missing option value");
        if(argument==L"--serial")serial=argv[++index];
        else if(argument==L"--control")action=argv[++index];
        else if(argument==L"--port")port=Number(argv[++index]);
        else if(argument==L"--timeout-ms")timeout=Number(argv[++index]);
        else throw std::runtime_error("Unknown option");
    }
    if(serial.empty()||port==0||port>65535||timeout<100||timeout>30000 || (action!=L"toggle"&&action!=L"on"&&action!=L"off"))throw std::runtime_error("Invalid command options");
    const auto target=Utf8(serial);UniqueLock lock(port);Winsock winsock;Client client(port,timeout);
    client.Request(40,U32(7),40);
    client.Request(52,U32(1u<<16),53); // Local caller, no subscriptions that can stall effects.
    auto listing=client.Request(0,{},0);Cursor ids(listing);auto count=ids.Take<uint32_t>();
    if(count>4096||listing.size()!=4+size_t(count)*4)throw std::runtime_error("Invalid controller list");
    Device selected;unsigned matches=0;
    while(count--){auto id=ids.Take<uint32_t>();auto d=Describe(id,client.Request(1,U32(7),1,id),target);if(d.serial==target){selected=std::move(d);++matches;}}
    if(matches!=1)throw std::runtime_error("Expected one exact matching controller");
    const bool before=selected.configuration["configuration"]["control_enabled"].get<bool>();
    const bool enabled=action==L"toggle"?!before:action==L"on";
    auto config=selected.configuration["configuration"];config["control_enabled"]=enabled;
    const auto json=config.dump();std::vector<uint8_t> payload(json.begin(),json.end());payload.push_back(0);
    client.Request(1130,payload,0xffffffffu,selected.id);
    // Packet 1130 updates memory only. ConfigureDevice marks the option manual
    // and saves Configuration.json, preserving every other flag and the name.
    client.Request(1004,Persist(selected),0xffffffffu,selected.id);
    auto verify=Describe(selected.id,client.Request(1,U32(7),1,selected.id),target);
    if(verify.serial!=target || verify.configuration["configuration"]["control_enabled"].get<bool>()!=enabled || !(verify.flags&(1u<<25)))throw std::runtime_error("Control setting was not retained");
    return 0;
}

}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 2;
    int result=1;
    try{result=Run(argc,argv);}catch(const std::exception& error){OutputDebugStringA(error.what());}
    LocalFree(argv);return result;
}
