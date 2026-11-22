# Graphics Benchmark

## Build and run

- Open `GraphicsBenchmark.sln` in Visual Studio 2022 with **Desktop development with C++**, MSVC **v143**, and a Windows SDK. The build was verified with Visual Studio 2022 17.11.2 and Windows SDK 10.0.22621.0.
- Run on Windows 10 or later with a hardware adapter that supports Direct3D feature level 11.0. Debug uses the Direct3D debug layer supplied by the Windows **Graphics Tools** optional feature. The project uses Win32, Direct3D 11, DXGI, and WRL from the SDK, with no third-party packages or assets.
- Select `Debug | x64` for correctness checks and `Release | x64` for performance comparisons. Build the solution and run `Project1` from Visual Studio. Use a Developer PowerShell for VS 2022 to run the same builds from the directory containing the solution.

```powershell
msbuild .\GraphicsBenchmark.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
msbuild .\GraphicsBenchmark.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
```

The executables are `x64\Debug\Project1.exe` and `x64\Release\Project1.exe`. Both configurations also build with `/p:Platform=x86`, which maps to the project's Win32 target and outputs `Debug\Project1.exe` and `Release\Project1.exe`.

From the solution directory, run `.\x64\Debug\Project1.exe` for a smoke test. MSBuild compiles the HLSL shaders with the Windows SDK shader compiler and embeds their bytecode in the executable. Shader compiler errors appear in the build output. The executable can run from any working directory.

The default is one cube in separate-draw mode. Supported values for `--cubes` are 1, 100, 1000, and 10000. Use `--mode separate|instanced` in interactive or benchmark runs. Set the same arguments in **Project Properties > Debugging > Command Arguments** when launching from Visual Studio.

```powershell
.\x64\Debug\Project1.exe --cubes 1
.\x64\Debug\Project1.exe --cubes 100
.\x64\Debug\Project1.exe --cubes 1000 --mode separate
.\x64\Release\Project1.exe --cubes 10000 --mode instanced
```

The title shows the device's adapter name, the requested cube count, and the actual back-buffer dimensions. Resize the window and check that the grid stays fully in view and the dimensions update. Minimize and restore it, then close it and confirm the process exits. Run Debug under the Visual Studio debugger and check the Debug Output window for Direct3D diagnostics.

## Timing separate draws and instancing

Run an optimized benchmark from PowerShell without a debugger. Use `-Wait` to read the exit code and wait for both output files.

```powershell
$run = Start-Process .\x64\Release\Project1.exe -Wait -PassThru -ArgumentList '--benchmark --cubes 1000 --mode instanced --vsync 0 --seconds 10 --warmup-seconds 3 --sample-capacity 100000 --require-foreground --output benchmark-instanced.csv'
$run.ExitCode
```

- `--benchmark` renders the normal scene, finishes automatically, and writes the CSV and its accompanying `<csv-path>.summary.txt` after final query collection. Choose a new output path to keep prior results. Existing files at the selected paths are replaced. Quote a path that contains spaces.
- For a controlled comparison, hold the cube count, display, back-buffer dimensions, presentation settings, power state, and diagnostics setting constant. Alternate which mode runs first in each repetition. The foreground NVIDIA baseline measured 600 frames in 10.000 seconds at 1280 × 720 with `Present(0, DXGI_PRESENT_ALLOW_TEARING)` on a 60 Hz display. Compare CPU submission and GPU marker intervals separately from observed loop FPS, which can remain display-paced.
- `--vsync 0|1` sets the `Present` sync interval and defaults to `1` in both modes. When DXGI reports tearing support, the renderer enables it on the two-buffer flip-discard chain and preserves that creation flag on resize. The renderer sets `DXGI_PRESENT_ALLOW_TEARING` only for interval 0 with a tearing-enabled, windowed chain. Other configurations use presentation flags zero. Systems without tearing support keep the windowed flip-discard path with swap-chain flags zero. [Present queue semantics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present) · [Tearing requirements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)
- For frame-count runs, `--warmup` accepts 0 through 1,000,000 frames and defaults to 120. `--frames` accepts 1 through 1,000,000 frames and defaults to 600.
- For duration runs, `--seconds` accepts 1 through 600 whole seconds and `--warmup-seconds` accepts 0 through 600, with a default of 3. Each phase ends at the first frame boundary that reaches its requested duration. `--sample-capacity` sets the preallocated sample limit from 1 through 1,000,000, with a default of 1,000,000. If storage fills before the measurement deadline, the benchmark saves an incomplete result with `incomplete_reason=sample_capacity_exhausted`. Duration options are incompatible with frame-count options and `--diagnostics`.
- `--output` defaults to `benchmark.csv`. All timing and output options require `--benchmark`. Use `--require-foreground` for controlled runs. It allows up to five seconds to acquire focus before warmup and ends the run if focus is lost or the window is hidden. The guard uses window messages during the run.
- Once the initial window setup finishes, a resize, minimize, zero-sized client area, display change, or close during warmup or sampling ends the run. Run on the interactive desktop so DXGI can present the window. An occluded presentation ends the run with `incomplete_reason=present_occluded`. The program writes partial results with `completed=false` and the reason. Benchmark interruptions and runtime errors exit with code 1. Invalid arguments exit with code 2. A completed run with both files written exits with code 0. [DXGI status codes](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status)

