/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "KBHEController.h"
#include "KBHELayout.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

using namespace KBHEProtocol;

/* Link-time stubs only. No USB library is linked, so this executable cannot
 * enumerate, open, or send anything to a physical device. */
struct hid_device_
{
    std::vector<Report> writes;
    std::deque<std::vector<unsigned char>> replies;
    unsigned char mode = 2;
    unsigned char enabled = 0;
    bool capabilities = false;
    unsigned char cap_count = LED_COUNT;
    bool short_write = false;
    bool timeout = false;
    bool wrong_chunk_size = false;
    bool prepend_stale_chunk = false;
    bool numbered_reply = false;
    bool restore_token = false;
    unsigned char previous_mode = 2;
    bool force_restore_token_lost = false;
    bool fail_enable_off_once = false;
    bool bad_snapshot_size = false;
    bool corrupt_restore_readback = false;
    Frame live{};
    int closed = 0;
};

extern "C" int HID_API_CALL hid_write(hid_device* dev, const unsigned char* data, size_t size)
{
    assert(size == 65 && data[0] == 0 && data[2] == 0);
    Report report{};std::copy_n(data,size,report.begin());dev->writes.push_back(report);
    if(dev->short_write)return 64;
    Reply reply{};reply[0]=data[1];
    switch(data[1])
    {
    case 0x00:reply[2]=2;reply[3]=0;reply[4]=10;break;
    case 0x7F:
        if(!dev->capabilities)reply[1]=2;
        else {reply[2]=1;reply[4]=dev->cap_count;reply[5]=3;reply[6]=60;reply[7]=7;reply[8]=0x7F;}
        break;
    case 0x60:reply[2]=dev->enabled;break;
    case 0x61:
        if(data[3]==0 && dev->fail_enable_off_once) { dev->fail_enable_off_once=false; reply[1]=1; reply[2]=dev->enabled; }
        else reply[2]=dev->enabled=data[3];
        break;
    case 0x6E:reply[2]=dev->mode;break;
    case 0x6F:
        // Firmware settings.c: setting the current mode is a no-op. Only a
        // non-live -> live transition creates the previous-effect token.
        if(dev->mode != data[3]) {
            if(data[3]==7) {dev->previous_mode=dev->mode;dev->restore_token=true;}
            else dev->restore_token=false;
            dev->mode=data[3];
        }
        reply[2]=dev->mode;break;
    case 0x76:
        if(dev->force_restore_token_lost)dev->restore_token=false;
        if(dev->restore_token){dev->mode=dev->previous_mode;dev->restore_token=false;}
        else reply[1]=1;
        reply[2]=dev->mode;break;
    case 0x68: {
        const auto offset=static_cast<std::size_t>(data[3])*60;
        assert(offset<FRAME_BYTES);
        const auto count=std::min<std::size_t>(60,FRAME_BYTES-offset);
        reply[2]=data[3];reply[3]=static_cast<unsigned char>(dev->bad_snapshot_size ? 1 : count);
        std::copy_n(dev->live.begin()+offset,count,reply.begin()+4);
        if(dev->corrupt_restore_readback) reply[4]^=1;
        if(dev->prepend_stale_chunk) {
            auto stale=reply;stale[2]=99;stale[4]^=0xFF;
            dev->replies.emplace_back(stale.begin(),stale.end());
        }
        break;
    }
    case 0x6A:
        assert(dev->mode==7);reply[2]=data[3];reply[3]=dev->wrong_chunk_size?1:data[4];
        std::copy_n(data+5,data[4],dev->live.begin()+static_cast<std::size_t>(data[3])*60);
        if(dev->prepend_stale_chunk){auto stale=reply;stale[2]=99;dev->replies.emplace_back(stale.begin(),stale.end());}
        break;
    default:assert(false);
    }
    if(!dev->timeout)
    {
        std::vector<unsigned char> bytes(reply.begin(),reply.end());
        if(dev->numbered_reply)bytes.insert(bytes.begin(),0);
        dev->replies.push_back(bytes);
    }
    return 65;
}
extern "C" int HID_API_CALL hid_read_timeout(hid_device* dev, unsigned char* data, size_t size, int)
{
    if(dev->replies.empty())return 0;
    auto bytes=dev->replies.front();dev->replies.pop_front();assert(size>=bytes.size());
    std::copy(bytes.begin(),bytes.end(),data);return static_cast<int>(bytes.size());
}
extern "C" void HID_API_CALL hid_close(hid_device* dev){++dev->closed;}

