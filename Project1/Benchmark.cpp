#include "Benchmark.h"
#include "DirectX11/Renderer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>

namespace
{
    const char* StatusName(GpuStatus status)
    {
        switch (status)
        {
        case GpuStatus::Pending: return "unresolved";
        case GpuStatus::Valid: return "valid";
        case GpuStatus::RingFull: return "skipped_ring_full";
        case GpuStatus::QueryRingDisabled: return "skipped_query_ring_disabled";
        case GpuStatus::Disjoint: return "invalid_disjoint";
        case GpuStatus::InvalidFrequency: return "invalid_frequency";
        case GpuStatus::InvalidTimestamps: return "invalid_timestamps";
        case GpuStatus::QueryError: return "invalid_query_error";
        case GpuStatus::Timeout: return "unresolved_timeout";
        }
        return "unknown";
    }

    void WriteQuantiles(std::ostream& output, const char* metric, std::vector<double>& values)
    {
        output << metric << "_count=" << values.size() << '\n';
        output << metric << "_median=";
        if (values.empty())
        {
            output << "unavailable\n" << metric << "_p95=unavailable\n";
            return;
        }

        std::sort(values.begin(), values.end());
        const std::size_t count = values.size();
        const double median = (values[(count - 1) / 2] + values[count / 2]) * 0.5;
        const std::size_t p95Rank = (95 * count + 99) / 100;
        output << median << '\n' << metric << "_p95=" << values[p95Rank - 1] << '\n';
    }

    void WriteWindowObservation(std::ostream& output, const WindowObservation& window)
    {
        output << window.foreground << ',' << window.focused << ',' << window.visible << ','
            << window.minimized << ',' << window.focusEvents << ',' << window.activationEvents << ','
            << window.visibilityEvents;
    }
}

bool WriteBenchmarkResults(const BenchmarkOptions& options, const BenchmarkRun& run,
    const DirectX11::FrameInfo& frameInfo, UINT cubeCount, DirectX11::RenderMode mode,
    const std::vector<FrameSample>& samples, const DirectX11::PresentationInfo& presentation)
{
    std::ofstream csv(options.output.c_str());
    if (!csv)
        return false;
    csv.imbue(std::locale::classic());
    csv << std::fixed << std::setprecision(6);
    csv << "frame_id,cpu_submit_ms,present_ms,gpu_elapsed_ms,gpu_status\n";

    UINT valid = 0, skipped = 0, invalid = 0, unresolved = 0;
    UINT queryErrors = 0;
    HRESULT firstQueryError = S_OK;
    for (UINT index = 0; index < run.framesRecorded; ++index)
    {
        const auto& sample = samples[index];
        csv << sample.frameId << ',' << sample.cpuSubmitMs << ',' << sample.presentMs << ',';
        if (sample.gpuStatus == GpuStatus::Valid)
            csv << sample.gpuElapsedMs;
        csv << ',' << StatusName(sample.gpuStatus) << '\n';

        switch (sample.gpuStatus)
        {
        case GpuStatus::Valid:
            ++valid;
            break;
        case GpuStatus::RingFull:
        case GpuStatus::QueryRingDisabled:
            ++skipped;
            break;
        case GpuStatus::Disjoint:
        case GpuStatus::InvalidFrequency:
        case GpuStatus::InvalidTimestamps:
            ++invalid;
            break;
        case GpuStatus::QueryError:
            ++invalid;
            if (queryErrors++ == 0)
                firstQueryError = sample.queryResult;
            break;
        case GpuStatus::Pending:
        case GpuStatus::Timeout:
            ++unresolved;
            break;
        }
    }
    csv.close();
    if (!csv)
        return false;

    const std::wstring summaryPath = options.output + L".summary.txt";
    std::ofstream summary(summaryPath.c_str());
    if (!summary)
        return false;
    summary.imbue(std::locale::classic());
    summary << std::fixed << std::setprecision(6);

    char adapter[ARRAYSIZE(frameInfo.adapter) * 4]{};
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, frameInfo.adapter, -1,
        adapter, sizeof(adapter), nullptr, nullptr) == 0)
        return false;
#ifdef _DEBUG
    constexpr char build[] = "Debug";
