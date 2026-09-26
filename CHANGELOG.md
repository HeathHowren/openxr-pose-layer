# Changelog

All notable changes to openxr-pose-layer are recorded here. This project follows
[Semantic Versioning](https://semver.org/).

## [1.0.2] - 2026-09-26

### Fixed

- **A recording that cannot be written is reported.** In record mode the error
  went only to the log stream, which exists only in log mode, so it was never
  shown. The layer now prints it to stderr and to the debugger output, with the
  file path. A new test covers it.

## [1.0.1] - 2026-09-26

### Fixed

- **install.ps1 and uninstall.ps1 separate `XR_ENABLE_API_LAYERS` entries with
  semicolons.** The OpenXR loader on Windows splits that variable on `;`. 1.0.0
  joined it with `,`. When another layer was already enabled, the loader could
  not find a layer by the joined name, and `xrCreateInstance` failed with
  `XR_ERROR_API_LAYER_NOT_PRESENT`.
- **Both scripts repair a value written by 1.0.0.** They read `;` and `,`, trim
  spaces, drop empty entries, and write the list back with `;`. Run
  `install.ps1` again to fix an existing install. A new ctest check covers the
  list logic without touching the registry or the environment.

### Changed

- **The README says when a recording is saved.** The file is written only when
  the app calls `xrDestroyInstance`. If the app crashes or is killed, nothing is
  saved.

## [1.0.0] - 2026-09-26

The first release.

### Added

- **An explicit OpenXR API layer.** `xrNegotiateLoaderApiLayerInterface` and
  `xrCreateApiLayerInstance` follow the loader's layer-negotiation contract:
  the create info is copied, `nextInfo` is advanced, the next layer's create is
  called, and every downstream function is resolved through the next layer's
  `xrGetInstanceProcAddr`.
- **Three modes, chosen by environment variable or config file.** `log` writes a
  call trace with per-call timings; `record` writes each frame's poses and
  inputs to a compact binary file keyed by frame index; `replay` returns the
  recorded poses and rewrites the projection-layer view poses in `xrEndFrame` so
  the compositor agrees.
- **Interception of the pose and input calls.** `xrWaitFrame`, `xrLocateViews`,
  `xrLocateSpace`, `xrLocateSpaces`, `xrSyncActions`, `xrGetActionStatePose`,
  `xrGetActionStateBoolean`, `xrGetActionStateFloat`, `xrGetActionStateVector2f`
  and `xrEndFrame`.
- **Replay controls:** a time scale, looping, and an `x y z yawDegrees` offset
  applied to every replayed pose.
- **A recording keyed by frame index.** Frames come from `xrWaitFrame`, not from
  absolute `XrTime`, and entries within a frame are replayed in the order they
  were recorded, so nothing depends on an `XrSpace` or `XrAction` handle
  surviving a new run.
- **poseplay**, a CLI that summarizes a recording (`info`), prints per-frame
  times and head poses (`dump`), and writes a trimmed copy (`trim`), with
  `--json` output.
- **install.ps1 and uninstall.ps1**, which register the layer for the current
  user under `HKCU` and add it to `XR_ENABLE_API_LAYERS`. No administrator
  rights are needed and nothing machine-wide is touched.
- **Unit tests** that drive the real layer through an in-process fake next
  layer, with no OpenXR runtime and no headset: 40 test cases covering
  negotiation, the record format, replay lookup, the offset math, config
  parsing, and record and replay end to end.

[1.0.1]: https://github.com/HeathHowren/openxr-pose-layer/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/HeathHowren/openxr-pose-layer/releases/tag/v1.0.0