### Measurement boundaries

Warmup uses the same scene, back buffers, and presentation interval as sampling. The benchmark records warmup wall time without per-frame CPU or GPU measurements. The first CSV frame ID is the number of completed warmup frames plus one. Each CSV row represents a sampled frame whose `Present` returned `S_OK`. An unsuccessful presentation ends the benchmark and its frame stays out of the CSV.

Sample wall time uses QPC from the transition to sampling until the benchmark exits the sample loop. This includes message processing, in-loop query polling, and presentation. The summary reports raw QPC ticks, frequency, milliseconds, and observed application loop FPS. Final query collection and result writes follow the wall interval.

| CSV metric | Measured interval |
| --- | --- |
| `cpu_submit_ms` | CPU QPC interval from after the optional GPU start-marker call and just before target lookup and binding through the last draw. Includes both clears, draw dispatch and submission, the separate mode's constant-buffer updates, the timing branch before the ending counter read, QPC boundary overhead, and any driver stalls. Query API calls, collection, conversion, and presentation are outside this interval. |
| `present_ms` | CPU QPC interval immediately around `Present`, with QPC boundary overhead. The summary records the exact sync interval and flags. |
| `gpu_elapsed_ms` | GPU timestamp interval from just before target binding and both clears to after the final draw submission and before `Present`. The timestamps lie inside the same disjoint query. Elapsed time can include gaps in GPU execution. |

The renderer caches the CPU counter frequency at timing initialization and converts each QPC delta to milliseconds. It owns 16 preallocated D3D11 query sets, each with a disjoint query and two timestamps. The benchmark loop owns the preallocated sample rows, whose addresses and frame IDs stay stable through collection. Before each sampled frame, the renderer polls older sets once with `D3D11_ASYNC_GETDATA_DONOTFLUSH`. `S_FALSE` leaves the set pending, and a set is reused only after all three reads succeed. When every usable set is pending, that frame keeps its CPU values and gets `skipped_ring_full`. A query read error marks that frame `invalid_query_error` and retires its set. If every set is retired, subsequent frames get `skipped_query_ring_disabled`. [QPC frequency](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency) · [Direct3D query types](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query) · [GetData return values](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata) · [Non-flushing collection](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag)

Final collection polls once, flushes the context once if queries are still pending, and polls with sleeps for up to two seconds. Remaining sets get `unresolved_timeout`. Disjoint intervals, a zero GPU counter frequency, and reversed timestamps get distinct invalid statuses. For valid samples, the renderer divides the tick delta by the frequency returned by the disjoint query and converts to milliseconds. The GPU time column is blank for skipped, invalid, and unresolved samples. The summary records counts and uses only valid values for each metric. Median averages the middle pair for even sample counts, and p95 uses nearest rank. With no valid values, both statistics are reported as unavailable.

CPU and GPU intervals can overlap, so report them separately. GPU timestamps measure elapsed time between markers. Use separate experiments to determine utilization or locate a bottleneck. Timed runs use no pipeline-statistics queries.

### Diagnostic runs

Every benchmark summary records the startup tearing support check, swap-chain flags, arguments used by `Present`, and enabled instrumentation. Frame-count validation runs can use `--diagnostics`. Its default output is `benchmark-diagnostic.csv`, and it also writes `<csv-path>.diagnostics.csv`. Duration runs leave submission counting and per-frame window observations off. Use a distinct output path for each run.

The diagnostic CSV has one row per attempted frame, including warmup and any final frame that fails to present. It records actual draw submissions, the arguments and HRESULT for `Present`, query issue and collection slots, the original frame ID saved on the slot, and raw timestamp and disjoint data when available. The per-frame draw count adds instrumentation inside the CPU submission interval. Warmup rows have no GPU queries. For equivalence checks, capture the back buffer and count input-assembler triangles with pipeline statistics in an untimed Debug validation. Each cube contributes 12 input triangles in either mode.

Window observations around each frame record foreground ownership, keyboard focus, Win32 visibility, minimized state, and cumulative focus, app activation, and visibility event counts. Win32 visibility describes window state. Check desktop coverage separately to verify that the window is unobstructed. The renderer and benchmark preallocate their query and diagnostic storage before warmup and write all results after collection.

## Architecture

The starting solution contained a single empty Visual C++ application project. It had no entry point, render loop, meshes, shaders, draw calls, or CPU or GPU timing. No course notes or coding standards were present in the project.

`Project1/Main.cpp` contains the Win32 entry point, message loop, and clear color. `wWinMain` owns the window and the `WindowState` that its callback borrows. `Run` owns the renderer and calls `Initialize`, `Resize`, and `RenderFrame`.

`Project1/DirectX11/Renderer.h` declares the interface, and `Renderer.cpp` contains the Direct3D and DXGI calls. The renderer owns the device, immediate context, swap chain, views, buffers, shaders, and pipeline states through `ComPtr`. The swap chain owns its back buffers. The depth view keeps the depth texture alive. The renderer clears the context's bindings at shutdown, then its owning references release before `wWinMain` destroys the window.

