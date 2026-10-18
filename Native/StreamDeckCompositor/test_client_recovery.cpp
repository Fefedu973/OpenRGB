// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../Controllers/StreamDeckBackgroundController/StreamDeckNativeClient.h"
#include "../../Controllers/StreamDeckBackgroundController/StreamDeckBackgroundController.h"
#include "RoomStreamDeckNative.h"
#include <atomic>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using Client=streamdeck_background::NativeClient;
using namespace std::chrono_literals;
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
  const auto bad_close=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"fixture_close_failure"));
  const auto open_fail=reinterpret_cast<void(*)(int,int)>(GetProcAddress(dll,"fixture_open_failure"));
  Check(reset&&fail&&count&&pid&&stalled&&bad_close&&open_fail,"fixture exports");
  Client::Clock::time_point now(100s);
  const auto clock=[&]{return now;};
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);
   auto status=client.Request("/health","");
   Check(status["ready"]==true&&status["cached"]==false&&status["ageMs"]==0,"fresh healthy sample");
   const auto sampled=status["sampledSteadyMs"];
   stalled(1);
   Check(!client.Request("/health","").value("ready",true),"queued native stall remains a normal health result");
   Check(!client.Request("/frame","q").value("accepted",true),"queued native stall refuses frame without transport fault");
   Check(count(0)==1&&count(1)==0&&count(2)==1,"native stall retains original handle");
   stalled(0);client.Request("/health","");
   fail(2,1,5);Failure([&]{client.Request("/frame","old");});
   Check(count(5)==3,"timeout does not immediately issue a second blocking RPC");
   now+=500ms;status=client.LastDiagnostics();
   Check(status["transportState"]=="recovering"&&status["ready"]==false&&status["lastKnownReady"]==true,"old ready is not current transport health");
   Check(status["cached"]==true&&status["stale"]==true&&status["sampledSteadyMs"]==sampled&&status["ageMs"]==500,"cached age increases without rejuvenating sample");
   Failure([&]{client.Request("/health","");});
   Check(count(5)==3,"early health probe does no native call");
   const auto frames=count(3);
   now+=500ms;
   for(const auto delay:{2000,4000,8000,10000,10000})
   {
    Failure([&]{client.Request("/frame","stale");});
    Failure([&]{client.Request("/layout","");});
    Check(count(3)==frames&&count(6)==0,"quarantine never forwards prepared frame or layout");
    Failure([&]{client.Request("/health","");});
    status=client.LastDiagnostics();
    Check(status["retryAfterMs"]==delay&&status["sampledSteadyMs"]==sampled,"bounded exponential backoff preserves real timestamp");
    Check(count(0)==1&&count(1)==0&&count(2)==1,"prolonged timeout neither closes nor reinjects");
    now+=std::chrono::milliseconds(delay);
   }
   fail(0,0,0);stalled(1);
   status=client.Request("/health","");
   Check(status["transportState"]=="recovering"&&status["ready"]==false&&status["cached"]==false,"responding but not-ready stays in quarantine");
   Check(status["sampledSteadyMs"]!=sampled&&status["ageMs"]==0,"only a genuine status advances sample time");
   now+=10s;stalled(0);status=client.Request("/health","");
   Check(status["ready"]==true&&status["transportState"]=="active"&&status["transportError"]==""&&!status["stale"].get<bool>(),"fresh ready status resumes same session");
   Check(count(3)==frames,"recovery never replays failed or quarantined frames");
   Check(client.Request("/frame","fresh").value("accepted",false)&&count(8)=='f',"only caller newly selected frame is submitted");
   Check(count(0)==1&&count(1)==0&&count(2)==1,"one original handle through long recovery");
   Check(client.Close().empty()&&count(1)==1&&count(2)==0,"normal close remains available");
  }
  // Both old ABI and structured timeouts are conservative; no frame retry.
  for(int type:{1,2})
  {
   reset();Client client(path,"C:/synthetic",20,clock);
   for(unsigned i=0;i<4;++i)
   {
    client.Request("/health","");fail(type,1,0);
    Failure([&]{client.Request("/frame","old");});
    now+=1s;Check(client.Request("/health","").value("ready",false),"isolated timeout recovers via scheduled status");
    client.Request("/frame","fresh");
   }
   Check(count(0)==1&&count(1)==0&&count(3)==8,"repeated isolated delays never accumulate permanent PID latch");
  }
  // Stop does not wait for the recovery schedule or perform a health probe.
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);fail(2,1,5);
   Failure([&]{client.Request("/frame","old");});
   const auto health=count(5);Failure([&]{client.Request("/stop","");});
   const auto started=Client::Clock::now();Check(client.Close().empty(),"close in recovery succeeds");
   Check(Client::Clock::now()-started<1s&&count(5)==health&&count(1)==1&&count(2)==0,"close bypasses backoff without extra RPC");
  }
  // A status timeout (rather than a frame timeout) follows the same policy.
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);fail(0,0,5);
   Failure([&]{client.Request("/health","");});
   Check(client.LastDiagnostics()["sampledSteadyMs"].is_null(),"no fabricated timestamp before first response");
   Check(count(5)==1&&count(1)==0,"initial status timeout retains handle");
   now+=1s;fail(0,0,0);Check(client.Request("/health","").value("ready",false),"initial status can recover");
  }
  // Explicit detach, native error, malformed health or missing required fields
  // stay fail-closed; a plain timeout is deliberately absent from this list.
  for(int scenario:{2,3,4,6})
  {
   reset();Client client(path,"C:/synthetic",20,clock);client.Request("/health","");
   fail(0,0,scenario);Failure([&]{client.Request("/health","");});
   Check(count(1)==1&&count(2)==0&&client.LastDiagnostics()["transportState"]=="faulted","invalid or explicitly faulted health closes");
   Check(Failure([&]{client.Request("/health","");}).find("new Elgato process")!=std::string::npos,"true fault latches PID");
   Check(count(0)==1,"fault cannot create second hook");
   pid(11);fail(0,0,0);Check(client.Request("/health","").value("ready",false),"new PID remains valid recovery");
  }
  for(int type:{3,4})
  {
   reset();Client client(path,"C:/synthetic",20,clock);fail(type,1,0);
   Failure([&]{client.Request("/frame","bad");});
   Check(count(1)==1&&client.LastDiagnostics()["transportState"]=="faulted","explicit detached/native fault is not a timeout");
  }
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);client.Request("/health","");bad_close(1);
   Check(!client.Close().empty(),"cleanup failure is retained");
   Failure([&]{client.Request("/health","");});Check(count(0)==1,"ambiguous cleanup cannot reattach same PID");
  }
  // Only the typed ABI result certifying cleanup allows another initial open.
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);open_fail(ROOM_SD_RETRY_OPEN,6);
   unsigned attempts=0;
   for(const auto delay:{1,2,4,8,10,10})
   {
    const auto reason=Failure([&]{client.Request("/health","");});++attempts;
    const auto status=client.LastDiagnostics();
    Check(status["transportState"]=="recovering"&&status["retryKind"]=="open"&&status["openAttempts"]==attempts,"open retry has distinct diagnostics");
    Check(status["sampledSteadyMs"].is_null()&&!status["lastKnownReady"].get<bool>()&&status["retryAfterMs"]==delay*1000,"open retry never invents health and caps its delay");
    now+=500ms;
    Check(Failure([&]{client.Request("/health","");})==reason,"deadline wait keeps the same logged cause");
    Check(count(9)==static_cast<int>(attempts)&&count(0)==0&&count(2)==0&&count(5)==0,"no open or health before retry deadline");
    now+=std::chrono::milliseconds(delay*1000-500);
   }
   const auto status=client.Request("/health","");
   Check(status["ready"]==true&&status["retryKind"]=="none"&&status["openAttempts"]==7,"same PID opens after confirmed-cleanup retries");
   Check(count(9)==7&&count(0)==1&&count(2)==1&&count(3)==0,"retry success creates only one live handle without a frame replay");
  }
  // A new process bypasses only the old process's backoff, with no cached sample.
  reset();
  {
   Client client(path,"C:/synthetic",20,clock);client.Request("/health","");
   fail(3,1,0);Failure([&]{client.Request("/frame","old");});
   pid(11);open_fail(ROOM_SD_RETRY_OPEN,3);
   Failure([&]{client.Request("/health","");});
   auto status=client.LastDiagnostics();
   Check(status["sampledSteadyMs"].is_null()&&status["ageMs"].is_null()&&!status["lastKnownReady"].get<bool>()&&!status.contains("errors"),"new PID cannot retain previous health or fault counters");
   now+=1s;Failure([&]{client.Request("/health","");});
   Check(client.LastDiagnostics()["retryAfterMs"]==2000,"second same-PID retry backs off");
   pid(12);Failure([&]{client.Request("/health","");});
   status=client.LastDiagnostics();
   Check(status["openAttempts"]==1&&status["retryAfterMs"]==1000,"new PID resets pending open backoff immediately");
   now+=1s;Check(client.Request("/health","").value("ready",false),"new PID recovers after its own deadline");
  }
  for(int code:{ROOM_SD_GUARD,ROOM_SD_ATTACH,ROOM_SD_RPC})
  {
   reset();Client client(path,"C:/synthetic",20,clock);open_fail(code,1);
   Failure([&]{client.Request("/health","");});now+=30s;
   Failure([&]{client.Request("/health","");});
   Check(count(9)==1&&count(2)==0&&client.LastDiagnostics()["transportState"]=="faulted","cancelled text alone never retries guard or ambiguous attachment failure");
   pid(11);Check(client.Request("/health","").value("ready",false),"a different PID is allowed after a true initial fault");
  }
  // Real worker + fake ABI: SDK's more detailed cause must not defeat log dedup.
  // Configure fixture only before starting, and inspect its counters after join.
  reset();open_fail(ROOM_SD_ATTACH,1);
  {
   streamdeck_background::Options options;options.transport="native";options.native_library=path;options.native_lock_directory="C:/synthetic";
   std::atomic<unsigned> reports{0};
   streamdeck_background::Controller controller(options,[&](const std::string&){++reports;});
   controller.Submit(std::vector<unsigned char>(streamdeck_background::LED_COUNT*3,127));
   const auto deadline=Client::Clock::now()+6s;
   streamdeck_background::Status status;
   do {
    status=controller.GetStatus();
    if(status.failures>=4&&status.state=="faulted")break;
    std::this_thread::sleep_for(5ms);
   }while(Client::Clock::now()<deadline);
   controller.Stop();
   Check(status.failures>=4&&status.state=="faulted","real controller ran repeated fatal-PID checks");
   Check(reports.load()==2&&count(9)==1,"initial error and latched cause logged once each despite status rewrites");
  }
  Check(count(2)==0,"final handle closed");FreeLibrary(dll);
  std::cout<<"PASS "<<checks<<" native-client recovery checks; fake ABI and clock only, no target process\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
