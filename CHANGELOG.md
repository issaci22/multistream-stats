# Changelog

## 1.1.0 – first public release

Plugin build of 2026-11-09 for OBS Studio 33. The first version published on GitHub.

### Downloads for Windows, macOS and Linux
- **Windows x64:** installer (standard OBS installations and portable OBS folders, upgrade and uninstall that keep your settings) and a manual ZIP.
- **macOS 13+ universal** (Apple Silicon and Intel in one binary): DMG with an installer package, and a manual ZIP with the `.plugin` bundle.
- **Ubuntu 26.04 x86_64:** `.deb` package; other distributions: manual `.tar.xz` (not tested).
- The plugin's code is the same on every platform. The Windows packages contain exactly the DLL that passed the isolated regression and installer tests before release. See [COMPATIBILITY.md](COMPATIBILITY.md) for what has been verified on each platform.

### Only active outputs are listed (OBS's own stream always)
- OBS's own stream is always listed, in every state.
- Every other output (Aitum Multistream destinations, other plugins' outputs) is listed while it runs (Connecting, Streaming, Reconnecting, Stopping) and disappears as soon as it stops, disconnects or fails. Destinations that are configured in Aitum but not started are not listed.
- **Last failure** summary line: the most recent output that ended Disconnected or Error, with the reason and how long ago (full OBS error text in the tooltip). It is information only, not counted as an output. It disappears when that output starts again or via **Clear Last Failure** in the right-click menu.
- A failure of OBS's own stream stays in its always-listed row.
- Wording: "Live" → "Streaming"; the Outputs line no longer shows "(N total)".

### Responsive, native-style layout
- **Tall docks** keep the stacked layout.
- **Wide docks** (wider than tall): summary and OBS Performance side by side; outputs as a table with Status, Bitrate, Dropped (network), Data sent and, when it fits, Duration. Columns line up across outputs and never jump while values change. Click a line for its remaining details.
- Label/value lists flow into several columns wherever they fit.
- Collapse state is remembered separately for both layouts.
- Fixed during testing: the status line was wider than a 260 px dock; it now has a compact form ("2 live · 1 failed").

## 1.0.1 – not publicly released

### Narrow docks: values are never cut off
- The dock first opens 360 px wide (only when OBS has no saved layout for it).
- Measured values are never shortened with "…". When a value does not fit, it switches to a compact form that keeps the key number (`720 / 25142 (2.9%)` → `720 (2.9%)`; the full text is in the tooltip). Labels shorten first.
- Output headers drop the provider word first, then shorten the destination name; the header's bitrate/state is never cut off.
- The horizontal scroll bar appears only below the content's real minimum width (not reached at 260 px).

### Rendering/encoding lag counted like OBS's native Stats dock
- Counting starts when OBS finishes loading and restarts when the video settings are applied, exactly like the native dock (1.0.0 also counted frames lost while OBS was loading).
- Counters going backwards after a video reset re-baseline instead of underflowing.
- Tooltips state which counting period applies. 0 counted frames shows N/A, not 0.0%.

## 1.0.0 – not publicly released

First version.
