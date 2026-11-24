#include "veyra/engine/WindowCaptureFeeder.h"
#include "veyra/engine/LiveRecorder.h"
#include "veyra/Log.h"
#include "veyra/source/ScreenCaptureSource.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <format>
#include <future>
#include <mutex>
#include <thread>

namespace veyra::engine {
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
namespace {
void check(HRESULT hr,const char* op){
    if(FAILED(hr)){log::error("ui-record",std::format("{} hr=0x{:08X}",op,uint32_t(hr)));winrt::check_hresult(hr);}
}
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
std::string narrow(const std::wstring& text){
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(size<=1)return {};
    std::string out(size_t(size-1),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,out.data(),size,nullptr,nullptr);
    return out;
}
// Is this monitor presenting HDR (PQ/BT.2020)? The desktop then holds scRGB
// (linear, 1.0 = 80 cd/m2), and an 8-bit capture of it comes out washed out
// with lifted blacks. The feeder captures FP16 there and records HDR.
bool monitorHdr(HMONITOR monitor){
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
    for(UINT a=0;;++a){
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        for(UINT o=0;;++o){
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_OUTPUT_DESC desc{};
            if(FAILED(output->GetDesc(&desc))||desc.Monitor!=monitor)continue;
            Microsoft::WRL::ComPtr<IDXGIOutput6> six;
            DXGI_OUTPUT_DESC1 wide{};
            return SUCCEEDED(output.As(&six))&&SUCCEEDED(six->GetDesc1(&wide))
                &&wide.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
    }
    return false;
}
}

struct WindowCaptureFeeder::Impl {
    std::thread thread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
    mutable std::mutex noteMutex;
    std::wstring note;
    void setNote(const std::wstring& text){std::lock_guard lock(noteMutex);note=text;}
};

WindowCaptureFeeder::WindowCaptureFeeder():p_(std::make_unique<Impl>()){}
WindowCaptureFeeder::~WindowCaptureFeeder(){stop();}

bool WindowCaptureFeeder::start(void* window,LiveRecorder& recorder,unsigned fps,std::wstring& error,
                               const CaptureRegion& region){
    error.clear();
    if(p_->running.load()){error=L"界面录制已在进行中";return false;}
    if(!window||!IsWindow(static_cast<HWND>(window))){error=L"录制界面需要 Veyra 的主窗口";return false;}
    p_->stopping=false;p_->setNote({});
    auto ready=std::make_shared<std::promise<std::wstring>>();
    auto future=ready->get_future();
    p_->running=true;
    p_->thread=std::thread([this,window,&recorder,fps,ready,region]{
        const auto notify=[ready](const std::wstring& why){
            try{ready->set_value(why);}catch(...){}
        };
        const auto failure=run(window,recorder,fps,notify,region);
        // A loop that died before it could report readiness must not leave
        // start() waiting; a second set_value is a no-op.
        notify(failure.empty()?L"界面录制已停止":failure);
        p_->running=false;
    });
    if(future.wait_for(std::chrono::seconds(8))!=std::future_status::ready){
        p_->stopping=true;
        if(p_->thread.joinable())p_->thread.join();
        p_->running=false;error=L"界面录制启动超时";p_->setNote(error);
        log::error("ui-record","capture thread did not become ready");
        return false;
    }
    const auto failure=future.get();
    if(!failure.empty()){
        if(p_->thread.joinable())p_->thread.join();
        p_->running=false;error=failure;p_->setNote(failure);
        log::error("ui-record",std::format("start refused: {}",narrow(failure)));
        return false;
    }
    log::info("ui-record",std::format("window capture started hwnd={} fpsHint={}",uint64_t(reinterpret_cast<uintptr_t>(window)),fps));
    return true;
}

void WindowCaptureFeeder::stop(){
    p_->stopping=true;
    if(p_->thread.joinable())p_->thread.join();
    p_->running=false;
}

bool WindowCaptureFeeder::running() const { return p_->running.load(); }

std::wstring WindowCaptureFeeder::note() const {
    std::lock_guard lock(p_->noteMutex);
    return p_->note;
}

// The whole capture lives here, on the feeder's own thread. WinRT objects are
// created and released inside this call: the apartment is uninitialised after
// they are gone.
std::wstring WindowCaptureFeeder::run(void* windowHandle,LiveRecorder& recorder,unsigned fps,
                                     const std::function<void(const std::wstring&)>& ready,
                                     const CaptureRegion& region){
    const HWND window=static_cast<HWND>(windowHandle);
    bool apartment=false;
    try{winrt::init_apartment(winrt::apartment_type::multi_threaded);apartment=true;}
    catch(const winrt::hresult_error& e){log::warn("ui-record",std::format("apartment init hr=0x{:08X}",uint32_t(e.code())));}
    std::wstring failure;
    try{
        failure=[&]() -> std::wstring {
            ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
            // Same adapter rule as the screen source: the device must be on the
            // adapter that drives the window, otherwise Windows copies frames
            // between the GPUs on every capture.
            ComPtr<IDXGIAdapter> adapter;
            const uint64_t luidValue=source::displayAdapterLuid(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST));
            if(luidValue){
                LUID luid{};luid.LowPart=uint32_t(luidValue);luid.HighPart=int32_t(luidValue>>32);
                factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));
            }
            if(!adapter)check(factory->EnumAdapters(0,&adapter),"EnumAdapters");
            ComPtr<ID3D11Device> d3d11;ComPtr<ID3D11DeviceContext> context;
            check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&d3d11,nullptr,&context),"D3D11CreateDevice");
            ComPtr<ID3D11Device5> device11;check(d3d11.As(&device11),"D3D11 device5");
            ComPtr<ID3D11DeviceContext4> context4;check(context.As(&context4),"D3D11 context4");
            ComPtr<ID3D12Device> device12;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12)),"D3D12CreateDevice");

            // The window as the desktop composites it - except when it covers
            // its whole monitor. DWM then hands the video to an overlay plane
            // and the window's own surface only changes when the interface
            // does (measured: 3 fps at 4K). Capturing the monitor and cropping
            // to the window keeps the full frame rate and still records just
            // Veyra's picture plus its overlays.
            ComPtr<IDXGIDevice> dxgi;check(d3d11.As(&dxgi),"DXGI device");
            winrt::com_ptr<IInspectable> inspectable;
            check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put()),"CreateDirect3D11DeviceFromDXGIDevice");
            const auto winrtDevice=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
            const auto interop=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
            const bool hdr=monitorHdr(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST));
            struct Capture {
                GraphicsCaptureItem item{nullptr};
                Direct3D11CaptureFramePool pool{nullptr};
                GraphicsCaptureSession session{nullptr};
                bool monitor=false;
                bool hdr=false;
                DXGI_FORMAT format=DXGI_FORMAT_B8G8R8A8_UNORM;
                unsigned width=0,height=0;   // the captured surface
            };
            auto openCapture=[&](bool monitorMode)->std::unique_ptr<Capture>{
                // HDR first where the desktop is HDR; an 8-bit pool is the
                // fallback when Windows refuses the FP16 one.
                for(int attempt=0;attempt<2;++attempt){
                    const bool wantHdr=hdr&&attempt==0;
                    auto c=std::make_unique<Capture>();
                    c->monitor=monitorMode;
                    c->hdr=wantHdr;
                    c->format=wantHdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
                    if(monitorMode){
                        check(interop->CreateForMonitor(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),
                            winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(c->item)),"CreateForMonitor");
                    }else{
                        check(interop->CreateForWindow(window,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(c->item)),"CreateForWindow");
                    }
                    const auto size=c->item.Size();
                    c->width=unsigned(std::max(1,size.Width));c->height=unsigned(std::max(1,size.Height));
                    try{
                        c->pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winrtDevice,
                            wantHdr?DirectXPixelFormat::R16G16B16A16Float:DirectXPixelFormat::B8G8R8A8UIntNormalized,3,size);
                    }catch(const winrt::hresult_error& e){
                        if(wantHdr){log::warn("ui-record",std::format("FP16 capture refused hr=0x{:08X}; using 8-bit",uint32_t(e.code())));continue;}
                        throw;
                    }
                    c->session=c->pool.CreateCaptureSession(c->item);
                    c->session.IsCursorCaptureEnabled(false);
                    try{
                        if(winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent
                                (L"Windows.Graphics.Capture.GraphicsCaptureSession",L"IsBorderRequired")){
                            GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
                            c->session.IsBorderRequired(false);
                        }
                    }catch(const winrt::hresult_error& e){
                        log::warn("ui-record",std::format("capture border kept hr=0x{:08X}",uint32_t(e.code())));
                    }
                    c->session.StartCapture();
                    log::info("ui-record",std::format("capture {} surface={}x{} hdr={}",monitorMode?"monitor":"window",c->width,c->height,c->hdr));
                    return c;
                }
                return nullptr;
            };
            auto coversMonitor=[&]{
                RECT wr{};MONITORINFO mi{sizeof(mi)};
                if(!GetWindowRect(window,&wr)||!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi))return false;
                return wr.left<=mi.rcMonitor.left&&wr.top<=mi.rcMonitor.top&&wr.right>=mi.rcMonitor.right&&wr.bottom>=mi.rcMonitor.bottom;
            };
            // 含界面 records the screen itself, at a constant size: the window
            // changes size when it is maximized or goes fullscreen, and a size
            // change ends the file (MP4 cannot change extent mid-stream). A
            // fixed monitor-sized canvas keeps one recording from the first
            // frame to the manual stop, and it is literally what is on screen.
            auto capture=openCapture(true);
            if(!capture)return std::wstring(L"界面录制无法建立抓屏");
            if(capture->width<16||capture->height<16)return std::wstring(L"窗口太小，无法录制界面");
            ready(L"");

            D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            ComPtr<ID3D12CommandQueue> queue;check(device12->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"CreateCommandQueue");
            ComPtr<ID3D12CommandAllocator> allocator;
            check(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"CreateCommandAllocator");
            ComPtr<ID3D12GraphicsCommandList> list;
            check(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"CreateCommandList");
            check(list->Close(),"Close list");
            ComPtr<ID3D12Fence> doneFence;check(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&doneFence)),"CreateFence");
            Handle doneEvent{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
            if(!doneEvent.value)return std::wstring(L"界面录制无法建立同步事件");
            // The copy on the D3D11 side is signalled through a shared fence so
            // the D3D12 queue waits for it on the GPU instead of the CPU.
            ComPtr<ID3D11Fence> copyFence11;check(device11->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&copyFence11)),"D3D11 fence");
            Handle fenceHandle;
            check(copyFence11->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle.value),"fence shared handle");
            ComPtr<ID3D12Fence> copyFence12;
            check(device12->OpenSharedHandle(fenceHandle.value,IID_PPV_ARGS(&copyFence12)),"Open fence D3D12");

            ComPtr<ID3D11Texture2D> producer;ComPtr<ID3D12Resource> consumer;
            unsigned width=0,height=0;
            auto ensure=[](ID3D11Device* d3d11Device,ID3D12Device* d3d12Device,unsigned w,unsigned h,DXGI_FORMAT format,
                           ComPtr<ID3D11Texture2D>& out,ComPtr<ID3D12Resource>& in)->bool{
                if(out&&in)return true;
                D3D11_TEXTURE2D_DESC td{};
                td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=format;td.SampleDesc.Count=1;
                td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                td.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
                if(FAILED(d3d11Device->CreateTexture2D(&td,nullptr,&out)))return false;
                ComPtr<IDXGIResource1> resource;if(FAILED(out.As(&resource)))return false;
                HANDLE handle=nullptr;
                if(FAILED(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle)))return false;
                const HRESULT hr=d3d12Device->OpenSharedHandle(handle,IID_PPV_ARGS(&in));
                CloseHandle(handle);
                return SUCCEEDED(hr);
            };

            recorder.setPresentRateHint(fps?double(fps):60.0);
            uint64_t copyValue=0,doneValue=0,frames=0,nulls=0;
            while(!p_->stopping.load()){
                if(!recorder.active()){p_->setNote(L"录制已停止");break;}
                if(!IsWindow(window)){p_->setNote(L"窗口已关闭，界面录制已停止");break;}
                auto frame=capture->pool.TryGetNextFrame();
                if(!frame){++nulls;std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
                struct Close{Direct3D11CaptureFrame& f;bool closed=false;~Close(){if(!closed&&f)try{f.Close();}catch(...){}}} close{frame};
                // 区域录制 keeps one rectangle of the monitor (fixed, so the
                // extent never changes); otherwise the whole screen.
                const auto content=frame.ContentSize();
                long cropLeft=0,cropTop=0;
                long cropWidth=content.Width,cropHeight=content.Height;
                bool cropping=false;
                if(region.width>0&&region.height>0){
                    cropLeft=std::clamp<long>(region.x,0,std::max<long>(0,long(content.Width)-16));
                    cropTop=std::clamp<long>(region.y,0,std::max<long>(0,long(content.Height)-16));
                    cropWidth=std::min<long>(region.width,long(content.Width)-cropLeft);
                    cropHeight=std::min<long>(region.height,long(content.Height)-cropTop);
                    cropping=true;
                }
                if(cropWidth<16||cropHeight<16){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
                const unsigned w=unsigned(cropWidth)&~1u,h=unsigned(cropHeight)&~1u;
                if(!producer||!consumer||width!=w||height!=h){
                    producer.Reset();consumer.Reset();
                    if(!ensure(d3d11.Get(),device12.Get(),w,h,capture->format,producer,consumer)){p_->setNote(L"界面录制无法建立共享纹理");break;}
                    width=w;height=h;
                    log::info("ui-record",std::format("picture {}x{}",w,h));
                }
                auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                ComPtr<ID3D11Texture2D> surface;
                if(FAILED(access->GetInterface(IID_PPV_ARGS(&surface))))continue;
                const auto tWait0=std::chrono::steady_clock::now();
                // Do not overwrite the texture the encoder is still reading.
                if(doneValue){
                    doneFence->SetEventOnCompletion(doneValue,doneEvent.value);
                    WaitForSingleObject(doneEvent.value,1000);
                }
                const auto tWait1=std::chrono::steady_clock::now();
                if(cropping){
                    D3D11_BOX box{UINT(cropLeft),UINT(cropTop),0,UINT(cropLeft+w),UINT(cropTop+h),1};
                    context->CopySubresourceRegion(producer.Get(),0,0,0,0,surface.Get(),0,&box);
                }else{
                    context->CopyResource(producer.Get(),surface.Get());
                }
                context4->Signal(copyFence11.Get(),++copyValue);
                // The D3D12 queue waits on this signal; without the flush the
                // command can sit in the D3D11 buffer and stall the encoder.
                context->Flush();
                const auto tCopy1=std::chrono::steady_clock::now();
                close.f.Close();close.closed=true;

                PresentCapture::Frame picture;
                picture.texture=consumer.Get();picture.format=capture->format;
                picture.width=w;picture.height=h;picture.hdrMode=capture->hdr?1:0;picture.ditherStep=0;picture.generated=false;
                allocator->Reset();
                check(list->Reset(allocator.Get(),nullptr),"Reset list");
                const bool recorded=recorder.record(list.Get(),picture);
                list->Close();
                if(recorded)queue->Wait(copyFence12.Get(),copyValue);
                ID3D12CommandList* lists[]={list.Get()};
                queue->ExecuteCommandLists(1,lists);
                queue->Signal(doneFence.Get(),++doneValue);
                if(recorded)recorder.submitted(doneFence.Get(),doneValue);
                if((++frames&0x3F)==0){
                    const auto tEnd=std::chrono::steady_clock::now();
                    const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
                    log::info("ui-record",std::format("captured={} nulls={} waitMs={:.1f} copyMs={:.1f} submitMs={:.1f}",
                        frames,nulls,ms(tWait0,tWait1),ms(tWait1,tCopy1),ms(tCopy1,tEnd)));
                }
            }
            capture->session.Close();capture->pool.Close();
            std::wstring ended;{std::lock_guard lock(p_->noteMutex);ended=p_->note;}
            log::info("ui-record",std::format("window capture ended frames={} note={}",frames,narrow(ended)));
            return std::wstring{};
        }();
    }catch(const winrt::hresult_error& e){
        failure=std::format(L"界面录制失败 0x{:08X}：{}",uint32_t(e.code()),e.message().c_str());
        log::error("ui-record",winrt::to_string(e.message()));
    }catch(const std::exception& e){
        failure=L"界面录制失败：资源分配错误";
        log::error("ui-record",e.what());
    }
    if(apartment)winrt::uninit_apartment();
    return failure;
}

} // namespace veyra::engine
