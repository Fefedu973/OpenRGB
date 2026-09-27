/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QThread>
#include <atomic>
#include <iostream>
#include <fstream>
#include <numeric>
#include "FakeAPI.h"
#include "FrameRouting/RGBControllerImageInterface.h"
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
using json=nlohmann::json;
class Capture : public RGBController,public room_image::RGBControllerImageInterface
{
public:
    Capture()
    {
        name="DLL Synthetic Output";vendor="Room tests";description="Synthetic only";version="test";serial="dll-synthetic";location="test-only";
        leds.resize(32*18);zone z;z.name="Native Frame";z.type=ZONE_TYPE_MATRIX;z.leds_count=z.leds_min=z.leds_max=32*18;
        z.matrix_map.width=32;z.matrix_map.height=18;z.matrix_map.map.resize(32*18);std::iota(z.matrix_map.map.begin(),z.matrix_map.map.end(),0u);zones.push_back(z);
        mode m;m.name="Direct";m.flags=MODE_FLAG_HAS_PER_LED_COLOR;m.color_mode=MODE_COLORS_PER_LED;modes.push_back(m);SetupColors();
    }
    ~Capture(){Shutdown();}
    std::atomic<unsigned> submissions{0},led_updates{0};std::mutex mutex;
    std::shared_ptr<const room_image::Frame> captured;room_image::Mapping mapping;
    bool GetImageOutput(unsigned z,room_image::Output& output)const override{if(z)return false;output={0,800,600,60};return true;}
    room_image::SubmitResult SubmitImage(unsigned z,std::shared_ptr<const room_image::Frame> f,const room_image::Mapping& m,unsigned lease)override
    {if(z||!f||!f->Valid()||lease<100)return room_image::SubmitResult::Invalid;std::lock_guard<std::mutex> lock(mutex);captured=std::move(f);mapping=m;++submissions;return room_image::SubmitResult::Accepted;}
    void DeviceUpdateLEDs()override{++led_updates;}
};
class SegmentedCapture : public RGBController
{
public:
    std::atomic<unsigned> updates{0};
    SegmentedCapture()
    {
        name="DLL Synthetic Segments";vendor="Room tests";serial="dll-segments";location="test-only-segments";
        leds.resize(6);zone prefix;prefix.name="Untouched prefix";prefix.type=ZONE_TYPE_LINEAR;
        prefix.leds_count=prefix.leds_min=prefix.leds_max=2;zones.push_back(prefix);
        zone z;z.name="Two independent components";z.type=ZONE_TYPE_SEGMENTED;
        z.leds_count=z.leds_min=z.leds_max=4;
        for(unsigned i=0;i<2;++i){segment s;s.name="Part "+std::to_string(i);s.type=ZONE_TYPE_LINEAR;s.start_idx=2*i;s.leds_count=2;z.segments.push_back(s);}
        zones.push_back(z);mode m;m.name="Direct";m.color_mode=MODE_COLORS_PER_LED;modes.push_back(m);SetupColors();
    }
    ~SegmentedCapture(){Shutdown();}
    void DeviceUpdateLEDs()override{++updates;}
};
class HarnessAPI:public FakeAPI
{
public:
    filesystem::path directory;
    explicit HarnessAPI(const QString& path):directory(path.toStdString()){}
    filesystem::path GetConfigurationDirectory()override{return directory;}
    nlohmann::json GetSettings(std::string)override{return json::object();}
};
static void configure(const QString& root)
{
    json points=json::array();for(unsigned y=0;y<18;++y)for(unsigned x=0;x<32;++x)points.push_back({{"led_num",y*32+x},{"x",x},{"y",y}});
    json controller={{"name","DLL Synthetic Output"},{"vendor","Room tests"},{"description","Synthetic only"},{"version","test"},{"serial","dll-synthetic"},{"location","test-only"}};
    json settings={{"shape",2},{"x",80},{"y",60},{"scale",10},{"led_spacing",1},{"reverse",false},{"custom_shape",{{"w",32},{"h",18},{"led_positions",points}}}};
    json map={{"ctrl_zones",json::array({{{"controller",controller},{"zone_idx",0},{"custom_zone_name","Synthetic native rectangle"},{"settings",settings}}})},
        {"grid_settings",{{"w",800},{"h",600},{"show_grid",false},{"show_bounds",true},{"grid_size",10},{"snap_to_grid",false},{"auto_load",true},{"auto_register",true},{"hide_members",false}}}};
    const json segmented_controller={{"name","DLL Synthetic Segments"},{"vendor","Room tests"},{"serial","dll-segments"},{"location","test-only-segments"}};
    for(unsigned i=0;i<2;++i)
    {
        json part_settings={{"shape",2},{"x",80+400*i},{"y",500},{"scale",10},{"led_spacing",1},{"reverse",false},{"brightness",i ? 1.0 : 0.5},
            {"custom_shape",{{"w",2},{"h",1},{"led_positions",json::array({{{"led_num",0},{"x",0},{"y",0}},{{"led_num",1},{"x",1},{"y",0}}})}}}};
        map["ctrl_zones"].push_back({{"controller",segmented_controller},{"zone_idx",1},{"is_segment",true},{"segment_idx",i},{"settings",part_settings}});
    }
    const auto path=root+"/plugins/settings/virtual-controllers";CHECK(QDir().mkpath(path));
    std::ofstream file((path+"/synthetic.json").toStdString());file<<map.dump(2);CHECK(file.good());
}
int main(int argc,char** argv)
{
    qputenv("QT_QPA_PLATFORM","offscreen");QApplication application(argc,argv);
    try
    {
        CHECK(argc==3);QTemporaryDir temporary;CHECK(temporary.isValid());configure(temporary.path());
        Capture capture;SegmentedCapture segments;HarnessAPI api(temporary.path());api.physical.push_back(&capture);api.physical.push_back(&segments);
        QPluginLoader visual(QString::fromLocal8Bit(argv[1]));visual.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        QObject* object=visual.instance();if(!object)throw std::runtime_error(visual.errorString().toStdString());
        auto* plugin=qobject_cast<OpenRGBPluginInterface*>(object);CHECK(plugin&&plugin->GetPluginAPIVersion()==5);plugin->Load(&api);
        CHECK(plugin->GetWidget());CHECK(api.attachments==1&&api.created.size()==1);
        auto* image=dynamic_cast<room_image::RGBControllerImageInterface*>(api.created.front());CHECK(image);
        room_image::Output output;CHECK(image->GetImageOutput(0,output));CHECK(output.width==800&&output.height==600);
        CHECK(api.created.front()->GetLEDCount()<16382);
        auto pixels=std::make_shared<std::vector<uint8_t>>(800*600*4);
        for(unsigned y=0;y<600;++y)for(unsigned x=0;x<800;++x){auto i=(y*800+x)*4;(*pixels)[i]=173;(*pixels)[i+1]=uint8_t(y%256);(*pixels)[i+2]=uint8_t(x%256);(*pixels)[i+3]=255;}
        auto frame=std::make_shared<const room_image::Frame>(room_image::Frame{800,600,3200,991,pixels});
        constexpr unsigned image_lease_ms=1500;
        QElapsedTimer lease_timer;lease_timer.start();
        const auto prior_led=capture.led_updates.load();CHECK(image->SubmitImage(0,frame,{},image_lease_ms)==room_image::SubmitResult::Accepted);
        // Preview is a live lease, not an archive of the last submitted frame.
        // Check it before waiting for the independent consumer; a stalled test
        // process must not accidentally require an expired preview to survive.
        std::shared_ptr<const room_image::Frame> preview;room_image::Mapping mapping;
        const bool preview_available=image->GetImagePreview(0,preview,mapping);
        const auto preview_elapsed_ms=lease_timer.elapsed();
        if(preview_elapsed_ms>=image_lease_ms)
            throw std::runtime_error("Test scheduling exceeded image lease before preview: "+std::to_string(preview_elapsed_ms)+"ms / "+std::to_string(image_lease_ms)+"ms");
        CHECK(preview_available);CHECK(preview==frame);
        QElapsedTimer timer;timer.start();while((!capture.submissions||!segments.updates)&&timer.elapsed()<2500){application.processEvents();QThread::msleep(2);}
        CHECK(capture.submissions>0);
        {std::lock_guard<std::mutex> lock(capture.mutex);CHECK(capture.captured==frame);CHECK(std::abs(capture.mapping.origin_x-.1)<1e-8);CHECK(std::abs(capture.mapping.origin_y-.1)<1e-8);CHECK(std::abs(capture.mapping.u_x-.4)<1e-8);CHECK(std::abs(capture.mapping.v_y-.3)<1e-8);}
        CHECK(capture.led_updates==prior_led);
        CHECK(segments.updates>0);
        CHECK(segments.GetColor(0)==0&&segments.GetColor(1)==0);
        CHECK(RGBGetBValue(segments.GetColor(2))>=85&&RGBGetBValue(segments.GetColor(2))<=87);
        CHECK(RGBGetBValue(segments.GetColor(4))==173);
        CHECK(segments.GetColor(2)!=segments.GetColor(4));
        const unsigned compat_leds=api.created.front()->GetLEDCount();
        // Loading the Effects DLL without any profile/effect cannot start capture.
        QPluginLoader effects(QString::fromLocal8Bit(argv[2]));effects.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        auto* effects_object=effects.instance();if(!effects_object)throw std::runtime_error(effects.errorString().toStdString());
        auto* effects_plugin=qobject_cast<OpenRGBPluginInterface*>(effects_object);CHECK(effects_plugin&&effects_plugin->GetPluginAPIVersion()==5);
        effects_plugin->Load(&api);CHECK(effects_plugin->GetWidget());effects_plugin->Unload();CHECK(effects.unload());
        plugin->Unload();CHECK(visual.unload());application.processEvents();CHECK(api.created.empty());CHECK(api.detachments>=1);
        std::cout<<"{\"ok\":true,\"realDlls\":2,\"mockHostApi\":true,\"pluginApiVersion\":5,\"crossDllImageRtti\":true,\"image\":[800,600],\"compatibilityLeds\":"<<compat_leds<<",\"previewElapsedMs\":"<<preview_elapsed_ms<<",\"previewLeaseMs\":"<<image_lease_ms<<",\"exactSharedFrame\":true,\"mappedNativeSink\":true,\"effectsLoadedWithoutCapture\":true,\"cleanUnload\":true}\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"Plugin DLL test: "<<error.what()<<'\n';return 1;}
}
