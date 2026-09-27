/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib,"bcrypt.lib")

namespace room_sd {
inline const std::filesystem::path& InstallDirectory()
{ static const std::filesystem::path value=L"C:\\Program Files\\Elgato\\StreamDeck";return value; }
inline std::wstring ProcessPath(DWORD pid)
{
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return {};
    std::wstring result(32768,L'\0');DWORD size=static_cast<DWORD>(result.size());
    const bool ok=QueryFullProcessImageNameW(process,0,result.data(),&size)!=FALSE;
    CloseHandle(process);if(!ok)return {};result.resize(size);return result;
}
inline bool ExpectedProcess(DWORD pid)
{ return _wcsicmp(ProcessPath(pid).c_str(),(InstallDirectory()/L"StreamDeck.exe").c_str())==0; }
inline DWORD FindProcess()
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    DWORD found=0;PROCESSENTRY32W item{};item.dwSize=sizeof(item);
    if(Process32FirstW(snapshot,&item))do {
        if(_wcsicmp(item.szExeFile,L"StreamDeck.exe")==0 && ExpectedProcess(item.th32ProcessID))
        { if(found) { found=0;break; }found=item.th32ProcessID; }
    } while(Process32NextW(snapshot,&item));
    CloseHandle(snapshot);return found;
}
inline std::string Sha256(const std::filesystem::path& path)
{
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Guard file unavailable");
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("SHA256 provider unavailable");
    std::array<unsigned char,32> digest{};std::array<char,65536> block{};
    bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    while(valid && stream) {
        stream.read(block.data(),block.size());const auto n=stream.gcount();
        if(n>0)valid=BCryptHashData(hash,reinterpret_cast<PUCHAR>(block.data()),static_cast<ULONG>(n),0)>=0;
    }
    valid=valid && !stream.bad() && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(!valid)throw std::runtime_error("SHA256 failed");
    static const char hex[]="0123456789ABCDEF";std::string result;result.reserve(64);
    for(const auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
inline void CheckBuild(DWORD pid)
{
    if(!pid || !ExpectedProcess(pid))throw std::runtime_error("Expected Elgato process not found");
    const std::pair<const wchar_t*,const char*> files[]={
        {L"StreamDeck.exe","9B2E3D0069052F18F372B9C9AEA46E925CB463ACBA443A1075E68EC5648F769D"},
        {L"Qt6Gui.dll","8FCEEE959A670372AAA5763287C2EF7924CD9ECDBE2C29CF4B6C12A63079C503"},
        {L"Qt6Core.dll","FAE4778A42E93ADC82B831C879C886A05147E9CC26760808D21116BE5547259B"}};
    for(const auto& entry:files)if(Sha256(InstallDirectory()/entry.first)!=entry.second)
        throw std::runtime_error("Unsupported Elgato build: a pinned SHA256 differs; attachment refused");
    if(!ExpectedProcess(pid))throw std::runtime_error("Elgato process changed during guard");
}
/* Windows byte-range lock interoperates with Python msvcrt.locking on the same
 * observer-PID.lock file. A named mutex alone would not exclude the old bridge. */
class ObserverLock {
    HANDLE file=INVALID_HANDLE_VALUE;OVERLAPPED position{};
public:
    ObserverLock()=default;ObserverLock(const ObserverLock&)=delete;
    ~ObserverLock(){if(file!=INVALID_HANDLE_VALUE){UnlockFileEx(file,0,1,0,&position);CloseHandle(file);}}
    void Acquire(const std::filesystem::path& directory,DWORD pid)
    {
        if(!directory.is_absolute())throw std::runtime_error("Observer lock directory must be absolute");
        std::filesystem::create_directories(directory);
        const auto path=directory/(L"observer-"+std::to_wstring(pid)+L".lock");
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                         nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Observer lock unavailable");
        if(!LockFileEx(file,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&position))
        {CloseHandle(file);file=INVALID_HANDLE_VALUE;throw std::runtime_error("Another compositor already owns this Elgato process");}
    }
};
}