`WM_SIZE` records the change for `Run` to handle on the window thread. Interactive rendering pauses while minimized or when the client area has zero size. For a drawable resize, the renderer unbinds the color and depth targets, releases both views, and resizes the swap chain with its creation flags. It recreates the views and depth texture, then updates the viewport and uses the actual back-buffer dimensions for framing. Each frame binds and clears both targets, draws the scene, and presents using the arguments selected at swap-chain initialization. The application stays windowed and disables DXGI's Alt+Enter transition. [DXGI resize documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers) · [DXGI presentation documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)

### Cube coordinates and transforms

The scene uses left-handed coordinates with +Y up. Each cube spans −1 to +1 in local space. The single cube sits at the origin and uses the original camera at (4, 3, −6) at the default aspect ratio. `CubeScene.cpp` generates the larger presets as centered XZ grids of 10 × 10, 40 × 25, and 100 × 100, with three units between cube centers. The camera looks toward the origin from the same direction in each preset. The 45-degree vertical field of view and the back-buffer aspect ratio determine how far the camera backs up to fit an enclosing sphere. Near and far planes for the grids bracket that sphere. The camera stays fixed until a resize changes the framing. The rasterizer uses clockwise front faces and back-face culling.

The renderer owns the contiguous CPU arrays of world translations and cached matrices. DirectXMath uses row vectors. Each row-major `XMFLOAT4X4` stores `world * view * projection`. The renderer computes the matrices for the initial scene and refreshes them when the back-buffer dimensions change. Both vertex shaders apply the same homogeneous position and matrix multiplication. All cubes share the same immutable vertex and index buffers and pixel shader. [HLSL matrix ordering](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-per-component-math#matrix-ordering)

This compares complete static-scene submission approaches. Separate mode updates the 64-byte constant buffer at `b0` and submits one `DrawIndexed` per cube each frame. Instanced mode owns a vertex buffer with one 64-byte matrix per cube. The renderer uploads the full matrix cache when it computes or refreshes it, outside steady-state timing. The input assembler advances four matrix rows in vertex slot 1 once per instance. A single `DrawIndexedInstanced` submits the whole grid and avoids per-frame matrix uploads. The renderer releases the mode-specific buffers along with its other resources. [D3D11 instance data](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_input_element_desc)

## Working rules

These project rules are inspired by Ed Keenan's Optimized C++ and Architecture of Real-Time Systems courses. Attribute only requirements supported by the actual course materials to Ed Keenan.

### Understand the existing architecture

- Read existing code and documentation before changing it. Follow the project's naming, formatting, and class conventions.
- If course notes or coding standards are available, read them and identify the specific rules that apply.

### Make ownership and lifetime explicit

- Explain who owns each resource, who can reference it, and when it is created and destroyed.
- Make initialization and shutdown order clear. Define or delete copy and move operations as ownership requires. Follow the existing resource-management approach consistently.

### Understand the cost of the code

- Consider allocations, copies, indirection, virtual dispatch, data layout, alignment, and cache locality in frequently executed code. Identify candidates for measurement and use the data to establish bottlenecks.
- Keep asset loading, shader compilation, and resource creation outside the measured steady-state rendering workload.
- Require a specific hypothesis and a measured baseline before introducing custom allocators, SIMD, prefetching, or multithreading.

### Keep architecture deliberate and small

- Give each component a clear responsibility. Keep scene data, resource management, rendering, and measurement distinguishable within a small codebase.
- Reuse existing managers and abstractions when they fit. Introduce design patterns only to solve concrete problems and explain the performance and maintenance trade-offs. Apply that same standard to singletons, factories, and inheritance hierarchies.

### Establish correctness before performance

- Check initialization and API failures. Use assertions for programmer invariants and runtime error handling for failures that can occur in normal execution.
- Verify resource cleanup and visual correctness. Use Debug for correctness checks and optimized Release for performance comparisons. Preserve a working baseline.

### Run controlled experiments

- Measure before optimizing. State the hypothesis before implementing an optimization and change one major variable at a time.
- Keep benchmark workloads repeatable. Hold the scene, camera, resolution, workload, and rendering output equivalent across comparison modes.
- Record build configuration, hardware, VSync settings, warm-up, measurement duration, and repeated-run results.
- Report CPU and GPU timings separately and describe the scope of each measurement. Classify draw-call submission time as CPU time and report GPU execution time using GPU measurements.
- Keep logging and result-file writes outside measured regions. Report regressions, variability, and inconclusive results honestly. Never invent benchmark results.

### Teach through the implementation

- Make small, reviewable changes. Before a substantial change, briefly explain the problem, the proposed design, and the relevant trade-off.
- Afterward, explain the important code paths and how to verify them. Explain why the technique helps this workload in concrete terms.

## Milestones

The Win32 baseline, fixed-camera cube, multi-cube separate draws, CPU/GPU timing, and instancing are complete.
