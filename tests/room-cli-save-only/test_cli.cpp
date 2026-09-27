/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vector>
#include <tuple>
#include <string>
#include <iostream>
#include <cstdlib>

enum {MODE_COLORS_NONE,MODE_COLORS_RANDOM,MODE_COLORS_PER_LED,MODE_COLORS_MODE_SPECIFIC};
enum {MODE_FLAG_HAS_RANDOM_COLOR=1,MODE_FLAG_HAS_BRIGHTNESS=2,MODE_FLAG_HAS_SPEED=4};
unsigned ToRGBColor(unsigned r,unsigned g,unsigned b){return r|(g<<8)|(b<<16);}
struct DeviceOptions
{
    bool hasOption=false,random_colors=false;
    unsigned device=0,brightness=100,speed=50,selected_mode=0;
    int zone=-1;
    std::vector<std::tuple<unsigned,unsigned,unsigned>> colors;
};
struct ProfileManager { unsigned clears=0; void ClearActiveProfile(){++clears;} };
struct ResourceManager
{
    ProfileManager profiles;
    static ResourceManager* get(){static ResourceManager value;return &value;}
    ProfileManager* GetProfileManager(){return &profiles;}
};
struct RGBController
{
    unsigned color_mode=MODE_COLORS_MODE_SPECIFIC,flags=0,active=99,updates=0,brightness=0,speed=0,count=0;
    std::vector<std::pair<unsigned,unsigned>> writes;
    unsigned GetModeFlags(unsigned){return flags;}
    void SetModeColorMode(unsigned,unsigned value){color_mode=value;}
    unsigned GetModeBrightnessMax(unsigned){return 210;}
    unsigned GetModeBrightnessMin(unsigned){return 10;}
    void SetModeBrightness(unsigned,unsigned value){brightness=value;}
    unsigned GetModeSpeedMax(unsigned){return 110;}
    unsigned GetModeSpeedMin(unsigned){return 10;}
    void SetModeSpeed(unsigned,unsigned value){speed=value;}
    unsigned GetModeColorMode(unsigned){return color_mode;}
    unsigned GetLEDCount(){return 3;}
    unsigned GetZoneStartIndex(int){return 1;}
    unsigned GetLEDsInZone(int){return 2;}
    void SetColor(unsigned index,unsigned value){writes.push_back({index,value});}
    unsigned GetModeColorsMin(unsigned){return 1;}
    unsigned GetModeColorsMax(unsigned){return 1;}
    void SetModeColorsCount(unsigned,unsigned value){count=value;}
    void SetModeColor(unsigned,unsigned index,unsigned value){writes.push_back({index,value});}
    std::string GetModeName(unsigned){return "Static fixture requiring one color";}
    void SetActiveMode(unsigned value){active=value;}
    void DeviceUpdateLEDs(){++updates;}
};
unsigned parse_calls=0;
unsigned ParseMode(DeviceOptions& options,std::vector<RGBController*>&){++parse_calls;return options.selected_mode;}
#include "apply-options.inc"

int main()
{
    unsigned checks=0;
    auto check=[&](bool condition){++checks;if(!condition){std::cerr<<"Assertion "<<checks<<" failed\n";std::exit(2);}};
    RGBController fixture;
    std::vector<RGBController*> devices{&fixture};
    DeviceOptions options;
    // The old code prints an error and exit(0) here, so only the parent-process
    // completion sentinel distinguishes that failure from a successful run.
    ApplyOptions(options,devices);
    check(ResourceManager::get()->profiles.clears==0);
    check(parse_calls==0 && fixture.active==99 && fixture.writes.empty() && fixture.count==0);
    check(fixture.updates==0);
    std::vector<RGBController*> empty;
    options.device=999;
    ApplyOptions(options,empty); // Guard must also precede controller indexing.
    check(parse_calls==0);

    options.device=0;options.hasOption=true;options.selected_mode=4;options.colors={{7,11,23}};
    ApplyOptions(options,devices);
    check(ResourceManager::get()->profiles.clears==1 && parse_calls==1);
    check(fixture.active==4 && fixture.count==1);
    check(fixture.writes==std::vector<std::pair<unsigned,unsigned>>{{0,ToRGBColor(7,11,23)}});
    check(fixture.updates==0);

    fixture=RGBController();fixture.color_mode=MODE_COLORS_PER_LED;
    options.colors.clear();options.selected_mode=2;
    ApplyOptions(options,devices); // Explicit direct mode, no color: unchanged.
    check(fixture.active==2 && fixture.updates==1 && fixture.writes.empty());
    options.colors={{1,2,3},{5,7,9}};
    ApplyOptions(options,devices);
    check(fixture.writes==std::vector<std::pair<unsigned,unsigned>>{{0,ToRGBColor(1,2,3)},{1,ToRGBColor(5,7,9)},{2,ToRGBColor(5,7,9)}});
    check(fixture.updates==2);

    fixture.writes.clear();options.zone=0;options.colors={{4,5,6}};
    ApplyOptions(options,devices);
    check(fixture.writes==std::vector<std::pair<unsigned,unsigned>>{{1,ToRGBColor(4,5,6)},{2,ToRGBColor(4,5,6)}});
    fixture.flags=MODE_FLAG_HAS_BRIGHTNESS|MODE_FLAG_HAS_SPEED;options.brightness=25;options.speed=70;
    ApplyOptions(options,devices);
    check(fixture.brightness==60 && fixture.speed==80);
    fixture.flags|=MODE_FLAG_HAS_RANDOM_COLOR;options.random_colors=true;options.colors.clear();
    ApplyOptions(options,devices);
    check(fixture.color_mode==MODE_COLORS_RANDOM);
    check(ResourceManager::get()->profiles.clears==6 && parse_calls==6);
    std::cout<<checks<<" assertions; CLI_SAVE_ONLY_ALL_CHECKS_PASSED\n";
}
