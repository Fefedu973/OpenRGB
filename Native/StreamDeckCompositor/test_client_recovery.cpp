// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../Controllers/StreamDeckBackgroundController/StreamDeckNativeClient.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> static std::string Failure(F fn)
{try{fn();}catch(const std::exception& e){return e.what();}throw std::runtime_error("expected rejection");}
int main(int argc,char** argv)
{
    try {
        Check(argc==2,"fixture path");const auto path=std::filesystem::absolute(argv[1]).string();
        const auto dll=LoadLibraryA(path.c_str());Check(dll!=nullptr,"load synthetic fixture");
        const auto reset=reinterpret_cast<void(*)()>(GetProcAddress(dll,"fixture_reset"));
        const auto fail=reinterpret_cast<void(*)(int,int,int)>(GetProcAddress(dll,"fixture_failure"));
        const auto count=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"fixture_count"));
        const auto pid=reinterpret_cast<void(*)(unsigned)>(GetProcAddress(dll,"fixture_pid"));
        const auto stalled=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"fixture_stalled"));
        Check(reset&&fail&&count&&pid&&stalled,"fixture exports");
        reset();
        { streamdeck_background::NativeClient client(path,"C:/synthetic",20);
          Check(client.Request("/health","").value("ready",false),"initial native target ready");
          stalled(1);
          for(unsigned i=0;i<5;++i)
          {
              const auto status=client.Request("/health","");
              Check(!status.value("ready",true)&&status.value("errors",-1)==0&&status.value("stalled",false),
                    "queued stall is a nonfatal status, not an RPC exception");
              const auto frame=client.Request("/frame","discarded while native queue waits");
              Check(frame.contains("accepted")&&frame["accepted"]==false&&frame.contains("reason")&&frame["reason"].is_string(),
                    "stalled frame refusal remains a normal result for controller backoff");
          }
          Check(count(0)==1&&count(1)==0&&count(2)==1,"repeated stalls neither close nor attach another observer");
          Check(count(3)==5&&count(4)==0,"refused stalled frames are not accepted or replayed");
          Check(client.LastDiagnostics().value("stalled",false),"last diagnostics preserve actual stalled state");
          stalled(0);
          const auto status=client.Request("/health","");
          Check(status.value("ready",false)&&!status.value("stalled",true),"late native completion returns ready on same PID");
          Check(client.Request("/frame","fresh after native restore").value("accepted",false),"fresh image resumes after stalled state");
          Check(count(0)==1&&count(1)==0&&count(2)==1&&count(4)==1,"resume retains exactly the original handle without stale replay");
          Check(client.Close().empty()&&count(1)==1&&count(2)==0,"recovered session closes once normally"); }
        reset();
        { streamdeck_background::NativeClient client(path,"C:/synthetic",20);
          stalled(1);Check(!client.Request("/health","").value("ready",true),"initial stall can remain pending");
          Check(client.Close().empty()&&count(0)==1&&count(1)==1&&count(2)==0,"stop while stalled still closes original handle exactly once"); }
        reset();
        { streamdeck_background::NativeClient client(path,"C:/synthetic",20);
          stalled(1);client.Request("/health","");
          fail(4,1,2);Failure([&]{client.Request("/frame","real fault during stall");});
          Check(count(0)==1&&count(1)==1&&count(2)==0,"real native fault during stall still closes");
          Check(Failure([&]{client.Request("/health","");}).find("new Elgato process")!=std::string::npos,
                "real native fault during stall still latches the PID"); }
        for(int type:{1,2})
        {
            reset();
            { streamdeck_background::NativeClient client(path,"C:/synthetic",20);
              fail(type,1,0);
              Check(Failure([&]{client.Request("/frame","old");}).find("health recovered")!=std::string::npos,"isolated timeout recovered");
              Check(count(0)==1&&count(1)==0&&count(2)==1,"same handle remains owned; no extra attach");
              Check(count(3)==1,"failed frame is never replayed automatically");
              Check(client.Request("/frame","fresh").value("accepted",false),"next fresh frame succeeds");
              fail(type,1,0);Failure([&]{client.Request("/frame","next");});
              Check(count(1)==0,"successful frame resets recovery bound");
              Check(client.Close().empty()&&count(1)==1,"normal restore and close retained"); }
            Check(count(2)==0,"fixture owns no session after scope");
        }
        for(const auto scenario:{std::pair<int,int>{1,1},{1,2},{1,3},{1,4},{3,0},{4,0}})
        {
            reset();streamdeck_background::NativeClient client(path,"C:/synthetic",20);
            fail(scenario.first,1,scenario.second);Failure([&]{client.Request("/frame","frame");});
            Check(count(0)==1&&count(1)==1&&count(2)==0,"unhealthy/detached/native fault closes exactly once");
            Check(Failure([&]{client.Request("/health","");}).find("new Elgato process")!=std::string::npos,"failed PID stays latched");
            Check(count(0)==1,"latched PID cannot reattach");
        }
        reset();
        { streamdeck_background::NativeClient client(path,"C:/synthetic",20);
          fail(1,3,0);
          Failure([&]{client.Request("/frame","a");});Failure([&]{client.Request("/frame","b");});
          Check(count(1)==0,"two consecutive recoveries bounded");
          Failure([&]{client.Request("/frame","c");});Check(count(1)==1,"third consecutive miss closes");
          Failure([&]{client.Request("/health","");});Check(count(0)==1,"no retry loop after bound");
          pid(11);Check(client.Request("/health","").value("ready",false),"new process remains supported recovery");
          Check(count(0)==2&&count(2)==1,"only one session after new PID"); }
        Check(count(2)==0,"final destructor closes");FreeLibrary(dll);
        std::cout<<"PASS "<<checks<<" native-client recovery checks; fake C ABI only, no target process\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
