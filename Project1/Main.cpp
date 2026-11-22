#include "Benchmark.h"
#include "DirectX11/Renderer.h"

#include <windows.h>
#include <dxgitype.h>
#include <shellapi.h>

#include <array>
#include <cstdio>
#include <cwchar>
#include <new>

namespace
{
    constexpr wchar_t WindowClassName[] = L"GraphicsBenchmarkWindow";
    constexpr wchar_t WindowTitle[] = L"Graphics Benchmark";
    constexpr std::array<float, 4> ClearColor{ 0.04f, 0.18f, 0.32f, 1.0f };
    constexpr UINT MaxFrameCount = 1000000;

    struct WindowState
    {
        bool drawable = true;
        bool resizePending = false;
        bool benchmarkActive = false;
        const char* interruption = nullptr;
        UINT focusEvents = 0;
        UINT activationEvents = 0;
        UINT visibilityEvents = 0;
    };

    struct Options
    {
        bool benchmark = false;
        UINT cubeCount = 1;
        UINT vsync = 1;
        DirectX11::RenderMode mode = DirectX11::RenderMode::Separate;
        BenchmarkOptions timing;
    };

    bool ParseUnsigned(const wchar_t* argument, UINT maximum, UINT& value)
    {
        if (*argument == L'\0')
            return false;
        value = 0;
        for (; *argument != L'\0'; ++argument)
        {
            if (*argument < L'0' || *argument > L'9')
                return false;
            value = value * 10 + (*argument - L'0');
            if (value > maximum)
                return false;
        }
        return true;
    }

    bool ParseArguments(int count, LPWSTR* arguments, Options& options)
    {
        enum Argument { Benchmark = 1, Cubes = 2, VSync = 4, Warmup = 8, Frames = 16, Output = 32,
            Diagnostics = 64, Mode = 128 };
        unsigned seen = 0;
        for (int index = 1; index < count; ++index)
        {
            const wchar_t* flag = arguments[index];
            Argument argument;
            if (std::wcscmp(flag, L"--benchmark") == 0)
                argument = Benchmark;
            else if (std::wcscmp(flag, L"--cubes") == 0)
                argument = Cubes;
            else if (std::wcscmp(flag, L"--vsync") == 0)
                argument = VSync;
            else if (std::wcscmp(flag, L"--warmup") == 0)
                argument = Warmup;
            else if (std::wcscmp(flag, L"--frames") == 0)
                argument = Frames;
            else if (std::wcscmp(flag, L"--output") == 0)
                argument = Output;
            else if (std::wcscmp(flag, L"--diagnostics") == 0)
                argument = Diagnostics;
            else if (std::wcscmp(flag, L"--mode") == 0)
                argument = Mode;
            else
                return false;

            if ((seen & argument) != 0)
                return false;
            seen |= argument;
            if (argument == Benchmark)
            {
                options.benchmark = true;
                continue;
            }
            if (argument == Diagnostics)
            {
                options.timing.diagnostics = true;
                continue;
            }
            if (++index == count)
                return false;

            const wchar_t* value = arguments[index];
            switch (argument)
            {
            case Cubes:
                if (!ParseUnsigned(value, 10000, options.cubeCount) ||
                    (options.cubeCount != 1 && options.cubeCount != 100 &&
                        options.cubeCount != 1000 && options.cubeCount != 10000))
                    return false;
                break;
            case VSync:
                if (!ParseUnsigned(value, 1, options.vsync))
                    return false;
                break;
            case Mode:
                if (std::wcscmp(value, L"separate") == 0)
                    options.mode = DirectX11::RenderMode::Separate;
                else if (std::wcscmp(value, L"instanced") == 0)
                    options.mode = DirectX11::RenderMode::Instanced;
                else
                    return false;
                break;
            case Warmup:
                if (!ParseUnsigned(value, MaxFrameCount, options.timing.warmup))
                    return false;
                break;
            case Frames:
                if (!ParseUnsigned(value, MaxFrameCount, options.timing.frames) || options.timing.frames == 0)
                    return false;
                break;
            case Output:
                if (*value == L'\0' || std::wcsncmp(value, L"--", 2) == 0)
                    return false;
                options.timing.output = value;
                break;
            case Benchmark:
            case Diagnostics:
                break;
            }
        }
        if (options.timing.diagnostics && (seen & Output) == 0)
            options.timing.output = L"benchmark-diagnostic.csv";
        return options.benchmark || (seen & (Warmup | Frames | Output | Diagnostics)) == 0;
    }

