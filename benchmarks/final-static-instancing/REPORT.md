# Static scene instancing on Direct3D 11

The experiment supports the hypothesis at 100, 1,000, and 10,000 cubes. One instanced draw with persistent transform data lowered CPU submission median and p95 in all nine matched pairs. The absolute CPU median savings increased with cube count on the tested hardware.

## Hypothesis and comparison

For the same static objects, instancing should reduce CPU submission time, with larger absolute savings as object count increases. Separate mode uploads one world-view-projection matrix to a constant buffer and issues one indexed draw per cube every frame. Instanced mode reads its cached matrices from an instance vertex buffer and submits the scene in one indexed instanced draw. Its matrix upload occurs during setup or projection changes. The comparison changes both draw count and per-frame transform-upload behavior. Both modes share the indexed cube mesh, deterministic positions, camera, color and depth output settings, and viewport.

## Conditions and measurement

- The November 22, 2026 trials ran on an AMD Ryzen 7 5800H and an NVIDIA GeForce RTX 3060 Laptop GPU. Windows 11 build 26300 used NVIDIA driver 610.91. The executable was built in Visual Studio 2022 as Release x64 with MSVC v143 14.38.33130 and Windows SDK 10.0.22621.0. It ran without a debugger.
- All trials used AC power with the Balanced plan and battery saver off. The stationary 1280 × 720 window stayed at the same foreground position on a display reporting 72 Hz. The app called `Present(0, DXGI_PRESENT_ALLOW_TEARING)` on a two-buffer flip-discard chain. Read-only NVAPI profile checks before and after showed the application’s Max Frame Rate setting was Off. A separate post-run read found the global profile set to 60 FPS. NVIDIA driver VSync was unknown at both scopes. The temporary Windows per-app GPU preference was restored afterward. [Recorded conditions](conditions.json) · [NVIDIA profile settings API](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h)
- Each mode ran three times per count with a three-second warmup and a ten-second QPC target. Separate went first in repetitions 1 and 3, with instancing first in repetition 2. Storage was preallocated for 100,000 samples per run. Per-frame diagnostic submission counts and window probes were off. The in-app foreground guard used window messages, while a separate harness checked the desktop and power roughly every 100 ms. The harness logged 16 startup transient checks within 0.2 seconds of activation. All 18 timed runs passed the condition checks without a flagged interruption. [Run index](runs.csv)
- CPU submission used QPC around binding, clears, draw dispatch, and any in-frame uploads. It can include driver stalls. GPU timestamps bracketed clears and draws, with results read asynchronously from a 16-slot ring. A separate QPC interval measured `Present`. The sample wall interval includes the message loop, query polling, and presentation. Final query collection and file writes followed timing.

## Correctness and verification

Debug x64 and Release x64 built successfully. Untimed Debug readback found pixel-identical, nonblank output between modes at 1, 100, 1,000, and 10,000 cubes at 1280 × 720 and after resizing to 960 × 800. The D3D11 debug layer reported zero warnings or errors. The CPU submission counter confirmed N draws versus one. Independently, pipeline statistics confirmed 12 input triangles per cube in both modes.

Debug tests verified legacy frame counts and diagnostic output, the default three-second warmup, QPC duration and FPS calculations, invalid option combinations, and explicit sample-capacity exhaustion with partial results. A resize and an injected focus-loss message each produced a flagged partial run. Independent calculations from every final CSV reproduced the summary quantiles and query counts.

## Per-run results

Rows are in execution order. Loop FPS is the number of successful application frames divided by QPC sample duration. GPU coverage columns list valid, invalid, skipped, and unresolved samples. All 576,713 samples had valid GPU timing. Each mode link opens its summary. The run index lists the corresponding raw CSVs. [Run index](runs.csv)