#else
    constexpr char build[] = "Release";
#endif
    summary << "completed=" << (run.incompleteReason == nullptr ? "true" : "false") << '\n';
    if (run.incompleteReason != nullptr)
        summary << "incomplete_reason=" << run.incompleteReason << '\n';
    if (run.result != S_OK)
        summary << "run_hresult=0x" << std::hex << static_cast<unsigned long>(run.result) << std::dec << '\n';
    summary << "adapter=" << adapter << '\n'
        << "build_configuration=" << build << '\n'
        << "buffer_width=" << frameInfo.width << '\n'
        << "buffer_height=" << frameInfo.height << '\n'
        << "cube_count=" << cubeCount << '\n'
        << "rendering_mode=" << (mode == DirectX11::RenderMode::Instanced ? "instanced" : "separate_draw") << '\n'
        << "vsync=" << presentation.presentInterval << '\n'
        << "present_sync_interval=" << presentation.presentInterval << '\n'
        << "present_flags=" << presentation.presentFlags << '\n'
        << "swap_chain_flags=" << presentation.swapChainFlags << '\n'
        << "swap_chain_desc_hresult=0x" << std::hex
        << static_cast<unsigned long>(presentation.descriptionResult) << std::dec << '\n'
        << "tearing_check_hresult=0x" << std::hex
        << static_cast<unsigned long>(presentation.tearingCheckResult) << std::dec << '\n'
        << "tearing_supported=" << (presentation.tearingSupported ? "true" : "false") << '\n';
    if (options.sampleSeconds != 0)
    {
        summary << "benchmark_mode=duration\n"
            << "warmup_seconds_requested=" << options.warmupSeconds << '\n'
            << "sample_seconds_requested=" << options.sampleSeconds << '\n';
    }
    else
    {
        summary << "benchmark_mode=frames\n"
            << "warmup_requested=" << options.warmup << '\n'
            << "samples_requested=" << options.frames << '\n';
    }

    summary << "sample_capacity=" << samples.size() << '\n'
        << "warmup_completed=" << run.warmupCompleted << '\n'
        << "warmup_wall_ms=" << (1000.0 * run.warmupWallTicks / run.qpcFrequency) << '\n'
        << "samples_recorded=" << run.framesRecorded << '\n'
        << "sample_wall_qpc_ticks=" << run.sampleWallTicks << '\n'
        << "qpc_frequency_hz=" << run.qpcFrequency << '\n'
        << "sample_wall_ms=" << (1000.0 * run.sampleWallTicks / run.qpcFrequency) << '\n'
        << "diagnostics=" << (options.diagnostics ? "true" : "false") << '\n'
        << "submission_counting=" << (options.diagnostics ? "true" : "false") << '\n'
        << "window_observations=" << (options.diagnostics ? "true" : "false") << '\n'
        << "foreground_guard=" << (options.requireForeground ? "true" : "false") << '\n';
    summary << "observed_loop_fps=";
    if (run.sampleWallTicks > 0 && run.framesRecorded != 0)
        summary << (static_cast<double>(run.qpcFrequency) * run.framesRecorded / run.sampleWallTicks) << '\n';
    else
        summary << "unavailable\n";
    summary << "gpu_valid=" << valid << '\n'
        << "gpu_skipped=" << skipped << '\n'
        << "gpu_invalid=" << invalid << '\n'
        << "gpu_unresolved=" << unresolved << '\n'
        << "gpu_query_errors=" << queryErrors << '\n';
    if (queryErrors != 0)
        summary << "first_gpu_query_error_hresult=0x" << std::hex
            << static_cast<unsigned long>(firstQueryError) << std::dec << '\n';
    if (presentation.descriptionResult == S_OK)
    {
        summary << "swap_effect=" << presentation.swapEffect << '\n'
            << "swap_chain_buffer_count=" << presentation.bufferCount << '\n';
    }
    if (options.diagnostics)
    {
        summary << "diagnostics_scope=Counts draw calls in the CPU submission interval. "
               "Window observations are outside that interval and Present.\n";
    }

    std::vector<double> values;
    values.reserve(run.framesRecorded);
    for (UINT index = 0; index < run.framesRecorded; ++index)
        values.push_back(samples[index].cpuSubmitMs);
    WriteQuantiles(summary, "cpu_submit_ms", values);
    values.clear();
    for (UINT index = 0; index < run.framesRecorded; ++index)
        values.push_back(samples[index].presentMs);
    WriteQuantiles(summary, "present_ms", values);
    values.clear();
    for (UINT index = 0; index < run.framesRecorded; ++index)
    {
        if (samples[index].gpuStatus == GpuStatus::Valid)
            values.push_back(samples[index].gpuElapsedMs);
    }
    WriteQuantiles(summary, "gpu_elapsed_ms", values);

    summary
        << "cpu_submit_scope=QPC after optional GPU start marker and before target lookup and binding "
           "through the final draw and the timing branch before the ending QPC. "
           "Includes both clears, all submitted draws, separate-mode constant-buffer updates, QPC boundary overhead "
           "and any driver stalls. Excludes marker API calls, query collection and Present.\n"
        << "present_scope=QPC immediately before and after Present(present_sync_interval, present_flags), "
           "including QPC boundary overhead.\n"
        << "gpu_elapsed_scope=Elapsed GPU time from the timestamp before target binding and both clears "
           "to the timestamp after the final draw submission and before Present. "
           "The interval may include idle time and does not measure utilization.\n"
        << "sample_wall_scope=QPC elapsed time from just before the first sampled frame through the final "
           "message and completion check, before final query collection.\n"
        << "percentiles=Median averages the middle pair for even counts. P95 uses nearest rank.\n"
        << "overlap=CPU and GPU intervals can overlap. Their relative durations alone do not identify a bottleneck.\n";
    summary.close();
    return static_cast<bool>(summary);
}

