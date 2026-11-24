#include "veyra/engine/EngineController.h"
#include "veyra/engine/FgCompatibilityProbe.h"
#include "veyra/Log.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv){
    using namespace veyra::engine;
    using Clock=std::chrono::steady_clock;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr,executable,32768);
    setFgCompatibilityProbeExecutable(executable);
    if(argc==3&&std::wstring_view(argv[1])==L"--fg-compat-probe")
        return runFgCompatibilityProbe(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
    if(argc!=7&&argc!=8&&argc!=9)return 2; // optional target ratio, then live-1440p profile
    const bool xessProfile=argc==9&&std::wstring_view(argv[8])==L"file-xess-4k";
    const bool nr900Profile=argc==9&&std::wstring_view(argv[8])==L"file-4k-nr900";
    const bool nr720Profile=argc==9&&std::wstring_view(argv[8])==L"file-4k-nr720";
    const bool fileProfile=argc==9&&(std::wstring_view(argv[8])==L"file-4k"||xessProfile||nr900Profile||nr720Profile);
    const bool liveProfile=argc==9&&(std::wstring_view(argv[8])==L"live-1440p"||fileProfile);
    if(argc==9&&!liveProfile)return 2;
    const unsigned multiplier=_wtoi(argv[3]);
    const int seconds=_wtoi(argv[5]);
    const double minimumRatio=argc>=8?_wtof(argv[7]):0;
    if((multiplier!=2&&multiplier!=4&&multiplier!=6)||seconds<30||seconds>240)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    std::filesystem::create_directories(argv[2]);
    veyra::Logger::instance().openFile((std::filesystem::path(argv[2])/L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    RECT rect{0,0,liveProfile?1795:2191,liveProfile?816:1187};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowExW(0,L"STATIC",L"DLSS sustained regression",WS_OVERLAPPEDWINDOW,
        0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window){CoUninitialize();return 2;}
    ShowWindow(window,SW_SHOWNOACTIVATE);
    bool ok=true;
    {
        EngineController engine;engine.setVolume(0,true);
        EnhancementSettings settings;settings.multiplier=multiplier;
        if(xessProfile)settings.frameGenerationBackend=FrameGenerationBackend::XeSS;
        const std::wstring_view effects=argv[6];
        settings.nr=effects==L"on"||effects==L"nr";
        settings.sr=effects==L"on"||effects==L"sr";
        settings.nrTemporal=GetEnvironmentVariableW(L"VEYRA_TEST_NR_TEMPORAL",nullptr,0)>0;
        // Repeat-pair bypass (EnhancementSettings::fgSkipRepeats) for A/B runs on a
        // recorded 30 fps source in a 60 Hz container.
        {wchar_t skip[8]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_SKIP_REPEATS",skip,8))settings.fgSkipRepeats=_wtoi(skip)>0;}
        {wchar_t quality[16]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_VFG_QUALITY",quality,16))settings.vfgQuality=unsigned(std::clamp(_wtoi(quality),0,5));}
        // The remaining switches reproduce one live capture configuration on real hardware
        // (the console setup: VFG High 2X, NR, DLAA) for same-EXE A/B runs.
        {wchar_t v[16]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_BACKEND",v,16)&&std::wstring_view(v)==L"vfg")settings.frameGenerationBackend=FrameGenerationBackend::Vfg;}
        {wchar_t v[8]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_DLAA",v,8)&&_wtoi(v)>0){settings.sr=false;settings.antiAlias.enabled=true;settings.antiAlias.method=AntiAliasMethod::Dlaa;}}
        {wchar_t v[8]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_VFG_AUTO_REGULATE",v,8))settings.vfgAutoRegulate=_wtoi(v)>0;}
        {wchar_t v[8]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_LOW_LATENCY",v,8))settings.fgLowLatencyPresent=_wtoi(v)>0;}
        // The console setup's presentation (low queue, tearing, NVIDIA low latency, no
        // output cap), so a live run sees the same fast-model and present path as the app.
        {wchar_t v[8]{};if(GetEnvironmentVariableW(L"VEYRA_TEST_PRESENTATION",v,8)&&wcscmp(v,L"user")==0){
            PresentationSettings presentation;presentation.enabled=true;presentation.display=DisplaySync::Tearing;presentation.nvidiaLowLatency=NvidiaLowLatency::On;
            engine.requestPresentation(presentation);}}
        veyra::log::info("test",std::string("backend=")+std::string(frameGenerationBackendName(settings.frameGenerationBackend))+" nr="+(settings.nr?"on":"off")+" sr="+(settings.sr?"on":"off")+" dlaa="+(settings.antiAlias.enabled&&settings.antiAlias.usesDlaa()?"on":"off")+
            " FG skip repeats="+(settings.fgSkipRepeats?"on":"off")+" vfgQuality="+std::to_string(settings.vfgQuality)+" autoRegulate="+(settings.vfgAutoRegulate?"on":"off")+" lowLatencyPresent="+(settings.fgLowLatencyPresent?"on":"off"));
        if(liveProfile){settings.flow=FlowQuality::Performance;settings.videoSrQuality=0;settings.srTarget=veyra::pipeline::SrTarget::Uhd4K;}
        if(nr900Profile)settings.nrPolicy=veyra::pipeline::NrSizePolicy::P900;
        if(nr720Profile)settings.nrPolicy=veyra::pipeline::NrSizePolicy::P720;
        auto options=PlayerOptions::from(settings);
        const std::wstring source=argv[1];
        options.captureReplayForTest=!fileProfile&&!source.starts_with(L"capture:")&&!source.starts_with(L"capture2:");
        engine.open(window,source,options);
        const auto opened=Clock::now();auto started=Clock::time_point{};
        int reported=-1;bool stalled=false,cleared=false;
        uint64_t beforeRecovery=0;double recoveredRate=0;unsigned recoveredSamples=0;
        while(Clock::now()-opened<std::chrono::seconds(seconds+35)){
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto s=engine.snapshot();
            if(s.failed){std::wcerr<<s.status<<L" / "<<s.backendWarning<<std::endl;ok=false;break;}
            if(started==Clock::time_point{}&&s.frames>=10)started=Clock::now();
            if(started!=Clock::time_point{}){
                if(s.previewFgMultiplier!=multiplier){
                    std::cerr<<"Requested multiplier changed during playback: "<<s.previewFgMultiplier<<std::endl;
                    ok=false;break;
                }
                const auto elapsed=int(std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-started).count());
                if(elapsed>=seconds)break;
                if(elapsed>=15&&!stalled&&!liveProfile){SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",L"55");stalled=true;}
                if(elapsed>=17&&!cleared){SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);cleared=true;beforeRecovery=s.generated;}
                if(elapsed!=reported){
                    reported=elapsed;const auto& f=s.metrics.flow;const auto& c=f.counters;
                    std::cout<<"t="<<elapsed<<" multiplier="<<s.applied.multiplier<<" effective="<<s.previewFgMultiplier<<" frames="<<s.frames<<" generated="<<s.generated
                        <<" sourceFps="<<f.sourceCompletedFps<<" submitFps="<<f.presentSubmitFps<<" validFps="<<f.validGeneratedFps
                        <<" providerFps="<<f.xessSdkSubmitFps<<" providerGenerated="<<c.xessSdkGenerated
                        <<" limited="<<s.fgBudgetLimited<<" skipped="<<c.fgSkippedBeforeEval<<" expired="<<c.generatedExpiredAfterEval
                        <<" received="<<c.captureReceived<<" accepted="<<c.sourceAccepted<<" overwritten="<<c.mailboxOverwritten
                        <<" evaluated="<<c.fgEvaluated<<" warmup="<<c.fgWarmup<<" realPresented="<<c.realPresented<<" generatedPresented="<<c.generatedPresented
                        <<" ageP95Ms="<<s.captureAgeP95Ms<<" waitP95Ms="<<s.schedulingWaitP95Ms
                        <<" presentMs="<<s.presentCpuP95Ms<<" slotMs="<<f.slotWaitPerFrameMs.value_or(-1);
                    for(const auto stage:{veyra::diagnostics::GpuStage::Nr,veyra::diagnostics::GpuStage::Sr,veyra::diagnostics::GpuStage::Flow,veyra::diagnostics::GpuStage::FgBatch})
                        std::cout<<" gpu"<<int(stage)<<"="<<f.gpuTiming[size_t(stage)].p95.value_or(-1);
                    std::cout<<std::endl;
                    if(elapsed>=22){recoveredRate+=xessProfile?f.xessSdkSubmitFps:f.presentSubmitFps;++recoveredSamples;}
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        SetEnvironmentVariableW(L"VEYRA_TEST_VIDEO_WORK_MS",nullptr);
        const auto s=engine.snapshot();
        ok=ok&&cleared&&s.fgActive&&s.applied.multiplier==multiplier&&s.generated>beforeRecovery+100;
        const double mean=recoveredSamples?recoveredRate/recoveredSamples:0;
        const bool throughput=minimumRatio<=0||(recoveredSamples&&mean>=s.nominalSourceFps*multiplier*minimumRatio);
        std::cout<<"SUSTAINED lifecycle="<<ok<<" recoveredMeanSubmitFps="<<mean
            <<" rateSource="<<(xessProfile?"xess-provider":"app")
            <<" minimumTargetRatio="<<minimumRatio<<" throughput="<<throughput
            <<" nominalTarget="<<s.nominalSourceFps*multiplier<<" frames="<<s.frames<<" generated="<<s.generated<<std::endl;
        ok=ok&&throughput;
        engine.stop();
        const auto stopping=Clock::now();
        while(!engine.idle()&&Clock::now()-stopping<std::chrono::seconds(15))std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ok=ok&&engine.idle();
    }
    // Dump the bounded existing trace after stopping, without per-frame disk I/O.
    std::ofstream(std::filesystem::path(argv[2])/L"frame-trace.txt")
        <<veyra::Logger::instance().diagnosticReport();
    DestroyWindow(window);CoUninitialize();
    std::cout<<"Submission rates are not scanout rates. Lifecycle recovery does not assert target throughput or image quality.\n";
    return ok?0:1;
}