| Cubes | Rep | Mode | CPU med / p95 (ms) | GPU med / p95 (ms) | Present med / p95 (ms) | Wall (s) | Loop FPS | GPU V / I / S / U |
| ---: | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 | 1 | [separate](raw/c100-r1-separate.csv.summary.txt) | 0.004400 / 0.006400 | 0.021504 / 0.040960 | 0.194200 / 0.414700 | 10.000032 | 3705.6 | 37056 / 0 / 0 / 0 |
| 100 | 1 | [instanced](raw/c100-r1-instanced.csv.summary.txt) | 0.000900 / 0.001700 | 0.019456 / 0.036864 | 0.189400 / 0.390300 | 10.000141 | 3890.2 | 38903 / 0 / 0 / 0 |
| 100 | 2 | [instanced](raw/c100-r2-instanced.csv.summary.txt) | 0.000900 / 0.001700 | 0.014336 / 0.025600 | 0.182300 / 0.390700 | 10.000307 | 3978.1 | 39782 / 0 / 0 / 0 |
| 100 | 2 | [separate](raw/c100-r2-separate.csv.summary.txt) | 0.004100 / 0.006200 | 0.021504 / 0.039936 | 0.192000 / 0.414200 | 10.001348 | 3731.0 | 37315 / 0 / 0 / 0 |
| 100 | 3 | [separate](raw/c100-r3-separate.csv.summary.txt) | 0.004100 / 0.006100 | 0.021504 / 0.040960 | 0.192200 / 0.396100 | 10.000093 | 3780.3 | 37803 / 0 / 0 / 0 |
| 100 | 3 | [instanced](raw/c100-r3-instanced.csv.summary.txt) | 0.000900 / 0.001700 | 0.012288 / 0.019456 | 0.173700 / 0.361700 | 10.000094 | 4158.9 | 41589 / 0 / 0 / 0 |
| 1,000 | 1 | [separate](raw/c1000-r1-separate.csv.summary.txt) | 0.034100 / 0.164600 | 0.064512 / 0.108544 | 0.171300 / 0.418600 | 10.000169 | 3368.6 | 33687 / 0 / 0 / 0 |
| 1,000 | 1 | [instanced](raw/c1000-r1-instanced.csv.summary.txt) | 0.000900 / 0.001700 | 0.023552 / 0.037888 | 0.186100 / 0.378900 | 10.000132 | 3966.9 | 39670 / 0 / 0 / 0 |
| 1,000 | 2 | [instanced](raw/c1000-r2-instanced.csv.summary.txt) | 0.000900 / 0.001600 | 0.024576 / 0.040960 | 0.189600 / 0.386200 | 10.000041 | 3928.9 | 39289 / 0 / 0 / 0 |
| 1,000 | 2 | [separate](raw/c1000-r2-separate.csv.summary.txt) | 0.033900 / 0.164300 | 0.064512 / 0.106496 | 0.170400 / 0.408700 | 10.000061 | 3390.5 | 33905 / 0 / 0 / 0 |
| 1,000 | 3 | [separate](raw/c1000-r3-separate.csv.summary.txt) | 0.029100 / 0.163900 | 0.059392 / 0.069632 | 0.182300 / 0.392500 | 10.000022 | 3418.5 | 34185 / 0 / 0 / 0 |
| 1,000 | 3 | [instanced](raw/c1000-r3-instanced.csv.summary.txt) | 0.000800 / 0.001700 | 0.023552 / 0.040960 | 0.188200 / 0.377900 | 10.001113 | 3962.8 | 39632 / 0 / 0 / 0 |
| 10,000 | 1 | [separate](raw/c10000-r1-separate.csv.summary.txt) | 0.335200 / 0.426300 | 0.468992 / 2.201600 | 0.049600 / 2.594000 | 10.000304 | 842.8 | 8428 / 0 / 0 / 0 |
| 10,000 | 1 | [instanced](raw/c10000-r1-instanced.csv.summary.txt) | 0.001000 / 0.001600 | 0.087040 / 0.091136 | 0.233000 / 0.355400 | 10.000018 | 3262.8 | 32628 / 0 / 0 / 0 |
| 10,000 | 2 | [instanced](raw/c10000-r2-instanced.csv.summary.txt) | 0.001000 / 0.001600 | 0.090112 / 0.092160 | 0.235800 / 0.370000 | 10.001308 | 3208.2 | 32086 / 0 / 0 / 0 |
| 10,000 | 2 | [separate](raw/c10000-r2-separate.csv.summary.txt) | 0.369800 / 0.515400 | 0.467968 / 2.282496 | 0.126800 / 2.655200 | 10.001308 | 959.3 | 9594 / 0 / 0 / 0 |
| 10,000 | 3 | [separate](raw/c10000-r3-separate.csv.summary.txt) | 0.366800 / 0.502000 | 0.467968 / 2.366464 | 0.122600 / 2.728900 | 10.000219 | 913.6 | 9136 / 0 / 0 / 0 |
| 10,000 | 3 | [instanced](raw/c10000-r3-instanced.csv.summary.txt) | 0.001000 / 0.001700 | 0.087040 / 0.092160 | 0.235400 / 0.393000 | 10.000092 | 3202.5 | 32025 / 0 / 0 / 0 |