bool WriteBenchmarkDiagnostics(const BenchmarkOptions& options, UINT attempted,
    const std::vector<FrameDiagnostics>& diagnostics, const std::vector<FrameSample>& samples)
{
    const std::wstring path = options.output + L".diagnostics.csv";
    std::ofstream csv(path.c_str());
    if (!csv)
        return false;
    csv.imbue(std::locale::classic());
    csv << "frame_id,phase,draw_submissions,present_sync_interval,present_flags,present_hresult,"
        "query_slot,collected_slot,collected_frame_id,gpu_start_ticks,gpu_end_ticks,gpu_frequency_hz,"
        "gpu_disjoint,gpu_status,gpu_query_hresult,"
        "foreground_before,focused_before,visible_before,minimized_before,"
        "focus_events_before,activation_events_before,visibility_events_before,"
        "foreground_after,focused_after,visible_after,minimized_after,"
        "focus_events_after,activation_events_after,visibility_events_after\n";
    for (UINT index = 0; index < attempted; ++index)
    {
        const auto& frame = diagnostics[index];
        const bool warmup = index < options.warmup;
        csv << frame.frameId << ',' << (warmup ? "warmup" : "sample") << ','
            << frame.drawSubmissions << ',' << frame.presentInterval << ',' << frame.presentFlags
            << ",0x" << std::hex << static_cast<unsigned long>(frame.presentResult) << std::dec << ',';
        if (frame.querySlot >= 0)
            csv << frame.querySlot;
        csv << ',';
        if (frame.collectedSlot >= 0)
            csv << frame.collectedSlot << ',' << frame.collectedFrameId;
        else
            csv << ',';
        csv << ',';
        if (frame.timestampsRead)
        {
            csv << frame.startTicks << ',' << frame.endTicks << ',' << frame.frequencyHz
                << ',' << (frame.disjoint != FALSE);
        }
        else
            csv << ",,,";
        if (warmup)
            csv << ",warmup,";
        else
        {
            const auto& sample = samples[index - options.warmup];
            csv << ',' << StatusName(sample.gpuStatus) << ',';
            if (frame.collectedSlot >= 0)
                csv << "0x" << std::hex << static_cast<unsigned long>(sample.queryResult) << std::dec;
        }
        csv << ',';
        WriteWindowObservation(csv, frame.before);
        csv << ',';
        WriteWindowObservation(csv, frame.after);
        csv << '\n';
    }
    csv.close();
    return static_cast<bool>(csv);
}
