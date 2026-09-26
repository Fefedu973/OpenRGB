/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../../FrameSurface/FrameSurface.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char** argv)
{
    if(argc < 2 || argc > 3) { std::cerr << "Usage: read_surface CHANNEL [timeout_ms:1..5000]\n"; return 2; }
    unsigned timeout = 5000;
    try { if(argc == 3) timeout = std::stoul(argv[2]); } catch(...) { return 2; }
    if(!room_surface::ValidChannel(argv[1]) || timeout < 1 || timeout > 5000) return 2;
    room_surface::Reader reader(argv[1]);
    room_surface::Frame frame;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while(std::chrono::steady_clock::now() < end)
    {
        if(reader.ReadLatest(frame,2000,5) == room_surface::FrameStatus::NewFrame)
        {
            nlohmann::json output = {{"width",frame.width},{"height",frame.height},{"stride",frame.stride},
                {"sequence",frame.sequence},{"generation",frame.generation},{"bytes",frame.bgra.size()}};
            output["samples"] = nlohmann::json::array();
            for(const auto& p : std::vector<std::pair<unsigned,unsigned>>{{0,0},{frame.width-1,0},{0,frame.height-1},
                    {frame.width-1,frame.height-1},{frame.width/2,frame.height/2}})
            {
                const auto i = std::size_t(p.second)*frame.stride + p.first*4;
                output["samples"].push_back({{"x",p.first},{"y",p.second},
                    {"rgba",{frame.bgra[i+2],frame.bgra[i+1],frame.bgra[i],frame.bgra[i+3]}}});
            }
            std::cout << output.dump() << '\n'; return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "{\"error\":\"No fresh frame before timeout\"}\n"; return 1;
}
