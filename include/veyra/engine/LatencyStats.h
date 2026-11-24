#pragma once
namespace veyra::engine {
// Live latency chain, published as plain atomics so the interface can show the
// same numbers the logs print without threading them through the engine
// snapshot (pattern: source::captureDriverAgeP50Ms).
//
// captureToPresent: capture callback arrival -> Present() call returned
//   (engine side: read, graph submit, present). Measured over the last ~120
//   presented capture frames.
// displayScanout: Present call -> the vblank that showed it (DXGI frame
//   statistics, PresentSink). Same window VideoPresenter logs as
//   display-latency.
void publishCaptureToPresent(double p50Ms, double p95Ms);
double captureToPresentP50Ms();
double captureToPresentP95Ms();
void publishDisplayScanout(double p50Ms, double p95Ms);
double displayScanoutP50Ms();
double displayScanoutP95Ms();
}
