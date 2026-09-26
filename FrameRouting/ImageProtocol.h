/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBControllerImageInterface.h"
#include <cstring>
#include <stdexcept>

// Room SDK 7 image extension, schema 1. Explicit little-endian encoding,
// independent of struct padding and the legacy 16-bit LED/matrix descriptions.
namespace room_image::wire
{
constexpr uint32_t Magic = 0x474d4952; // ASCII RIMG
constexpr uint32_t Schema = 1;
constexpr size_t FrameHeaderBytes = 100;
constexpr size_t MaxPacketBytes = MaxFrameBytes + FrameHeaderBytes;
constexpr uint32_t MaxOutputs = 4096;
constexpr uint32_t PixelFormatBGRA8 = 1;

inline void U32(std::vector<uint8_t>& b, uint32_t n)
{ for(unsigned i=0;i<4;++i) b.push_back(uint8_t(n>>(8*i))); }
inline void U64(std::vector<uint8_t>& b, uint64_t n)
{ for(unsigned i=0;i<8;++i) b.push_back(uint8_t(n>>(8*i))); }
inline void F64(std::vector<uint8_t>& b, double n)
{ static_assert(sizeof(double)==8); uint64_t v; std::memcpy(&v,&n,8); U64(b,v); }

class Reader
{
public:
    Reader(const uint8_t* data, size_t length) : p(data), n(length) {}
    uint32_t U32() { uint32_t v=0; for(unsigned i=0;i<4;++i) v |= uint32_t(Byte())<<(8*i); return v; }
    uint64_t U64() { uint64_t v=0; for(unsigned i=0;i<8;++i) v |= uint64_t(Byte())<<(8*i); return v; }
    double F64() { auto v=U64(); double d; std::memcpy(&d,&v,8); return d; }
    size_t Remaining() const { return n; }
    const uint8_t* Data() const { return p; }
private:
    uint8_t Byte() { if(!p || !n) throw std::runtime_error("Truncated image packet"); --n; return *p++; }
    const uint8_t* p; size_t n;
};

inline std::vector<uint8_t> EncodeOutputs(const std::vector<Output>& outputs)
{
    if(outputs.size()>MaxOutputs) throw std::invalid_argument("Too many image outputs");
    std::vector<uint8_t> b;
    U32(b,Magic); U32(b,Schema); U32(b,uint32_t(outputs.size()));
    for(const auto& o:outputs) { U32(b,o.zone); U32(b,o.width); U32(b,o.height); U32(b,o.max_fps); }
    return b;
}
inline bool DecodeOutputs(const uint8_t* p,size_t n,std::vector<Output>& outputs)
{
    outputs.clear();
    try
    {
        Reader r(p,n);
        if(r.U32()!=Magic || r.U32()!=Schema) return false;
        const auto count=r.U32();
        if(count>MaxOutputs || r.Remaining()!=size_t(count)*16) return false;
        std::vector<Output> parsed;
        for(uint32_t i=0;i<count;++i)
        {
            Output o; o.zone=r.U32(); o.width=r.U32(); o.height=r.U32(); o.max_fps=r.U32();
            if(!o.width || !o.height || o.width>16384 || o.height>16384 || o.max_fps>1000) return false;
            for(const auto& previous:parsed) if(previous.zone==o.zone) return false;
            parsed.push_back(o);
        }
        outputs=std::move(parsed); return true;
    }
    catch(const std::exception&) { return false; }
}

inline std::vector<uint8_t> EncodeFrame(unsigned zone,const Frame& f,const Mapping& m,unsigned lease_ms)
{
    if(!f.Valid() || !m.Valid() || lease_ms<100 || lease_ms>5000)
        throw std::invalid_argument("Invalid image frame");
    const auto bytes=uint32_t(uint64_t(f.stride)*f.height);
    std::vector<uint8_t> b; b.reserve(FrameHeaderBytes+bytes);
    U32(b,Magic); U32(b,Schema); U32(b,zone); U32(b,f.width); U32(b,f.height);
    U32(b,f.stride); U32(b,PixelFormatBGRA8); U32(b,lease_ms); U64(b,f.sequence);
    F64(b,m.origin_x); F64(b,m.origin_y); F64(b,m.u_x); F64(b,m.u_y);
    F64(b,m.v_x); F64(b,m.v_y); F64(b,m.brightness); U32(b,bytes);
    b.insert(b.end(),f.pixels->begin(),f.pixels->begin()+bytes); return b;
}
inline bool DecodeFrame(const uint8_t* p,size_t n,unsigned& zone,
                        std::shared_ptr<const Frame>& frame,Mapping& m,unsigned& lease_ms)
{
    frame.reset();
    if(n<FrameHeaderBytes || n>MaxPacketBytes) return false;
    try
    {
        Reader r(p,n); Frame f;
        if(r.U32()!=Magic || r.U32()!=Schema) return false;
        zone=r.U32(); f.width=r.U32(); f.height=r.U32(); f.stride=r.U32();
        if(r.U32()!=PixelFormatBGRA8) return false;
        lease_ms=r.U32(); f.sequence=r.U64();
        m.origin_x=r.F64(); m.origin_y=r.F64(); m.u_x=r.F64(); m.u_y=r.F64();
        m.v_x=r.F64(); m.v_y=r.F64(); m.brightness=r.F64();
        const auto bytes=r.U32();
        if(!m.Valid() || lease_ms<100 || lease_ms>5000 || !f.width || !f.height ||
           f.width>16384 || f.height>16384 || uint64_t(f.width)*4>f.stride ||
           uint64_t(f.stride)*f.height!=bytes || bytes>MaxFrameBytes || bytes!=r.Remaining()) return false;
        f.pixels=std::make_shared<const std::vector<uint8_t>>(r.Data(),r.Data()+bytes);
        frame=std::make_shared<const Frame>(std::move(f)); return true;
    }
    catch(const std::exception&) { return false; }
}
}