    bool ParseOptions(Options& options)
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
            return false;
        const bool valid = ParseArguments(argumentCount, arguments, options);
        LocalFree(arguments);
        return valid;
    }

    bool UpdateWindowTitle(HWND window, const DirectX11::FrameInfo& info, UINT cubeCount)
    {
        wchar_t title[256]{};
        swprintf_s(title, L"%ls | %ls | %u %ls | %u x %u", WindowTitle, info.adapter,
            cubeCount, cubeCount == 1 ? L"cube" : L"cubes", info.width, info.height);
        return SetWindowTextW(window, title) != FALSE;
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams)) == 0
                && GetLastError() != ERROR_SUCCESS)
                return FALSE;
            break;
        }
        case WM_SIZE:
        {
            auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (state != nullptr)
            {
                const bool drawable = wParam != SIZE_MINIMIZED && LOWORD(lParam) != 0 && HIWORD(lParam) != 0;
                if (state->drawable != drawable)
                    ++state->visibilityEvents;
                state->drawable = drawable;
                state->resizePending = true;
                if (state->benchmarkActive && state->interruption == nullptr)
                    state->interruption = state->drawable ? "resized" : "minimized_or_zero_client_area";
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ACTIVATEAPP:
        case WM_SHOWWINDOW:
        {
            auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (state != nullptr)
            {
                if (message == WM_ACTIVATEAPP)
                    ++state->activationEvents;
                else if (message == WM_SHOWWINDOW)
                    ++state->visibilityEvents;
                else
                    ++state->focusEvents;
            }
            break;
        }
        case WM_CLOSE:
        {
            auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (state != nullptr && state->benchmarkActive && state->interruption == nullptr)
                state->interruption = "closed";
            // Let the loop release Direct3D resources before the HWND is destroyed.
            PostQuitMessage(0);
            return 0;
        }
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool WriteError(const wchar_t* message)
    {
        OutputDebugStringW(message);
        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        return error != nullptr && error != INVALID_HANDLE_VALUE &&
            std::fwprintf(stderr, L"%ls\n", message) >= 0 && std::fflush(stderr) == 0;
    }

    int ReportFailure(HWND window, const wchar_t* operation, HRESULT result, bool benchmark = false)
    {
        wchar_t message[256]{};
        swprintf_s(message, L"%ls failed (0x%08lX).", operation, static_cast<unsigned long>(result));
        if (benchmark)
            WriteError(message);
        else
            MessageBoxW(window, message, WindowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    bool PumpMessages(int& exitCode)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                exitCode = static_cast<int>(message.wParam);
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return true;
    }

    WindowObservation ObserveWindow(HWND window, const WindowState& state)
    {
        return { GetForegroundWindow() == window, GetFocus() == window,
            IsWindowVisible(window) != FALSE, IsIconic(window) != FALSE,
            state.focusEvents, state.activationEvents, state.visibilityEvents };
    }

    int RunBenchmark(HWND window, WindowState& state, DirectX11::Renderer& renderer, const Options& options)
    {
        const auto& frameInfo = renderer.GetFrameInfo();
        const auto& timing = options.timing;
        std::vector<FrameSample> samples(timing.frames);
        std::vector<FrameDiagnostics> diagnostics;
        if (timing.diagnostics)
            diagnostics.resize(timing.warmup + timing.frames);
        UINT diagnosticFrames = 0;
        BenchmarkRun run;

        // Initial show/size messages establish the workload before warmup starts.
        int exitCode = 0;
        if (!PumpMessages(exitCode))
            run.incompleteReason = "closed";
        else if (!state.drawable)
            run.incompleteReason = "minimized_or_zero_client_area";
        else if (state.resizePending)
        {
            state.resizePending = false;
            run.result = renderer.Resize();
            if (FAILED(run.result))
                run.incompleteReason = "initial_resize_failed";
            else if (!UpdateWindowTitle(window, frameInfo, options.cubeCount))
            {
                run.result = HRESULT_FROM_WIN32(GetLastError());
                run.incompleteReason = "window_title_failed";
            }
        }

        ULONGLONG sampleStart = 0;
        bool sampling = false;
        state.benchmarkActive = true;
        while (run.incompleteReason == nullptr)
        {
            const bool running = PumpMessages(exitCode);
            if (state.interruption != nullptr || !running)
            {
                run.incompleteReason = state.interruption != nullptr ? state.interruption : "closed";
                break;
            }
            if (run.framesRecorded == timing.frames)
                break;

            FrameSample* sample = nullptr;
            if (run.warmupCompleted == timing.warmup)
            {
                if (!sampling)
                {
                    sampleStart = GetTickCount64();
                    sampling = true;
                }
                sample = &samples[run.framesRecorded];
                sample->frameId = run.warmupCompleted + run.framesRecorded + 1;
            }

            FrameDiagnostics* diagnostic = nullptr;
            if (timing.diagnostics)
            {
                diagnostic = &diagnostics[diagnosticFrames++];
                diagnostic->frameId = run.warmupCompleted + run.framesRecorded + 1;
                diagnostic->before = ObserveWindow(window, state);
                if (sample != nullptr)
                    sample->diagnostics = diagnostic;
            }

            run.result = renderer.RenderFrame(ClearColor, sample, diagnostic);
            if (diagnostic != nullptr)
            {
                diagnostic->presentResult = run.result;
                diagnostic->after = ObserveWindow(window, state);
            }
            if (state.interruption != nullptr)
            {
                run.incompleteReason = state.interruption;
                break;
            }
            if (run.result != S_OK)
            {
                run.incompleteReason = run.result == DXGI_STATUS_OCCLUDED ? "present_occluded" : "present_failed";
                break;
            }
            if (sample != nullptr)
                ++run.framesRecorded;
            else
                ++run.warmupCompleted;
        }
        state.benchmarkActive = false;
        if (sampling)
            run.sampleWallMs = GetTickCount64() - sampleStart;

        renderer.FinishTiming();
        const auto presentation = renderer.ReadPresentationInfo();
        if (!WriteBenchmarkResults(timing, run, frameInfo, options.cubeCount, options.mode, samples, presentation))
            return ReportFailure(window, L"Writing benchmark CSV or summary", E_FAIL, true);
        if (timing.diagnostics && !WriteBenchmarkDiagnostics(timing, diagnosticFrames, diagnostics, samples))
            return ReportFailure(window, L"Writing benchmark diagnostics", E_FAIL, true);
        if (run.incompleteReason != nullptr)
        {
            wchar_t message[256]{};
            swprintf_s(message, L"Benchmark incomplete (%hs, 0x%08lX).",
                run.incompleteReason, static_cast<unsigned long>(run.result));
            WriteError(message);
            return 1;
        }
        return 0;
    }

    int Run(HWND window, WindowState& state, int showCommand, const Options& options)
    {
        DirectX11::Renderer renderer;
        HRESULT result = renderer.Initialize(window, options.cubeCount, options.vsync, options.mode);
        if (FAILED(result))
            return ReportFailure(window, L"Direct3D 11 initialization", result, options.benchmark);

        if (options.benchmark)
        {
            result = renderer.InitializeTiming();
            if (FAILED(result))
                return ReportFailure(window, L"Benchmark timing initialization", result, true);
        }

        const auto& frameInfo = renderer.GetFrameInfo();
        if (!UpdateWindowTitle(window, frameInfo, options.cubeCount))
            return ReportFailure(window, L"SetWindowTextW", HRESULT_FROM_WIN32(GetLastError()), options.benchmark);

        ShowWindow(window, showCommand);
        if (options.benchmark)
        {
            try
            {
                return RunBenchmark(window, state, renderer, options);
            }
            catch (const std::bad_alloc&)
            {
                return ReportFailure(window, L"Benchmark sample storage", E_OUTOFMEMORY, true);
            }
        }

        for (;;)
        {
            int exitCode = 0;
            if (!PumpMessages(exitCode))
                return exitCode;

            if (!state.drawable)
            {
                if (!WaitMessage())
                    return ReportFailure(window, L"WaitMessage", HRESULT_FROM_WIN32(GetLastError()));
                continue;
            }

            if (state.resizePending)
            {
                const UINT oldWidth = frameInfo.width;
                const UINT oldHeight = frameInfo.height;
                state.resizePending = false;
                result = renderer.Resize();
                if (FAILED(result))
                    return ReportFailure(window, L"Back-buffer resize", result);

                if ((oldWidth != frameInfo.width || oldHeight != frameInfo.height) &&
                    !UpdateWindowTitle(window, frameInfo, options.cubeCount))
                    return ReportFailure(window, L"SetWindowTextW", HRESULT_FROM_WIN32(GetLastError()));
            }

            result = renderer.RenderFrame(ClearColor);
            if (FAILED(result))
                return ReportFailure(window, L"Present", result);
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    Options options;
    if (!ParseOptions(options))
    {
        const wchar_t* usage = L"Invalid arguments. Usage: Project1.exe [--cubes N] [--vsync 0|1]\n"
            L"    [--mode separate|instanced]\n"
            L"    [--benchmark [--warmup N] [--frames N] [--output CSV-PATH] [--diagnostics]]\n"
            L"Cube counts are 1, 100, 1000, and 10000. Warmup ranges from 0 to 1000000.\n"
            L"Frames range from 1 to 1000000.";
        if (!WriteError(usage))
            MessageBoxW(nullptr, usage, WindowTitle, MB_OK | MB_ICONERROR);
        return 2;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassW(&windowClass))
        return ReportFailure(nullptr, L"RegisterClassW", HRESULT_FROM_WIN32(GetLastError()), options.benchmark);

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds{ 0, 0, 1280, 720 };
    if (!AdjustWindowRect(&bounds, style, FALSE))
        return ReportFailure(nullptr, L"AdjustWindowRect", HRESULT_FROM_WIN32(GetLastError()), options.benchmark);

    WindowState state;
    const HWND window = CreateWindowExW(0, WindowClassName, WindowTitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, &state);
    if (window == nullptr)
        return ReportFailure(nullptr, L"CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()), options.benchmark);

    const int exitCode = Run(window, state, showCommand, options);
    if (!DestroyWindow(window))
        return ReportFailure(nullptr, L"DestroyWindow", HRESULT_FROM_WIN32(GetLastError()), options.benchmark);

    return exitCode;
}
