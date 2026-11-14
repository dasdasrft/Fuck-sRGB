# Fuck sRGB (Modernized Edition)

A Windows 11 utility and tray app that corrects SDR gamma linearization when Windows HDR is active. It offers pure gamma 2.2 and 2.4 curves alongside the native Windows piecewise sRGB behavior and a linear test mode.

This repository is an enhanced, modernized fork of the original [HiBluey/Fuck-sRGB](https://github.com/HiBluey/Fuck-sRGB) project, adding a dedicated control center dashboard, real-time EOTF curve visualization, GPU crash protection, and seamless UAC-bypassing auto-start.

## Comparison with Upstream (HiBluey/Fuck-sRGB)

| Feature | Upstream (HiBluey/Fuck-sRGB v1.1.0) | Modernized Edition (v1.3.2) |
| :--- | :--- | :--- |
| **User Interface** | Basic tray right-click menu only | **Modern Dashboard Window** (KIN Design System v3.0.4) + Tray Menu |
| **Curve Visualizer** | None (pure text options) | **Real-time dynamic EOTF response curve** renderer |
| **Hardware Monitor** | Black box (no feedback) | Live monitor resolution, refresh rate, HDR color gamut, bit depth & DWM status |
| **GPU Stability** | Unprotected (rapid switches can trigger TDR/black screen) | **GPU Guard**: DWM uptime check (3s) + 3-second switching cooldown lock |
| **Auto-Start at Logon** | Not supported | **UAC-bypassing Task Scheduler logon task** with delay buffer |
| **Settings Persistence**| Resets every restart | Automatically saved in `%LOCALAPPDATA%\Fuck-sRGB\settings.ini` |
| **Process Lifecycle** | Basic process handling | Multi-session self-healing, single-instance focus wakeup, comprehensive diagnostic logs |

## Why SDR shadows look gray in HDR mode

Windows HDR uses FP16 linear scRGB for desktop composition. An ordinary SDR surface submitted as full-range sRGB is decoded with the piecewise sRGB transfer function before blending. Windows also scales SDR content to the configured SDR reference white. After composition, the display pipeline converts the result to the output format, such as PQ over an HDR10 connection. [Microsoft's Advanced Color documentation](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)

For a viewer expecting a pure 2.2 or 2.4 response, the sRGB curve maps dark nonzero code values to more light. At 2% of the input code range, sRGB gives about 0.00155 of reference white and pure gamma 2.2 gives about 0.000183. This difference can make shadows look washed out even though both curves map code zero to black and code one to reference white. The DXGI label `RGB_FULL_G22_NONE_P709` is easy to misread. Microsoft specifies the piecewise sRGB curve for that color space. [DXGI color space definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type)

## Using the tray

Download the executable from GitHub Releases and run it. The program requests administrator access to inspect and patch the Desktop Window Manager (DWM). Right-click its tray icon to choose a mode.

| Mode | Behavior on supported paths |
| --- | --- |
| **Native** | Restores the original Windows and Edge behavior for each path. |
| **Gamma2.2** | Uses the pure power curve `x^2.2`. |
| **Gamma2.4** | Uses the pure power curve `x^2.4`. |
| **Linear** | Uses `x` directly. The conspicuous change in shadows makes it easy to identify content that goes through the patch. |
| **Start with Windows** | Toggles a per-user logon task that starts the tray without a UAC prompt. |
| **Exit** | Closes the tray and stops monitoring. |

All active displays must be in HDR mode to enable the curve controls. If HDR is off, the display state is unavailable, or the desktop mixes HDR and SDR displays, only **Exit** remains available. A mode change restarts DWM and can briefly blank the screen. To undo the correction before closing the tray, choose **Native** first. **Exit** alone leaves existing in-memory edits in place until their processes exit.

### Last mode and start at logon

The tray records the curve it last applied in `%LOCALAPPDATA%\Fuck-sRGB\settings.ini` and reapplies it on the next start. A newly created DWM instance always runs the native shaders, so restoring **Gamma2.2**, **Gamma2.4** or **Linear** patches it during startup, which restarts DWM once and can briefly blank the screen. Selecting **Native** and exiting keeps the next start silent, and an unchanged selection never restarts DWM again.

### Prompts when starting the tray

The executable is not code-signed, so Windows can ask twice before the tray appears:

1. **Open File - Security Warning** ("The publisher could not be verified"). This dialog only appears for files carrying a Mark of the Web, which is what a browser download produces. A build copied from a build artifact does not carry it. If you do see it, `Unblock-File -Path .\Fuck-sRGB.exe` removes the mark, or just click **Run**.
2. **User Account Control.** The tray patches DWM, so it needs an elevated process. The manifest is `highestAvailable` and the logon entry is an `HKCU` Run value, so Windows asks once at sign-in and then starts the tray exactly as a manual launch would.

Clicking the executable a second time while the tray is already running does not start a second copy: the running instance opens its own menu so the existing tray becomes visible. Every start, exit and mode change is appended to `%LOCALAPPDATA%\Fuck-sRGB\startup.log`.

**Start with Windows** registers a Windows Task Scheduler task that runs at user logon with highest privileges, coupled with a 5-second initialization buffer. This allows the tray to start cleanly on boot without prompting for UAC approval, ensuring the notification icon and window station are ready. The configuration is mirrored in `%LOCALAPPDATA%\Fuck-sRGB\settings.ini` for persistence and recovery. Toggling the option off removes both the scheduled task and legacy registry run entries.

### Compatibility

| Component | Compatibility check |
| --- | --- |
| Windows 11 x64 DWM | Requires six verified SDR shaders. The scanner has been tested against `dwmcore.dll` **10.0.26100.9711** and **10.0.26100.9818**. |
| Microsoft Edge x64 | Requires an exact DLL SHA-256 match for **156.0.4314.50** or **156.0.4314.58**. |

The DWM check accepts relocated shaders whose contents match the six entries in the allow-list. Windows updates that replace a target shader can require a new signature. Edge compatibility applies to the additional browser patch.

## How it works

### Desktop Window Manager

The patch targets six allow-listed DXBC shaders for full-range BT.709/sRGB RGB input to scRGB, including the corresponding IA variants. The original shaders implement a piecewise curve with a linear toe and a 2.4-power branch. The patch changes the branch threshold, input offset, scale and exponent so positive SDR RGB values follow the selected power law. The shader's SDR white scaling remains in the path. BT.709 primaries and downstream composition, calibration and output conversion still have their original roles. Changing a per-channel transfer curve changes linear RGB values even with the same primaries.

At startup the program scans the read-only data section of the on-disk `dwmcore.dll`. It parses the DXBC library entries, matches the full SHA-256 of each target shader, and requires every target to appear exactly once. It then prepares the patch using the locations it found and recomputes the checksums for both the edited shaders and their enclosing libraries. When a mode changes, it checks the loaded libraries against this plan, restarts the current session's DWM, and briefly suspends the new instance while applying the verified edits to copy-on-write pages. It checks the resulting library hashes before resuming DWM. All writes target process memory.

### Microsoft Edge

Edge can linearize content in its own GPU process before handing a composed surface to DWM. For the two supported builds, the tray redirects verified Skia sRGB initialization sites to the selected transfer curve, updates the live canonical sRGB color-space object when present, and covers the sRGB branch of `SkColorSpace::MakeRGB`. The latter can be used with a separate gamut matrix, so changing the sRGB transfer function preserves the matrix used by that path. A browser-process edit selects an FP16/scRGB output path for the nominal SDR presentation cases it covers while Windows HDR is active. [Skia's color-space implementation](https://github.com/google/skia/blob/main/src/core/SkColorSpace.cpp)

The tray matches same-user browser and GPU processes, checks the exact build and expected instruction bytes, and verifies the writes. It can reapply the selected mode when an Edge GPU process is recreated. When HDR becomes inactive, the tray asks Edge to restore its native behavior. The program applies these changes in process memory while retaining the browser's hardware acceleration settings.

## Known limitations

- **Application-owned SDR linearization.** A player, image viewer, game, or color-managed renderer can decode SDR to linear light itself and submit an already-linear scRGB surface. It can also encode its result to PQ or combine SDR and HDR into a single surface. At that point DWM receives the submitted surface and its declared color space. Its pixels carry no reliable per-pixel record of the original SDR curve. For those paths, the change must happen in the application's own renderer or its settings before the content is combined. The Windows SDR brightness slider acts on reference-white scaling, a separate stage from the transfer curve. [Microsoft's guidance on matching SDR reference white](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range#match-your-apps-reference-white-to-the-os-sdr-reference-white-level)
- **Exclusive fullscreen.** A game or application in true fullscreen exclusive mode can present frames directly to the display. If that presentation path bypasses the patched DWM conversion, the selected gamma mode may have no effect on the application. [Microsoft's guide to DXGI presentation modes](https://devblogs.microsoft.com/directx/dxgi-flip-model/)
- **Video and images depend on their rendering path.** A video codec or container tag alone is insufficient to predict whether a frame will follow the tray. In a controlled Edge test, limited-range BT.709 HEVC decoded through DXVA was reported as `GAMMA22`. A 32-step, losslessly encoded gray ramp gave a measured exponent of approximately **2.197** from the FP16 output, confirming that this path already uses pure gamma 2.2. The release preserves that behavior, including in **Linear** mode. Other videos, ICC-managed images, and surfaces submitted by an application may follow different paths. [Media Foundation transfer function definitions](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/ne-mfobjects-mfvideotransferfunction)
- **HDR validation has a defined scope.** The tested native scRGB and HDR10 color patches were bit-identical in FP16 desktop captures across the DWM curve changes. HDR-only Edge test pages were also bit-identical in the tested samples. Some mixed Edge pages that paired HDR video with SDR or wide-gamut images differed by up to three FP16 steps in the sampled HDR channels. The edits target SDR transfer branches. Changes in browser composition can still affect rounding.
- **Build and process lifetime.** An unsupported DLL build is skipped. If a page retains previously created color transforms, reload it after changing a mode. If DWM restarts independently, the current implementation reads the new compositor's state, which can return to **Native**. Re-select a curve to apply it again.

## Building from source

Use an x64 MSVC developer command prompt with a recent Windows SDK. Run these commands from the directory containing `sdr-gamma-tray.cpp` and `sdr-gamma.rc`.

```bat
rc /nologo sdr-gamma.rc
cl /nologo /std:c++17 /W4 /O2 /utf-8 /EHsc /MT /Fe:Fuck-sRGB.exe sdr-gamma-tray.cpp sdr-gamma.res /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='highestAvailable' uiAccess='false'" bcrypt.lib advapi32.lib user32.lib shell32.lib gdiplus.lib dwmapi.lib gdi32.lib msimg32.lib
copy Fuck-sRGB.exe Fuck-sRGB-1.3.2.exe
```

The headers `modern-dashboard.h`, `tray-patch-plan.h`, `edge-gamma-plan.h` and `tray-settings.h` supply the modern KIN dashboard UI, DWM shader signatures, Edge patch data and the per-user settings and start-at-logon handling.

The workflow in `.github/workflows/build.yml` runs the same commands on `windows-latest` and uploads `Fuck-sRGB.exe` and `Fuck-sRGB-1.3.2.exe` as build artifacts, so a fork can be compiled without a local MSVC installation. The resource script embeds the icon, version metadata and upstream browser patch license notice.

## Acknowledgments

Thanks to [ledoge's `dwm_eotf`](https://github.com/ledoge/dwm_eotf) and [`dwm_eotf_rs`](https://github.com/SERGEYDJUM/dwm_eotf_rs) for the DWM shader-patching approach and the shader allow-list, and to [Browser Gamma Fix](https://github.com/mrsaliericz/browser-gamma-fix) for the foundation of the Edge runtime patch.

Developed by GPT.
