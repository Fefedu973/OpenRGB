/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "NVIDIAIlluminationColor.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using NVIDIAIlluminationColor::MonochromeBrightness;
    assert(MonochromeBrightness(0,0,0,100)==0);
    assert(MonochromeBrightness(255,255,255,100)==100);
    assert(MonochromeBrightness(128,128,128,100)==50);
    assert(MonochromeBrightness(0,64,0,100)==25);
    assert(MonochromeBrightness(128,10,20,50)==25);
    assert(MonochromeBrightness(255,0,0,0)==0);
    assert(MonochromeBrightness(255,255,255,255)==100);
    unsigned int checked=0;
    for(unsigned int percent=0;percent<=100;++percent)
    {
        unsigned int previous=0;
        for(unsigned int channel=0;channel<=255;++channel)
        {
            const auto brightness=MonochromeBrightness(channel,0,0,percent);
            const auto expected=std::lround(static_cast<double>(channel)*percent/255.0);
            assert(brightness==expected && brightness>=previous && brightness<=percent);
            assert(MonochromeBrightness(0,channel,0,percent)==brightness);
            assert(MonochromeBrightness(0,0,channel,percent)==brightness);
            assert(MonochromeBrightness(channel,channel,channel,percent)==brightness);
            previous=brightness;++checked;
        }
    }
    std::cout<<checked<<" levels x RGB/gray: bridge-equivalent monochrome intensity PASS (no NVAPI)\n";
}