static unsigned int Count(const hid_device_& dev,unsigned char command)
{
    return std::count_if(dev.writes.begin(),dev.writes.end(),[command](const Report& r){return r[1]==command;});
}
static Frame Gradient()
{
    Frame frame{};for(std::size_t i=0;i<frame.size();++i)frame[i]=static_cast<unsigned char>(i*19+5);return frame;
}
static void FramingAndLayout()
{
    const Frame frame=Gradient();const auto reports=FrameReports(frame);assert(reports.size()==5);
    Frame reconstructed{};
    for(std::size_t i=0;i<reports.size();++i)
    {
        const auto& r=reports[i];const unsigned int size=i==4?6:60;
        assert(r[0]==0&&r[1]==0x6A&&r[2]==0&&r[3]==i&&r[4]==size);
        std::copy_n(r.begin()+5,size,reconstructed.begin()+i*60);
        for(std::size_t p=5+size;p<65;++p)assert(r[p]==0);
    }
    assert(frame==reconstructed);
    std::set<unsigned int> positions;
    for(const auto& key:KBHELayout::KEYS)
    {
        assert(key.column<KBHELayout::WIDTH&&key.row<KBHELayout::HEIGHT);
        assert(key.name&&key.name[0]);positions.insert(key.row*KBHELayout::WIDTH+key.column);
    }
    assert(positions.size()==82);
    assert(KBHELayout::KEYS[0].column==4&&KBHELayout::KEYS[81].column==126);
    Reply caps{};caps[0]=0x7F;caps[2]=1;caps[4]=82;caps[5]=3;caps[6]=60;caps[7]=7;caps[8]=0x69;
    assert(CompatibleCapabilities(caps));caps[10]=1;assert(!CompatibleCapabilities(caps));
}
static void ProbeAndRestore()
{
    hid_device_ device;
    {
        KBHEController controller(&device,"mock","unit",true);
        assert(controller.Probe());assert(controller.GetVersion()=="2.0.10");
        assert(device.writes.size()==2&&device.mode==2&&device.enabled==0);
        assert(controller.EnterDirectMode());assert(device.mode==7&&device.enabled==1);
        assert(controller.SendFrame(Gradient()));assert(Count(device,0x6A)==5);
        std::size_t chunk=0;for(const auto& r:device.writes)if(r[1]==0x6A)assert(r==FrameReports(Gradient())[chunk++]);
    }
    assert(device.closed==1&&device.mode==2&&device.enabled==0&&Count(device,0x76)==1);
    hid_device_ unknown;
    {KBHEController controller(&unknown,"mock","unit",false);assert(!controller.Probe());assert(!controller.EnterDirectMode());}
    assert(Count(unknown,0x6F)==0&&Count(unknown,0x76)==0);
    hid_device_ advertised;advertised.capabilities=true;advertised.numbered_reply=true;
    {KBHEController controller(&advertised,"mock","unit",false);assert(controller.Probe());}
    advertised.cap_count=6;
    {KBHEController controller(&advertised,"mock","unit",true);assert(!controller.Probe());}
}
static void FaultsAndExternalMode()
{
    hid_device_ device;
    {
        KBHEController controller(&device,"mock","unit",true);assert(controller.Probe());assert(controller.EnterDirectMode());
        device.wrong_chunk_size=true;assert(!controller.SendFrame(Gradient()));assert(Count(device,0x6A)==1);
        device.wrong_chunk_size=false;device.prepend_stale_chunk=true;assert(controller.SendFrame(Gradient()));
        assert(Count(device,0x6A)==6);
        device.timeout=true;const auto count=Count(device,0x6A);assert(!controller.SendFrame(Gradient()));assert(Count(device,0x6A)==count);device.timeout=false;
        device.mode=4;assert(!controller.SendFrame(Gradient()));assert(Count(device,0x6A)==count);
        assert(controller.RestoreHardware());assert(device.mode==4&&Count(device,0x76)==0);
        assert(controller.EnterDirectMode());assert(controller.SendFrame(Gradient()));
        device.short_write=true;const auto old=Count(device,0x6A);assert(!controller.SendFrame(Gradient()));assert(Count(device,0x6A)==old);device.short_write=false;
    }
    assert(device.closed==1);
}
static void SerializedFrames()
{
    hid_device_ device;
    KBHEController controller(&device,"mock","unit",true);assert(controller.Probe());assert(controller.EnterDirectMode());
    Frame a{},b{};a.fill(17);b.fill(99);
    std::thread first([&](){assert(controller.SendFrame(a));});
    std::thread second([&](){assert(controller.SendFrame(b));});first.join();second.join();
    std::vector<Report> chunks;for(const auto& r:device.writes)if(r[1]==0x6A)chunks.push_back(r);
    assert(chunks.size()==10);for(std::size_t i=0;i<chunks.size();++i){assert(chunks[i][3]==i%5);assert(chunks[i][5]==chunks[i<5?0:5][5]);}
    assert(chunks[0][5]!=chunks[5][5]);
}
static void ExistingLiveSnapshotAndRestore()
{
    hid_device_ device;device.mode=7;device.enabled=0;device.live=Gradient();device.prepend_stale_chunk=true;
    const Frame initial=device.live;
    KBHEController controller(&device,"mock","unit",true);
    assert(controller.Probe());assert(controller.EnterDirectMode());
    assert(Count(device,0x68)==5);assert(!device.restore_token);
    Frame different{};different.fill(173);assert(controller.SendFrame(different));
    assert(device.live==different);
    assert(controller.RestoreHardware());
    assert(device.mode==7&&device.enabled==0&&device.live==initial);
    assert(controller.GetRestorationResult()=="initial_live_image_restored_prior_effect_unknown");
    assert(controller.GetLastError().empty());
    assert(Count(device,0x68)==10&&Count(device,0x76)==1&&Count(device,0x6F)==1);
    assert(controller.RestoreHardware());assert(Count(device,0x76)==1);
}
static void RestoreObservedModeAndRetryEnable()
{
    hid_device_ device;device.mode=5;device.force_restore_token_lost=true;
    KBHEController controller(&device,"mock","unit",true);
    assert(controller.Probe());assert(controller.EnterDirectMode());
    device.fail_enable_off_once=true;
    assert(!controller.RestoreHardware());assert(device.mode==5&&device.enabled==1);
    assert(Count(device,0x76)==1);
    assert(controller.RestoreHardware());assert(device.mode==5&&device.enabled==0);
    assert(controller.GetRestorationResult()=="observed_hardware_mode_restored");
    assert(Count(device,0x76)==1); // only enable restoration is retried
}
static void RejectIncompleteInitialLiveSnapshot()
{
    hid_device_ device;device.mode=7;device.bad_snapshot_size=true;
    KBHEController controller(&device,"mock","unit",true);
    assert(controller.Probe());assert(!controller.EnterDirectMode());
    assert(Count(device,0x6F)==0&&Count(device,0x61)==0&&Count(device,0x6A)==0);
    assert(controller.GetLastError().find("snapshot chunk size")!=std::string::npos);
    device.bad_snapshot_size=false;
    assert(controller.EnterDirectMode());assert(controller.RestoreHardware());
}
static void LiveRestorationRequiresExactReadback()
{
    hid_device_ device;device.mode=7;device.live=Gradient();
    KBHEController controller(&device,"mock","unit",true);
    assert(controller.Probe());assert(controller.EnterDirectMode());
    device.corrupt_restore_readback=true;
    assert(!controller.RestoreHardware());
    assert(controller.GetLastError().find("readback differs")!=std::string::npos);
    device.corrupt_restore_readback=false;
    assert(controller.RestoreHardware());assert(device.live==Gradient());
}
static void BlackExit()
{
    hid_device_ device;
    {
        KBHEController controller(&device,"mock","unit",true);
        assert(controller.Probe()&&controller.EnterDirectMode());
        assert(controller.SendFrame(Gradient()));
        const auto count=Count(device,0x6A);
        // The final core color is black but no ordinary SendFrame delivered it.
        assert(controller.KeepBlackOnExit());
        assert(device.live==Frame{} && Count(device,0x6A)==count+5 && Count(device,0x68)==5);
        assert(controller.GetRestorationResult()=="black_exit_confirmed");
    }
    assert(device.closed==1 && device.mode==LIVE_MODE && Count(device,0x76)==0 && device.live==Frame{});
    hid_device_ external;
    {
        KBHEController controller(&external,"mock","unit",true);
        assert(controller.Probe()&&controller.EnterDirectMode());external.mode=3;
        assert(controller.KeepBlackOnExit());
        assert(controller.GetRestorationResult()=="external_mode_preserved");
    }
    assert(external.mode==3&&Count(external,0x6A)==0&&Count(external,0x76)==0);
    hid_device_ disconnected;
    {
        KBHEController controller(&disconnected,"mock","unit",true);
        assert(controller.Probe()&&controller.EnterDirectMode());disconnected.short_write=true;
        const auto count=disconnected.writes.size();
        assert(!controller.KeepBlackOnExit());
        assert(disconnected.writes.size()==count+1 && controller.GetRestorationResult()=="black_exit_unconfirmed");
    }
    assert(disconnected.closed==1 && Count(disconnected,0x76)==0);
    hid_device_ mismatch;
    {
        KBHEController controller(&mismatch,"mock","unit",true);
        assert(controller.Probe()&&controller.EnterDirectMode());mismatch.corrupt_restore_readback=true;
        assert(!controller.KeepBlackOnExit());
        assert(controller.GetLastError().find("readback differs")!=std::string::npos);
    }
    assert(Count(mismatch,0x76)==0);
    hid_device_ untouched;
    {KBHEController controller(&untouched,"mock","unit",true);assert(controller.KeepBlackOnExit());}
    assert(untouched.writes.empty()&&untouched.closed==1);
}
int main()
{
    FramingAndLayout();ProbeAndRestore();FaultsAndExternalMode();SerializedFrames();
    ExistingLiveSnapshotAndRestore();RestoreObservedModeAndRetryEnable();RejectIncompleteInitialLiveSnapshot();
    LiveRestorationRequiresExactReadback();
    BlackExit();
    std::cout<<"KBHE framing, 82-key geometry, capabilities, legacy identity, matching ACKs, bounded failures, mode restoration, orphan-live snapshot/readback, retry enable and serialized frames: PASS\n";
}
