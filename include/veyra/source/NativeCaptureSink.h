#pragma once
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <functional>
#include <utility>
#include <vector>
namespace veyra::source {
enum class CaptureAllocatorMode { SinkDefault, Upstream };
// Exact raw connection contract, including subtype and VideoInfo2 field flags.
bool nativeCaptureContractsMatch(const AM_MEDIA_TYPE&,const AM_MEDIA_TYPE&);
// A timestamp-less priming sample is not a fatal driver error. Acknowledge it
// without creating a frame/PTS; timed samples retain the exact driver timestamp.
std::function<HRESULT(IMediaSample*)> timedCaptureDelivery(std::function<HRESULT(double,IMediaSample*)>);
// Zero-copy slot allocator: the input pin hands the driver these preallocated
// buffers instead of the system allocator, so the driver writes each frame
// straight into Veyra's D3D12 upload memory and the capture callback skips its
// 4K P010 memcpy (~1.5 ms). Each slot is one full sample (planes contiguous,
// see CaptureUploadFrame.h); slots must stay valid while the allocator lives.
// usable(data) is consulted before a slot is handed out: the capture source
// refuses the frame the reader currently owns, so the driver can never
// overwrite a picture that is still being displayed.
HRESULT createCaptureSlotAllocator(std::vector<std::pair<uint8_t*,long>> slots,std::function<bool(const uint8_t*)> usable,IMemAllocator** out);
// Native terminal filter: no legacy SampleGrabber orientation/VideoInfo2
// restriction, conversion filter or extra queue. Callback borrows the sample.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Blackmagic compatibility: let the output pin choose its own allocator and
// requirements. The selected allocator is still retained after NotifyAllocator.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&,CaptureAllocatorMode);
// Low-latency variant: also reports desiredVideoBuffers (>0) from
// GetAllocatorRequirements so the sink does not pull the negotiated count back
// up to its stock 3 while the output pin was asked for fewer (AVerMedia GC573
// low-latency mode; DirectShow merges allocator requirements upward).
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&,long desiredVideoBuffers);
// Zero-copy variant: the pin exposes the given slot allocator (created with
// createCaptureSlotAllocator) instead of CLSID_MemoryAllocator, so the driver
// fills Veyra's upload buffers directly.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&,IMemAllocator* slotAllocator);
HRESULT createNativeAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
HRESULT createBitstreamAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Compressed (MJPEG/H.264/HEVC/...) variant: carries the driver payload without
// a pixel-layout contract; decoding happens in our own backend.
HRESULT createCompressedCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Advisory request on the upstream output pin, before ConnectDirect. Failure
// does not invalidate the device; callers retain the compatible connection.
HRESULT suggestCaptureAudioBuffering(IPin*,const WAVEFORMATEX&);
// Advisory video-pin request (N4): suggest the driver's allocator buffer count
// and per-frame byte size before ConnectDirect. Failure is logged and ignored.
HRESULT suggestCaptureVideoBuffering(IPin*,long buffers,long bytes);
// Reads the allocator actually in use on our input pin after ConnectDirect.
HRESULT queryCaptureAllocatorProperties(IPin*,ALLOCATOR_PROPERTIES&);
}
