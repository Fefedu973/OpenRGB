// SPDX-License-Identifier: GPL-2.0-or-later
// One-shot localhost SDK profile selector. No detection, driver or daemon.
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
        auto name=L"Local\\OpenRGBRoom.ProfileSelect."+std::to_wstring(port);
        handle=CreateMutexW(nullptr,TRUE,name.c_str());
        if(!handle)throw std::runtime_error("Unable to serialize profile selection");
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(handle);handle=nullptr;throw std::runtime_error("A profile selection is already in progress");}
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
    std::vector<uint8_t> Request(uint32_t packet,const std::vector<uint8_t>& payload,uint32_t reply=0xffffffffu){
        Header header{Magic,0,packet,uint32_t(payload.size())};Send(&header,sizeof(header));if(!payload.empty())Send(payload.data(),payload.size());
        bool ack=false,answered=reply==0xffffffffu;std::vector<uint8_t> answer;
        while(!ack||!answered){
            Header incoming{};Read(&incoming,sizeof(incoming));
            if(incoming.magic!=Magic||incoming.size>1024*1024)throw std::runtime_error("Malformed SDK response");
            std::vector<uint8_t> body(incoming.size);if(!body.empty())Read(body.data(),body.size());
            if(incoming.device!=0)continue;
            if(incoming.packet==10){
                if(body.size()!=8)throw std::runtime_error("Malformed SDK acknowledgement");
                uint32_t id,status;std::memcpy(&id,body.data(),4);std::memcpy(&status,body.data()+4,4);
                if(id==packet){if(status)throw std::runtime_error("SDK rejected the profile command");ack=true;}
            }
            if(incoming.packet==reply){
                answer=std::move(body);answered=true;
                if(packet==40){
                    uint32_t version=0;if(answer.size()!=4)throw std::runtime_error("Malformed SDK version");std::memcpy(&version,answer.data(),4);
                    if(version<6)throw std::runtime_error("SDK profile API requires version 6 or newer");
                }
            }
        }
        return answer;
    }
private:
    Socket sock;ULONGLONG deadline;
    void Timeout(){
        auto now=GetTickCount64();if(now>=deadline)throw std::runtime_error("SDK profile command timed out");
        DWORD remaining=DWORD(deadline-now);setsockopt(sock.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&remaining),sizeof(remaining));setsockopt(sock.value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&remaining),sizeof(remaining));
    }
    void Send(const void* pointer,size_t bytes){auto data=static_cast<const char*>(pointer);while(bytes){Timeout();int count=send(sock.value,data,int(bytes),0);if(count<=0)throw std::runtime_error("SDK send failed");data+=count;bytes-=count;}}
    void Read(void* pointer,size_t bytes){auto data=static_cast<char*>(pointer);while(bytes){Timeout();int count=recv(sock.value,data,int(bytes),0);if(count<=0)throw std::runtime_error("SDK response failed or timed out");data+=count;bytes-=count;}}
};
std::vector<uint8_t> U32(uint32_t number){std::vector<uint8_t> result(4);std::memcpy(result.data(),&number,4);return result;}
int Run(int argc,wchar_t** argv){
    std::wstring name;unsigned port=6742,timeout=30000;
    for(int index=1;index<argc;++index){
        std::wstring argument=argv[index];if(index+1>=argc)throw std::runtime_error("Missing option value");
        if(argument==L"--profile")name=argv[++index];
        else if(argument==L"--port")port=Number(argv[++index]);
        else if(argument==L"--timeout-ms")timeout=Number(argv[++index]);
        else throw std::runtime_error("Unknown option");
    }
    if(name.empty()||port==0||port>65535||timeout<100||timeout>30000)throw std::runtime_error("Invalid command options");
    std::string profile=Utf8(name);UniqueLock lock(port);Winsock winsock;Client client(port,timeout);
    client.Request(40,U32(7),40);
    // No subscription flags: this short-lived caller has no UI/device mirror
    // to pause and must not make the server wait for profile-load callbacks.
    auto flags=client.Request(52,U32(0),53);uint32_t supported=0;
    if(flags.size()!=4)throw std::runtime_error("Malformed SDK feature flags");std::memcpy(&supported,flags.data(),4);
    if(!(supported&(1u<<2)))throw std::runtime_error("Server has no profile manager API");
    std::vector<uint8_t> payload(profile.begin(),profile.end());payload.push_back(0);
    client.Request(152,payload);
    auto active=client.Request(156,{},156);
    if(active.empty()||active.back()!=0||std::string(active.begin(),active.end()-1)!=profile)throw std::runtime_error("Active profile does not match the selected profile");
    return 0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 2;
    int result=1;
    try{result=Run(argc,argv);}catch(const std::exception& error){OutputDebugStringA(error.what());}
    LocalFree(argv);return result;
}
