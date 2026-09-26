/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../ImageProtocol.h"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

static void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main()
{
    using namespace room_image;
    const auto pixels=std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{
        0,0,255,255, 0,255,0,255, 33,44,55,66,
        255,0,0,255, 255,255,255,255, 77,88,99,0});
    Frame f{2,2,12,0x0102030405060708ull,pixels};
    Mapping identity;
    Check(f.Valid(),"padded frame rejected");
    Check(SampleBGRA(f,identity,0.25,0.25)==0xffff0000u,"top-left red");
    Check(SampleBGRA(f,identity,0.5,0.5)==0xff808080u,"bilinear center");
    auto flip=Mapping::Rectangle(0,0,1,1,0,true,false);
    Check(SampleBGRA(f,flip,0.25,0.25)==0xff00ff00u,"mirrored source");
    auto gain=identity; gain.brightness=0.5;
    Check(SampleBGRA(f,gain,0.25,0.25)==0xff800000u,"per-output gain");
    auto off=Mapping::Rectangle(2,2,1,1);
    Check(SampleBGRA(f,off,0.5,0.5)==0xff000000u,"off-scene must be black");
    auto b=wire::EncodeFrame(17,f,identity,1500);
    const uint8_t expected_prefix[]={0x52,0x49,0x4d,0x47,1,0,0,0,17,0,0,0,2,0,0,0,2,0,0,0,12,0,0,0,1,0,0,0,0xdc,5,0,0,8,7,6,5,4,3,2,1};
    Check(std::equal(std::begin(expected_prefix),std::end(expected_prefix),b.begin()),"wire endian/header mismatch");
    Check(b.size()==124 && b[96]==24 && b[100]==0 && b[102]==255,"payload offset/padding mismatch");
    unsigned zone,lease; Mapping m; std::shared_ptr<const Frame> parsed;
    Check(wire::DecodeFrame(b.data(),b.size(),zone,parsed,m,lease),"decode valid");
    Check(zone==17 && lease==1500 && parsed->sequence==f.sequence && *parsed->pixels==*pixels,"round-trip fields");
    for(size_t n=0;n<b.size();++n) Check(!wire::DecodeFrame(b.data(),n,zone,parsed,m,lease),"truncation accepted");
    for(const size_t offset:{size_t(0),size_t(4),size_t(12),size_t(16),size_t(20),size_t(24),size_t(28),size_t(96)})
    {
        auto bad=b; for(unsigned i=0;i<4;++i) bad[offset+i]=0xff;
        Check(!wire::DecodeFrame(bad.data(),bad.size(),zone,parsed,m,lease),"invalid field accepted");
    }
    auto bad=b; bad.push_back(0);
    Check(!wire::DecodeFrame(bad.data(),bad.size(),zone,parsed,m,lease),"trailing byte accepted");
    bad=b; const uint64_t nan=0x7ff8000000000000ull;
    for(unsigned i=0;i<8;++i) bad[40+i]=uint8_t(nan>>(8*i));
    Check(!wire::DecodeFrame(bad.data(),bad.size(),zone,parsed,m,lease),"NaN affine accepted");
    auto descriptors=wire::EncodeOutputs({{3,800,600,60},{9,3840,2160,30}});
    std::vector<Output> outputs;
    Check(wire::DecodeOutputs(descriptors.data(),descriptors.size(),outputs)&&outputs.size()==2&&outputs[1].zone==9,"arbitrary output descriptors");
    const auto duplicates=wire::EncodeOutputs({{3,800,600,60},{3,320,240,30}});
    Check(!wire::DecodeOutputs(duplicates.data(),duplicates.size(),outputs),"duplicate zone accepted");
    Frame hd{800,600,3200,42,std::make_shared<const std::vector<uint8_t>>(800*600*4,127)};
    const auto image=wire::EncodeFrame(9,hd,identity,1000);
    Check(wire::DecodeFrame(image.data(),image.size(),zone,parsed,m,lease)&&parsed->width*parsed->height>65535,"high-resolution frame truncated to LED count");
    std::mt19937 random(12345);
    for(unsigned test=0;test<4000;++test)
    {
        auto mutated=b;
        for(unsigned k=0;k<5;++k) mutated[random()%mutated.size()]=uint8_t(random());
        if(wire::DecodeFrame(mutated.data(),mutated.size(),zone,parsed,m,lease))
            Check(parsed->Valid()&&m.Valid(),"accepted malformed frame violates contract");
    }
    std::cout << "RIMG protocol and generic transforms PASS (4000 mutations, 800x600, multi-output, malformed lengths)\n";
}