## Paired savings

Savings are separate minus instanced in the same cube-count and repetition pair. Percent reduction divides that difference by the separate value. A negative entry means the instanced interval was longer. Each p95 difference compares two run-level percentiles. There is no per-frame pairing across runs. [Paired calculations](paired-savings.csv)

| Cubes | Rep | CPU med / p95 (ms, %) | GPU med / p95 (ms, %) | Present med / p95 (ms, %) |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 1 | +0.003500 (+79.5%) / +0.004700 (+73.4%) | +0.002048 (+9.5%) / +0.004096 (+10.0%) | +0.004800 (+2.5%) / +0.024400 (+5.9%) |
| 100 | 2 | +0.003200 (+78.0%) / +0.004500 (+72.6%) | +0.007168 (+33.3%) / +0.014336 (+35.9%) | +0.009700 (+5.1%) / +0.023500 (+5.7%) |
| 100 | 3 | +0.003200 (+78.0%) / +0.004400 (+72.1%) | +0.009216 (+42.9%) / +0.021504 (+52.5%) | +0.018500 (+9.6%) / +0.034400 (+8.7%) |
| 1,000 | 1 | +0.033200 (+97.4%) / +0.162900 (+99.0%) | +0.040960 (+63.5%) / +0.070656 (+65.1%) | -0.014800 (-8.6%) / +0.039700 (+9.5%) |
| 1,000 | 2 | +0.033000 (+97.3%) / +0.162700 (+99.0%) | +0.039936 (+61.9%) / +0.065536 (+61.5%) | -0.019200 (-11.3%) / +0.022500 (+5.5%) |
| 1,000 | 3 | +0.028300 (+97.3%) / +0.162200 (+99.0%) | +0.035840 (+60.3%) / +0.028672 (+41.2%) | -0.005900 (-3.2%) / +0.014600 (+3.7%) |
| 10,000 | 1 | +0.334200 (+99.7%) / +0.424700 (+99.6%) | +0.381952 (+81.4%) / +2.110464 (+95.9%) | -0.183400 (-369.8%) / +2.238600 (+86.3%) |
| 10,000 | 2 | +0.368800 (+99.7%) / +0.513800 (+99.7%) | +0.377856 (+80.7%) / +2.190336 (+96.0%) | -0.109000 (-86.0%) / +2.285200 (+86.1%) |
| 10,000 | 3 | +0.365800 (+99.7%) / +0.500300 (+99.7%) | +0.380928 (+81.4%) / +2.274304 (+96.1%) | -0.112800 (-92.0%) / +2.335900 (+85.6%) |

- **100 cubes** saved 0.003200–0.003500 ms at the CPU submission median, a 78.0–79.5% reduction. That supports the hypothesis at the smallest tested count. GPU median savings varied from 0.002048 to 0.009216 ms across the three pairs.
- **1,000 cubes** saved 0.028300–0.033200 ms at the CPU submission median, a 97.3–97.4% reduction. That supports the hypothesis with larger absolute savings. Separate mode’s GPU p95 ranged from 0.069632 to 0.108544 ms while the instanced range was 0.037888–0.040960 ms.
- **10,000 cubes** saved 0.334200–0.368800 ms at the CPU submission median, a 99.7% reduction. That supports the hypothesis with the largest absolute savings in every repetition. Instancing increased median `Present` time by 0.109000–0.183400 ms while reducing `Present` p95 by 2.238600–2.335900 ms. The separately observed loop rates were 842.8–959.3 FPS for separate draws and 3202.5–3262.8 FPS for instancing.

## Limits and conclusion

The earlier 60 FPS baseline ran on a 60 Hz display. This series used a display reporting 72 Hz and measured application loop rates from 842.8 to 4158.9 FPS. Those rates count successful application iterations. The physical scan-out mode was unobserved.

The results apply to a fixed scene that can reuse GPU transforms. The cost of generating and uploading new instance data for moving objects is outside this experiment. QPC reads, GPU markers, and asynchronous polling still perturb the loop. The GPU interval can include idle gaps and can overlap CPU work, so CPU and GPU intervals remain separate. The experiment measures the combined effect of fewer submissions and fewer per-frame uploads. It cannot allocate the savings between those changes.

On this laptop with this static scene and Direct3D 11 setup, instancing reduced CPU submission time at every tested count. Every 1,000-cube pair saved more CPU time than the 100-cube pairs, and every 10,000-cube pair saved more than the 1,000-cube pairs.
