<p align="center">
  <img src="docs/logo.svg" width="96" alt="openxr-pose-layer logo">
</p>

# openxr-pose-layer

An OpenXR API layer that logs, records and replays head and controller tracking.

[![CI](https://github.com/HeathHowren/openxr-pose-layer/actions/workflows/ci.yml/badge.svg)](https://github.com/HeathHowren/openxr-pose-layer/actions/workflows/ci.yml)

openxr-pose-layer sits between a VR app and the OpenXR runtime as an explicit API
layer. In `log` mode it prints the tracking and input calls the app makes, with
timings. In `record` mode it writes every frame's head pose, eye views,
controller poses and action states to a small binary file. In `replay` mode it
feeds those recorded poses back into any OpenXR app and rewrites the poses the
app submits for display, so the same motion runs again with no headset moving.
That gives a VR developer a deterministic repro for a tracking bug, and a CI job
a fixed input to render against.

openxr-pose-layer is written by Heath Howren
([Cyborg Elf](https://www.youtube.com/cyborgelf)) of
[Game Reversal Club](https://gamereversal.club). It watches what a VR app asks
the runtime for and can feed it back, the way
[Pointer Lab](https://gamereversal.club/tools/pointer-lab/) watches what a game
reads and writes.

```
poseplay 1.0.2
file            sample.oxrr
format version  1
frames          120
duration        1322.2 ms  (90.0 fps)
view sets       120
space locates   120 single, 0 grouped
inputs          0 pose, 120 boolean, 120 float, 0 vector2f
head position   x [-0.0300, 0.0300]  y [1.5500, 1.6500]  z [-0.0200, 0.0200]  (120 frames)
```

*Real output from `poseplay info` over a 120-frame recording written by the
layer in `record` mode, driven through the layer's own test harness (see
`tests/`).*

## What it does

- **Records by frame, not by clock.** Each frame is one `xrWaitFrame`. Poses and
  inputs are stored under the frame index the layer assigns, and entries within
  a frame are replayed in the order they were recorded. Nothing is keyed on an
  `XrSpace` or `XrAction` handle, because those do not survive into a new run.
- **Intercepts the calls that carry motion.** `xrWaitFrame`, `xrLocateViews`,
  `xrLocateSpace`, `xrLocateSpaces`, `xrSyncActions`, the four
  `xrGetActionState*` calls, and `xrEndFrame`. Anything else passes straight
  through.
- **Replays into the app and the compositor.** In `replay` mode the locate and
  action-state calls return the recorded values, and `xrEndFrame` rewrites the
  projection-layer view poses too, so an app that caches its poses still renders
  from the recorded path.
- **Time scale, loop and offset.** Replay can run at a different speed, loop at
  the end, and apply an `x y z yawDegrees` offset to every pose so the recorded
  path plays from a new place or facing.
- **A layer, not a runtime.** It does not invent tracking. Running an app with no
  headset needs a null runtime under the layer; see Quick start.
- **poseplay.** Summarize a recording, dump per-frame head poses, and trim a
  recording to a frame range, with `--json` for scripts.
- **Tested with no runtime.** The core and the layer's negotiation, record and
  replay paths are unit-tested against an in-process fake next layer, so the
  build needs no OpenXR runtime and no headset.

## Download

Get the latest zip from
[Releases](https://github.com/HeathHowren/openxr-pose-layer/releases). It
contains:

```
openxr_pose_layer.dll                 the API layer
XR_APILAYER_GRC_pose_layer.json       its manifest (sits next to the DLL)
poseplay.exe                          the recording tool
install.ps1 / uninstall.ps1           register or remove the layer for your user
LICENSE, THIRD_PARTY_NOTICES.md, README.md, CHANGELOG.md
```

The binaries are unsigned. Antivirus software may flag a new unsigned DLL that
loads into other processes. Build from source if you would rather not take a
binary on trust.

## Quick start

Register the layer for your user. This writes only under `HKEY_CURRENT_USER` and
needs no administrator rights:

```powershell
.\install.ps1
```

The script also adds `XR_APILAYER_GRC_pose_layer` to your user's
`XR_ENABLE_API_LAYERS`. The loader expects the names in that variable to be
separated by semicolons, for example `OtherLayer;XR_APILAYER_GRC_pose_layer`.
The 1.0.0 script used commas; run the new `install.ps1` once to fix the value.

Pick a mode with environment variables, then start your OpenXR app from the same
shell. Record a session:

```powershell
$env:XR_POSE_LAYER_MODE = "record"
$env:XR_POSE_LAYER_FILE = "run.oxrr"
# start your OpenXR app
```

The layer writes the file only when the app shuts down OpenXR (calls
`xrDestroyInstance`). If the app crashes, is killed, or exits without that call,
nothing is saved. If the file cannot be written, the layer prints an error to
stderr and to the debugger output.

Replay it into the same or another app:

```powershell
$env:XR_POSE_LAYER_MODE = "replay"
$env:XR_POSE_LAYER_FILE = "run.oxrr"
# start an OpenXR app; it now sees the recorded poses
```

On real hardware, replay overrides tracking, so the scene world-locks to the
recorded path and stops following your head. That is expected. To run with no
headset at all, put a null runtime under the layer: Monado's simulated driver
(Windows builds exist) or the SteamVR null driver. The layer does not provide
tracking of its own.

To remove the layer:

```powershell
.\uninstall.ps1
```

## Modes and settings

Settings come from environment variables, or from a `key = value` file named by
`XR_POSE_LAYER_CONFIG` (same names, without the prefix). Environment variables
win over the file.

| Variable | Meaning |
|---|---|
| `XR_POSE_LAYER_MODE` | `off` (default), `log`, `record`, `replay` |
| `XR_POSE_LAYER_FILE` | recording path (record and replay) |
| `XR_POSE_LAYER_LOG` | call-trace path for `log` mode (default: stderr) |
| `XR_POSE_LAYER_TIME_SCALE` | replay speed, e.g. `0.5` or `2.0` (default `1.0`) |
| `XR_POSE_LAYER_LOOP` | `1` to loop replay at the end (default `0`) |
| `XR_POSE_LAYER_OFFSET` | replay offset `"x y z"` or `"x y z yawDegrees"` |
| `XR_POSE_LAYER_CONFIG` | path to a config file |

`log` mode writes one line per intercepted call, with the elapsed time, the
frame index, the result, and how long the downstream call took:

```
[pose-layer] 1.0.2 log mode
[pose-layer] +    0.187ms f0 xrWaitFrame -> XR_SUCCESS (0.1 us)
[pose-layer] +    0.220ms xrLocateViews -> XR_SUCCESS (0.0 us)
[pose-layer] +    0.231ms xrLocateSpace -> XR_SUCCESS (0.1 us)
[pose-layer] +    0.237ms xrSyncActions -> XR_SUCCESS (0.1 us)
[pose-layer] +    0.242ms xrGetActionStateBoolean -> XR_SUCCESS (0.0 us)
[pose-layer] +    0.253ms xrEndFrame -> XR_SUCCESS (0.2 us)
```

*Real `log`-mode output produced by the fake-next-layer test harness.*

## poseplay

```
poseplay info <file> [--json]
poseplay dump <file> [--frames A-B] [--json]
poseplay trim <file> --frames A-B -o <out>
```

`info` prints the summary above. `dump` prints per-frame times and head
positions. `trim` writes frames A to B (inclusive) to a new recording, reindexed
from zero:

```
f0      t=1000000000     entries=4    head=(0.0000, 1.6000, 0.0200)
f1      t=1011111111     entries=4    head=(0.0030, 1.6025, 0.0199)
f2      t=1022222222     entries=4    head=(0.0060, 1.6050, 0.0196)
```

## Build

Visual Studio 2022 with the C++ workload and CMake 3.28 or newer. The CMake that
ships with Visual Studio is recent enough.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The first configure downloads the OpenXR SDK headers and Catch2, each pinned.
The tests drive the real layer through a fake next layer, so no OpenXR runtime is
needed to build or test. End-to-end replay against `hello_xr` on a null runtime
is a manual check, not part of CI.

To produce the release zip:

```powershell
cpack --config build/CPackConfig.cmake -C Release -B build/package
```

## License

MIT; see [LICENSE](LICENSE). openxr-pose-layer builds against the OpenXR SDK
headers (Apache-2.0) and, for the tests only, Catch2 (BSL-1.0). No OpenXR SDK
code is compiled into the shipped DLL. Details are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
