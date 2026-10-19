# GPU slicing

Part of the slicing can run on the graphics card through OpenCL, on NVIDIA, AMD, Intel and Apple GPUs.
It is off by default, falls back to the CPU when the GPU is not usable, and gives the same result as the CPU.

## Turning it on

| Preferences > Control > Slicing | Choices |
|---|---|
| GPU slicing | Off (default), Discrete GPU only, On |
| GPU device | Automatic, or one of the listed graphics cards |

On Apple silicon Macs there is one GPU, so the option is a single `GPU slicing` checkbox.

A change takes effect on the next slice. The command-line switch `--gpu` and the variable `SLIC3R_GPU`
override the preference; `SLIC3R_GPU` also overrides `--gpu`.

| Variable | Values | Effect |
|---|---|---|
| `SLIC3R_GPU` | `0`, `1`, `auto` | off (still prints the `[time]` lines); on; on only with a discrete GPU |
| `SLIC3R_GPU_DEVICE` | text | the device whose name contains the text, e.g. `4080`, `Radeon`, `Arc` |
| `SLIC3R_GPU_VERIFY` | `0`, `1` | compare no call (for timing); compare every call |
| `SLIC3R_GPU_FP64` | `0` | integer kernel also on GPUs with native doubles |

## What runs on the GPU

- Only mesh slicing: every triangle against every slicing plane. Everything else stays on the CPU.
- Only calls with at least 1M triangle-plane records (about the number of lines) go to the GPU.
- The first GPU call is also computed on the CPU and compared. Later calls use the GPU only if it was
  at least a fifth faster on that call.
- A mismatch or a driver error prints a `-> CPU` line, uses the CPU result and keeps later calls on the
  CPU. A mismatch of the native double kernel only switches to the integer kernel.
- Mesh slicing is about 1 % of a slice, so the GPU saves tenths of a second at most.

## Log lines

With `--gpu` or `SLIC3R_GPU` set, the `[gpu]` and `[time]` lines go to stderr;
`--debug 2 --logfile gpu.log` also writes them to a file. With only the preference on, they are in the application log.

```
[gpu] OpenCL 1.2 (Sep 26 2026 04:14:27) | Apple | Apple M4 Pro | GPU | driver 1.2 1.0 | 40.2 GB unified | 20 CU | init 52 ms
[gpu] slice_lines: 225154 facets, 2181 planes, 2.47M lines, soft, gpu 35.5 ms (up 0.3, wait 27.7, decode 5.8, sort 5.3), build 65.3 ms, verify identical, cpu 101.9 ms
[time] total 34.88
```

- The first line is the device in use; `slice_lines` comes once per sliced mesh.
- `fp64` (native doubles) or `soft` (integers) is the kernel; `gpu` and `cpu` are the times of the same
  call; `build` is the kernel compile.
- `[gpu] ... -> CPU: <reason>`: the CPU was used instead, e.g. `under 1.00M items`,
  `faster on the CPU (...)`, `no OpenCL library`, `verify mismatch ...`.
- `[time]`: wall time per step in seconds; `total` for the whole slice.

## Drivers

| | Windows | Linux |
|---|---|---|
| NVIDIA | Game Ready or Studio driver | `libnvidia-compute-<version>` (Ubuntu), `opencl-nvidia` (Arch) |
| AMD | Adrenalin | ROCm, or Mesa Rusticl with `RUSTICL_ENABLE=radeonsi` |
| Intel | the graphics driver | `intel-opencl-icd` (Ubuntu, Debian), `intel-compute-runtime` (Fedora, Arch) |

On Linux the OpenCL loader is a separate package: `ocl-icd-libopencl1` (Ubuntu, Debian) or `ocl-icd`.
The Flatpak cannot use ROCm or Intel's runtime; use the AppImage for tests.
`clinfo -l` (Linux) or GPU-Z (Windows) shows the OpenCL devices; `SLIC3R_GPU_DEVICE` picks one of them.

## Reporting a test

Per machine, slice `benchy_lh.3mf` and `moon.3mf` with `SLIC3R_GPU=1`, each twice; the second run is the
one for timing. Then slice both once with `SLIC3R_GPU=0` for the CPU time. Send the device line
(`[gpu] OpenCL ...`), every `slice_lines` and `-> CPU` line, and `[time] total` of each run.

```
# Linux
SLIC3R_GPU=1 ./Snapmaker_Orca_Linux_AppImage*.AppImage --datadir dd --slice 0 --outputdir out benchy_lh.3mf
# Windows, PowerShell in the folder of snapmaker-orca.exe
$env:SLIC3R_GPU='1'
Start-Process -Wait -NoNewWindow .\snapmaker-orca.exe -ArgumentList '--datadir','dd','--slice','0','--outputdir','out','benchy_lh.3mf'
```
