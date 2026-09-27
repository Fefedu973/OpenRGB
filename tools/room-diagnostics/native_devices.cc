// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit, bounded hardware diagnostics. No OpenRGB detector registry is run.
// Including this translation unit exposes the SAME private WinRT transport used
// by the shipping driver; this executable does not provide another BLE backend.
#include "../../Controllers/GoveeBluetoothController/GoveeBluetoothController_Windows.cpp"
#include "../../Controllers/KBHEController/KBHEController.h"
#include "../../Controllers/AlienwareMonitorController/AlienwareMonitorController/AlienwareMonitorController.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <set>
using json=nlohmann::json;
using namespace std::chrono_literals;

static std::string Read(const std::filesystem::path& file,size_t maximum)
{
    if(!file.is_absolute() || !std::filesystem::is_regular_file(file) || std::filesystem::file_size(file)>maximum)
        throw std::runtime_error("Invalid private input file");
    std::ifstream input(file,std::ios::binary);
    if(!input) throw std::runtime_error("Private input unavailable");
    return std::string(std::istreambuf_iterator<char>(input),{});
}
static uint64_t Milliseconds()
{ return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
static GoveeBluetooth::RGB Rainbow(unsigned index,unsigned total)
{
    GoveeBluetooth::RGB c{};
    for(unsigned i=0;i<3;++i)c[i]=uint8_t(std::lround(127.5*(1+std::sin(6.28318530718*(double(index)/total-double(i)/3)))));
    return c;
}
struct BleOwner
{
    HANDLE handle=nullptr;
    explicit BleOwner(uint64_t address)
    {
        std::ostringstream name;name<<"Local\\OpenRGB-Govee-BLE-"<<std::hex<<address;
        handle=CreateMutexA(nullptr,FALSE,name.str().c_str());
        const DWORD result=handle?WaitForSingleObject(handle,0):WAIT_FAILED;
        if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED)
        {if(handle)CloseHandle(handle);handle=nullptr;throw std::runtime_error("Device already has an OpenRGB BLE owner");}
    }
    ~BleOwner(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
};

// Observe restoration on the SAME authenticated connection before it closes.
// Re-authenticating immediately after Windows disposes a GATT session is a
// separate reconnect test and must not erase evidence of the restored state.
class DiagnosticTransport : public GoveeBluetooth::WindowsTransport
{
public:
    using WindowsTransport::WindowsTransport;
    bool verify_on_close=false, verified=false;
    GoveeBluetooth::Packet power{},brightness{},mode{};
    std::string verification_error;
    void Disconnect() noexcept override
    {
        if(verify_on_close)
        {
            verify_on_close=false;
            try
            {
                if(!Connected()) throw std::runtime_error("Connection lost before restoration readback");
                power=Query(1);brightness=Query(4);mode=Query(5);verified=true;
            }
            catch(const std::exception& error){verification_error=error.what();}
            catch(...){verification_error="Restoration readback failed";}
        }
        WindowsTransport::Disconnect();
    }
};

static json ExerciseGovee(const std::filesystem::path& config_path,unsigned index)
{
    using namespace GoveeBluetooth;
    Configuration config;
    try
    {
        const auto data=json::parse(Read(config_path,65536));
        const auto& item=data.at("devices").at(index);
        if(!item.value("enabled",true))throw std::runtime_error("Disabled");
        const auto profile=item.at("profile").get<std::string>();
        if(profile!="h6159-classic-v1" && profile!="h6008-realtime-v1")throw std::runtime_error("Unsupported");
        config.profile=profile=="h6159-classic-v1"?Profile::H6159:Profile::H6008;
        config.address=ParseAddress(item.at("address").get<std::string>());
        if(config.profile==Profile::H6008)
        {
            config.wifi_mac=ParseAddress(item.at("wifi_mac").get<std::string>());
            config.key=ParseKey(Read(std::filesystem::u8path(item.value("key_file",data.value("key_file",std::string()))),256));
        }
    }
    catch(...) { throw std::runtime_error("Private device configuration rejected (values redacted)"); }
    BleOwner owner(config.address);
    std::atomic<bool> stopping{false};
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    struct Apartment { ~Apartment(){winrt::uninit_apartment();} } apartment;
    DiagnosticTransport transport(config,stopping);
    auto trace=[](const char* phase){std::cerr << "Native diagnostic phase: " << phase << '\n';};
    trace("initial connection");
    for(unsigned attempt=0;;++attempt)
    {
        try {transport.Connect();break;}
        catch(...) {transport.Disconnect();if(attempt==2)throw;std::this_thread::sleep_for(1500ms);}
    }
    trace("initial power query");const auto initial_power=transport.Query(1);
    trace("initial brightness query");const auto initial_brightness=transport.Query(4);
    trace("initial mode query");const auto initial_mode=transport.Query(5);
    json result={{"model",config.profile==Profile::H6159?"H6159":"H6008"},{"index",index},
                 {"initialPower",initial_power[2]},{"initialMode",initial_mode[2]}};
    const bool inherited_realtime = config.profile==Profile::H6008 && initial_mode[2]==5;
    if(!RestorableMode(config.profile,initial_mode) && !inherited_realtime)
        throw std::runtime_error("Initial scene is not supported by the native session");
    // --exercise is explicit permission for a temporary ON acquisition; production
    // defaults remain profile-controlled. Release must restore the original power.
    Session session(config.profile,transport,true);
    try
    {
        unsigned frames=0;
        for(const RGB& color : {RGB{{255,0,0}},RGB{{0,255,0}},RGB{{0,0,255}}})
        {
            trace("primary color / native session acquisition");
            session.Step(Frame{color,65},Milliseconds());++frames;
            if(session.State()!="streaming")throw std::runtime_error("Native session is not streaming");
            std::this_thread::sleep_for(900ms);
        }
        if(config.profile==Profile::H6159)
        {
            session.Step(Frame{{{100,30,10}},0},Milliseconds());
            if(transport.Query(1)[2]!=0)throw std::runtime_error("Zero brightness did not power off");
            std::this_thread::sleep_for(600ms);
            session.Step(Frame{{{20,100,160}},65},Milliseconds());
            if(transport.Query(1)[2]!=1)throw std::runtime_error("Brightness resume did not power on");
            result["zeroBrightnessPowerCycle"]=true;
        }
        const auto start=Milliseconds();
        trace("rainbow");
        for(unsigned frame=0;frame<60;++frame)
        {
            session.Step(Frame{Rainbow(frame,60),65},Milliseconds());++frames;
            std::this_thread::sleep_for(config.profile==Profile::H6008?50ms:100ms);
        }
        result["rainbowSeconds"]=(Milliseconds()-start)/1000.0;
        result["submittedFrames"]=frames;
        result["streamingState"]=session.State();
        result["recoveredUnknownPriorColor"]=session.RecoveredBaseline();
        trace("release, verify state, and disconnect");transport.verify_on_close=true;transport.BeginRestore();session.Release();
    }
    catch(...)
    {
        transport.BeginRestore();try {session.Release();} catch(...) {}
        throw;
    }
    if(!transport.verified)throw std::runtime_error(transport.verification_error.empty()?"Missing restoration readback":transport.verification_error);
    const auto& power=transport.power;const auto& brightness=transport.brightness;const auto& mode=transport.mode;
    result["restoredPower"]=power[2]==initial_power[2];
    result["restoredBrightness"]=brightness[2]==initial_brightness[2];
    result["restoredMode"]=inherited_realtime ? MatchesColor(config.profile,mode,ScaledRGB(Frame{{{255,0,0}},65})) : mode==initial_mode;
    if(inherited_realtime)result["modeRestorationBasis"]="Prior realtime RGB is not readable; restored the explicit first-frame baseline";
    SecureZeroMemory(config.key.data(),config.key.size());
    if(!result["restoredPower"].get<bool>() || !result["restoredBrightness"].get<bool>() || !result["restoredMode"].get<bool>())
    {
        result["observedPower"]=power[2];result["observedBrightness"]=brightness[2];
        result["initialBrightness"]=initial_brightness[2];
        result["initialModePacket"]=initial_mode;result["observedModePacket"]=mode;
        std::cerr<<result.dump(2)<<'\n';
        throw std::runtime_error("Native restoration readback differs from initial state");
    }
    return result;
}

struct HidList { hid_device_info* value; ~HidList(){hid_free_enumeration(value);} };
static json ExerciseUSB(bool kbhe)
{
    HidList devices{hid_enumerate(kbhe?0x9172:0x187c,kbhe?0x0002:0x101d)};
    hid_device_info* selected=nullptr;
    for(auto* device=devices.value;device;device=device->next)
    {
        const bool match=kbhe?(device->interface_number==1 && device->usage_page==0xff00 && device->usage==1):
                              (device->usage_page==0xffda && device->usage==0xda);
        if(match){if(selected)throw std::runtime_error("Multiple matching devices; no ambiguous selection allowed");selected=device;}
    }
    if(!selected)throw std::runtime_error("Requested RGB interface is not present");
    auto* handle=hid_open_path(selected->path);
    if(!handle)throw std::runtime_error("RGB interface could not be opened");
    if(kbhe)
    {
        const bool legacy=selected->product_string && std::wstring(selected->product_string).find(L"75HE")!=std::wstring::npos;
        KBHEController controller(handle,selected->path,"",legacy);
        if(!controller.Probe())throw std::runtime_error(controller.GetLastError());
        if(!controller.EnterDirectMode())throw std::runtime_error(controller.GetLastError());
        for(unsigned step=0;step<90;++step)
        {
            KBHEProtocol::Frame frame{};
            for(unsigned led=0;led<KBHEProtocol::LED_COUNT;++led)
            {auto color=Rainbow((step+led)%90,90);std::copy(color.begin(),color.end(),frame.begin()+led*3);}
            if(!controller.SendFrame(frame))throw std::runtime_error(controller.GetLastError());
            std::this_thread::sleep_for(65ms);
        }
        if(!controller.RestoreHardware())throw std::runtime_error(controller.GetLastError());
        return {{"model","KBHE 75HE"},{"frames",90},{"leds",82},{"firmware",controller.GetVersion()},
                {"restored",true},{"restoration",controller.GetRestorationResult()}};
    }
    const auto* profile=AlienwareMonitor::FindProfile(0x187c,0x101d);
    if(!profile){hid_close(handle);throw std::runtime_error("AW3426DW profile missing");}
    AlienwareMonitorController controller(handle,selected->path,*profile);
    if(!controller.Initialize())throw std::runtime_error("AW3426DW initialization failed");
    for(unsigned step=0;step<120;++step)
    {
        std::vector<AlienwareMonitorController::Color> colors;
        for(unsigned zone=0;zone<profile->zones.size();++zone)
        {auto color=Rainbow((step+zone*40)%120,120);colors.push_back(color);}
        controller.SubmitColors(colors);
        std::this_thread::sleep_for(50ms);
    }
    // Exercise the nonblocking production path, then confirm one synchronous
    // final transfer. No unsupported 'get original color' operation is invented.
    std::this_thread::sleep_for(250ms);
    if(!controller.SendColor(profile->AllZones(),0,0,0))throw std::runtime_error("AW3426DW final transfer failed");
    return {{"model",profile->name},{"submittedFrames",120},{"zones",profile->zones.size()},
            {"usbIdentity","187C:101D"},{"authentication",profile->authentication},{"minimumIntervalMs",profile->delay_ms},
            {"restoration","Resume the previous RGB owner; this protocol does not expose a saved color"}};
}
int main(int argc,char** argv)
{
    try
    {
        if(argc<3 || std::string(argv[1])!="--exercise")
        {std::cerr<<"Explicit hardware use: --exercise govee ABS_CONFIG INDEX | kbhe | alienware\nClose all previous RGB owners first. No general scan is performed.\n";return 2;}
        const std::string target=argv[2];json result;
        if(target=="govee" && argc==5)result=ExerciseGovee(std::filesystem::u8path(argv[3]),std::stoul(argv[4]));
        else if((target=="kbhe" || target=="alienware") && argc==3)result=ExerciseUSB(target=="kbhe");
        else throw std::runtime_error("Invalid explicit diagnostic target");
        result["nativeProductionTransport"]=true;result["opticalConfirmationRequired"]=true;
        std::cout<<result.dump(2)<<'\n';return 0;
    }
    catch(const winrt::hresult_error& e){std::cerr<<"Windows transport error 0x"<<std::hex<<unsigned(e.code().value)<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<"Diagnostic failed: "<<e.what()<<'\n';return 1;}
}
