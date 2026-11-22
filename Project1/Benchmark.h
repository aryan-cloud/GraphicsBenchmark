#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace DirectX11
{
    enum class RenderMode;
    struct FrameInfo;
    struct PresentationInfo;
}

struct BenchmarkOptions
{
    UINT warmup = 120;
    UINT frames = 600;
    UINT warmupSeconds = 3;
    UINT sampleSeconds = 0;
    UINT sampleCapacity = 1000000;
    bool diagnostics = false;
    bool requireForeground = false;
    std::wstring output = L"benchmark.csv";
};

enum class GpuStatus
{
    Pending,
    Valid,
    RingFull,
    QueryRingDisabled,
    Disjoint,
    InvalidFrequency,
    InvalidTimestamps,
    QueryError,
    Timeout
};

struct WindowObservation
{
    bool foreground = false;
    bool focused = false;
    bool visible = false;
    bool minimized = false;
    UINT focusEvents = 0;
    UINT activationEvents = 0;
    UINT visibilityEvents = 0;
};

struct FrameDiagnostics
{
    UINT frameId = 0;
    UINT drawSubmissions = 0;
    UINT presentInterval = 0;
    UINT presentFlags = 0;
    HRESULT presentResult = S_OK;
    int querySlot = -1;
    int collectedSlot = -1;
    UINT collectedFrameId = 0;
    bool timestampsRead = false;
    UINT64 startTicks = 0;
    UINT64 endTicks = 0;
    UINT64 frequencyHz = 0;
    BOOL disjoint = FALSE;
    WindowObservation before;
    WindowObservation after;
};

struct FrameSample
{
    UINT frameId = 0;
    double cpuSubmitMs = 0.0;
    double presentMs = 0.0;
    double gpuElapsedMs = 0.0;
    GpuStatus gpuStatus = GpuStatus::Pending;
    HRESULT queryResult = S_OK;
    FrameDiagnostics* diagnostics = nullptr;
};

struct BenchmarkRun
{
    UINT warmupCompleted = 0;
    UINT framesRecorded = 0;
    LONGLONG qpcFrequency = 0;
    LONGLONG warmupWallTicks = 0;
    LONGLONG sampleWallTicks = 0;
    // A null reason means the configured measurement completed.
    const char* incompleteReason = nullptr;
    HRESULT result = S_OK;
};

bool WriteBenchmarkResults(const BenchmarkOptions& options, const BenchmarkRun& run,
    const DirectX11::FrameInfo& frameInfo, UINT cubeCount, DirectX11::RenderMode mode,
    const std::vector<FrameSample>& samples, const DirectX11::PresentationInfo& presentation);
bool WriteBenchmarkDiagnostics(const BenchmarkOptions& options, UINT attempted,
    const std::vector<FrameDiagnostics>& diagnostics, const std::vector<FrameSample>& samples);
