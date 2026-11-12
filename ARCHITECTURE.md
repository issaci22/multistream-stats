# Architecture

## Overview

```
                          UI thread (Qt / OBS frontend)
 ┌───────────────────────────────────────────────────────────────────────────┐
 │ plugin-main.cpp   obs_module_load → StatsCollector + StatsDock             │
 │                   frontend events → start / discover / Aitum refresh / exit │
 │                                                                            │
 │ obs/StatsCollector (QObject)                                               │
 │   discovery timer 250 ms ── obs_enum_outputs ──► OutputTracker per output  │
 │   stats timer   1000 ms ──► drain events + sample (output-probe) ──┐       │
 │                             AitumBridge (get_outputs, ≤ every 5 s) ─┤       │
 │                             SystemStats (CPU, RAM, FPS, lag…)      ─┤       │
 │                                                                    ▼       │
 │ core/SessionRegistry  (pure C++, unit-tested)  rows, sessions, health      │
 │ core/computeSummary   Σ bitrate, Σ drops, counts, overall health           │
 │                                                                    │       │
 │                     signal snapshotReady(DockSnapshot) ────────────┘       │
 │ ui/StatsDock ─► summary · OutputSection × N · OBS Performance              │
 └───────────────────────────────────────────────────────────────────────────┘
      ▲ queued events (mutex)
 libobs threads: output signals starting/start/stop/reconnect/… → OutputTracker
```

## Source layout

| Path | Responsibility | Depends on |
|---|---|---|
| `src/core/` | Statistics logic: metrics with an N/A reason, bitrate/congestion/drop meters, the session state machine, row registry, aggregation, formatting, provider naming rules, the rendering/encoding-lag counting window (`frame-window`, 1.0.1), the display rule (`aggregate`, 1.1.0) and responsive-layout decisions (`layout-math`, 1.1.0) | C++17 only |
| `src/obs/` | libobs/frontend adapters: output discovery, signal tracking, per-output sampling, system stats, Aitum integration | libobs, frontend API, Qt Core |
| `src/ui/` | Qt Widgets dock: summary, collapsible output sections, performance grid, theme integration | Qt Widgets, core |
| `src/plugin-main.cpp` | Module entry points, dock registration, lifecycle | all |
| `tests/unit/` | doctest unit tests for `src/core` (drive the registry with synthetic observations) | core |
| `tests/mock-outputs/` | **Test-only** OBS module: simulated output types and the scripted harness (never packaged) | libobs, Qt |

The core never includes libobs or Qt headers. The adapter layer turns libobs state into plain `OutputObservation` structs (`core/observation.hpp`), so all statistics logic can be tested without OBS.

## Data collection

### Discovery

- libobs has **no "output created" signal**, so `obs_enum_outputs` is polled every 250 ms.
- The enumeration callback runs while libobs holds `outputs_mutex`, so it only compares the output pointer against already-known outputs and takes a weak reference for unknown ones.
- New outputs are inspected afterwards:
  - outputs with `OBS_OUTPUT_SERVICE` (streaming) get an `OutputTracker`;
  - all others (recording, replay buffer, virtual camera) are remembered as ignored, by weak reference.
- Dead trackers are pruned **before** each enumeration, so a destroyed output's address can never hide a new output.
- The 250 ms interval keeps the window small in which a fast-connecting output (Aitum creates and starts its output in one call) could connect before its signals are attached.
- On `OBS_FRONTEND_EVENT_STREAMING_STARTING`, discovery runs immediately. OBS's primary output is then tracked before it connects, so its `starting`/`start` signals are seen exactly.

### Classification (providers)

| Kind | Rule | Row key |
|---|---|---|
| Primary | the output equals `obs_frontend_get_streaming_output()` (checked every tick; OBS may recreate it) | `primary` |
| Aitum | name starts with `aitum_multi_output_`; the destination is the suffix | `aitum:<destination>` |
| Multiple RTMP | name equals `multi-output` (every output of that plugin has this name) | `multirtmp:<ingest host>` |
| Generic | any other streaming output | `output:<name>\|<ingest host>` |

If two live objects share a key, the later one gets `#2`, `#3`, … (shown as "(2)"). Each object is bound to exactly one row, which is why sums never double-count.

### Per-output sampling (`obs/output-probe.cpp`, once per second)

| Metric | Source | Availability rule |
|---|---|---|
| State | `obs_output_active`, `obs_output_reconnecting` + signals | see the state machine below |
| Bitrate | Δ `obs_output_get_total_bytes` over a 2 s window | N/A until 2 samples |
| Average bitrate | session bytes / time since connect | N/A if the start time is unknown |
| Dropped / total frames | `obs_output_get_frames_dropped`, `obs_output_get_total_frames` | dropped is shown only for types known to implement it (`rtmp_output`) or after a non-zero value proves support; otherwise N/A, because libobs returns 0 for unimplemented callbacks |
| Congestion | `obs_output_get_congestion`, smoothed like the OBS status bar | same capability rule |
| Connect time | `obs_output_get_connect_time_ms` | −1 → N/A (not supported); 0 is trusted only after the session has connected |
| Resolution | `obs_output_get_width/height` | |
| FPS | `video_output_get_frame_rate(obs_encoder_video(enc))` / `obs_encoder_get_frame_rate_divisor` | |
| Encoders | `obs_encoder_get_display_name/codec/settings` (configured target bitrate) | labelled as configuration, not measurement |
| Encoder lag | `video_output_get_skipped_frames/total_frames(obs_encoder_video(enc))` | **only** if that video pipeline is not the main one; the main pipeline's lag is shown once, under OBS Performance |
| Canvas | `obs_encoder_parent_video` vs `obs_get_video()` / `obs_enum_canvases` | |
| Server | host part of `obs_service_get_connect_info(SERVER_URL)` | the stream key is never requested |
| Delay | `obs_output_get_active_delay` | shown only when > 0 |

Capability table (verified in OBS 33.0.1 source):

| Output type | dropped | congestion | connect time |
|---|---|---|---|
| `rtmp_output` | ✔ | ✔ | ✔ |
| `whip_output` | ✘ | ✘ | ✔ |
| `ffmpeg_mpegts_muxer` (SRT/RIST) | ✘ | ✘ | ✘ |

### Session counters

`rtmp_output` resets its byte and drop counters on every (re)connect. `CounterAccumulator` folds each reset into an offset, so session totals stay monotonic across reconnects. This was verified with the mock output, which resets on every reconnect, and with real RTMP reconnects.

### State machine (`core/session-registry.cpp`)

```
          starting/activate           start / poll: active
 Idle ───────────────────► Connecting ─────────────────────► Live ◄──────┐
  ▲                            │                              │ reconnect │ reconnect_success
  │  (Aitum object without     │ stop(code)                   ▼           │
  │   any signal for 30 s)     ▼                        Reconnecting ─────┘
  └──────────────────── Stopped / Disconnected / Error ◄── stop(code) ── Live / Stopping
```

- **Stop codes:**
  - `OBS_OUTPUT_SUCCESS` → Stopped;
  - `OBS_OUTPUT_DISCONNECTED` → Disconnected;
  - anything else → Error, with `obs_output_get_last_error` captured inside the `stop` callback.
- **Race protection:**
  - libobs emits `stop` before its deactivate thread clears `active`, so after any signal has been seen, polling alone never starts a new session for that object;
  - a live session with no `stop` signal ends only after 2 inactive polls.
- **Start time:** taken exactly from the `start` signal. If the output was discovered already live, it is "detected" (within 0.25 s). If it was live before monitoring began, it is "unknown", and the duration and average bitrate then show N/A.
- **Aitum inference:** Aitum creates an output only when a destination is started (observed in Aitum's source and in testing), so a new inactive Aitum object is shown as Connecting. If no signal arrives within 30 s, this inference expires.
- **Finished sessions:** their final values (bytes, drops, duration, error) are kept and shown muted as "last session". Final counters are read inside the `stop` callback, because Aitum releases the object right after it.
- **Retention (registry):**
  - configured Aitum destinations and the primary output always have a row;
  - unconfigured finished rows are kept, up to 16, until cleared;
  - Aitum rows that are no longer in Aitum's configuration (renamed or deleted) disappear once they are not live.
- **Display (1.1.0, `core/aggregate.cpp` `isRowDisplayed`):** the registry keeps the rows above (needed for restarts, ordering and failure reasons), but `StatsCollector::publish` passes only displayed rows to the dock and the summary:
  - the primary row always;
  - any other row while its session runs (Connecting / Live / Reconnecting / Stopping); terminal states (Stopped / Disconnected / Error) are not listed.
  - `lastFailure()` picks the non-primary row that most recently ended Disconnected / Error (by `RowSnapshot::endNs`) and has not been restarted or cleared; it goes into `DockSnapshot::lastFailure` and the summary's "Last failure" line (Role::Text, may elide). Not counted in the summary. "Clear Last Failure" = `SessionRegistry::clearEnded()`.
  - The debug tick log prints every registry row with `shown=0|1`, and `last_failure=` on the summary line.

### Aitum integration (`obs/aitum-bridge.cpp`)

- The only integration point beyond standard outputs is Aitum's **documented obs-websocket vendor request**:
  - call: `CallVendorRequest` → vendor `aitum-multistream`, request `get_outputs`;
  - made in-process through obs-websocket's public C API (`obs-websocket-api.h`, request status 100 = success);
  - returns `[{name, vertical}]` for the current profile.
- When it is called:
  - after loading;
  - on profile change;
  - when the number of live Aitum outputs changes;
  - every 5 s while the dock is visible.
- It runs on the UI thread. Aitum 1.0.9's `get_outputs` is a plain synchronous read (verified in its source). Its `start_output`/`stop_output` block on the UI thread, and the dock never calls them.
- If obs-websocket or Aitum is missing, the dock falls back to showing Aitum destinations only while they run.

### OBS-wide statistics (`obs/system-stats.cpp`)

These are the same calls as `OBSBasicStats`:
- `os_cpu_usage_info_query`;
- `os_get_proc_resident_size`;
- `os_get_free_disk_space(record path)`, every 10 s;
- `obs_get_active_fps` vs `obs_get_video_info`;
- `obs_get_average_frame_time_ns`;
- rendering lag: `obs_get_lagged_frames / obs_get_total_frames`;
- encoding lag: `video_output_get_skipped/total_frames(obs_get_video())`.

**Counting window for rendering and encoding lag (1.0.1, `core/frame-window.hpp`):** the same rules as `OBSBasicStats` in OBS 33.0.1.

| Event | Native Stats dock | This dock |
|---|---|---|
| Startup | `InitializeValues()` in `OBSBasic::OBSInit()` (line 1185), after the scene collection has loaded | `SystemStats` is constructed in the `OBS_FRONTEND_EVENT_FINISHED_LOADING` handler. OBS sends that event from the same synchronous `OBSInit()` call (line 1252, via `OnFirstLoad()`), so both windows start within about one frame. |
| Video settings applied | `ResetVideo()` → `InitializeValues()` | The public libobs global signal `video_reset` (`obs_get_signal_handler()`), emitted inside `obs_reset_video()`. The callback only records the counters under a mutex; the next UI-thread sample applies them. Disconnected in `~SystemStats`, which waits for an in-flight callback. |
| Reset | `Reset()`: the next update becomes the baseline | Our Reset button baselines immediately. The native dock is not affected, and our tooltip says so. OBS's native "Reset stats" hotkey has no public event, so it cannot reset our window; the two resets stay independent. |
| Counter goes backwards (new `video_t`) | re-baseline at the current values | the same (`FrameWindow::sample`). 1.0.0 underflowed here after a Reset. |
| 0 frames counted | shows `0.0%` | percentage **N/A** (never a fabricated 0) |

The tooltip names the window that is currently in use: "since OBS finished loading", "since the video settings were last applied", or "since this dock's statistics were reset". Each new baseline is logged once (`lag window: baseline (<reason>) rendered=… lagged=…`).

Thresholds match OBS:
- percentages: warning > 1 %, danger > 5 %;
- FPS: warning < 95 %, danger < 80 % of the target;
- render time: warning > 75 % of the frame interval, danger > 100 %;
- congestion: warning > ⅓, danger > ⅔.

## Threading and lifetime

- **All work runs on the UI thread** except signal callbacks.
- **Signal callbacks** (libobs threads):
  - only timestamp the event and append it under a small per-tracker mutex;
  - never block, never call Qt, never take another lock.
  - `stop` additionally reads the emitting output's counters and last error; the output is alive during its own signal.
- **No lifetime extension:** a tracker holds only a **weak reference**. Strong references are taken for the duration of one sample and released immediately, so the dock never keeps an output alive (Aitum releases its outputs on stop).
- **Signals are connected with `signal_handler_connect_ref`.** The output's signal handler then stays valid until the dock disconnects, even if the output has already been destroyed.
- **Disconnecting is safe:**
  - `signal_handler_disconnect` holds the per-signal mutex, so it waits for an in-flight callback to finish;
  - a tracker is destroyed only after it is disconnected;
  - for a destroyed output, the tracker disconnects first and then drains its last events, so a late `stop` is never lost.
- **Shutdown:**
  - `obs_shutdown` frees modules **before** destroying leftover outputs.
  - On `OBS_FRONTEND_EVENT_EXIT`, and again idempotently in `obs_module_unload`, the collector stops its timers, disconnects every output signal, releases every weak reference and is deleted. No libobs callback can reach the DLL after it unloads.
- **The dock widget is owned by OBS** (`obs_frontend_add_dock_by_id`). It holds only a `QPointer` to the collector, and the Qt connection drops automatically.

## UI

- **Layout:** the native Stats dock's two-column label/value grid inside a scroll area:
  - summary at the top;
  - one `OutputSection` per destination: a disclosure triangle, a status square, the title, a muted provider name and the bitrate or state on the right;
  - **OBS Performance** at the bottom;
  - a native-style **Reset** button.
- **Colors:** never hard-coded. Widgets get OBS theme classes through a re-implementation of OBS's `setClasses()`. The status squares and triangles are painted with the widget's themed palette.
- **Narrow docks (1.0.1):**
  - **`ElidedLabel` roles:**
    - **Value** — measured numbers (bitrate, frames, %, durations, data, resolution/FPS, connect time, all OBS Performance values, the header bitrate/state). Never elided. Shows its full text or a **compact form** with the key number: `720 / 25142 (2.9%)` → `720 (2.9%)`, `6012 kb/s (avg 5998 kb/s)` → `6012 kb/s`, `1280x720 · 30.00 fps · Main canvas` → `1280x720@30.00`, `connected in 149 ms` → `149 ms`, `60.00 / 60.00` → `60.00`, status → the state only.
    - **Label** — captions; elide first.
    - **Text** — encoder names, host, errors; full, then compact, then elided.
    - Whenever less than the full text is shown, the full text is added to the tooltip.
  - **`StatGrid` lays out its rows itself.** `QGridLayout` takes missing width equally from both columns, which squeezed values while long labels stayed full. Order of preference:
    1. natural label width with full values;
    2. labels shrink to about 40 % of the grid to keep full values;
    3. values switch to compact form;
    4. labels shrink to a few characters.
  - The grid's minimum width includes every visible value's compact width. Below that, the scroll area scrolls horizontally instead of cutting values off.
  - **Output header:** the provider word ("Aitum", "Primary") is hidden first (it is in the title tooltip); then the name elides. The bitrate/state is never cut off.
  - **Default floating width: 360 logical px** (`StatsDock::sizeHint`). OBS floats a newly added plugin dock at its size hint (`OBSStudioAPI::obs_frontend_add_dock_by_id`). Saved layouts are restored by OBS unchanged.
  - **Probes for the isolated UI tests:** read-only properties `msdRole` / `msdMode` / `msdFullText` / `msdShownText` on `ElidedLabel`, plus object names `msd-disclosure`, `msd-perf-render-lag` and `msd-perf-encode-lag`.
- **Repaint cost:**
  - Size hints only grow, so a value changing every second causes a repaint, not a re-layout.
  - Style spacing is cached, because with OBS's style-sheet themes each `pixelMetric()` resolves style rules.
- **Responsive layout (1.1.0):** decided in `StatsDock::updateResponsiveLayout` on every viewport resize and snapshot, from measured text widths; the decisions are pure functions in `core/layout-math.cpp`.
  - *Shape:* wide when viewport width > height. Entering a roomier layout needs one scroll-bar width (`PM_ScrollBarExtent`, cached) more than staying in it, so a scroll bar appearing or disappearing can't flip the layout.
  - *Tall:* the content `QGridLayout` stacks Summary → outputs → OBS Performance (1.0.x order).
  - *Wide:* Summary (natural width) | OBS Performance (rest, up to 3 grid columns) on top, outputs below, when both fit.
  - *Output table (wide only):* `planTable` picks the most of Status, Bitrate, Dropped, Data sent, Duration that fit (first four required) next to a name column (natural width, may elide to ~10 characters). `OutputSection::setTableGeometry` gives the header's name box and cells fixed widths; `StatsDock` shows a bold column-header row built the same way, so columns line up. Column widths are the widest cell or header ever seen (grow-only `ElidedLabel` hints), so values changing every second don't move columns. Detail rows already shown as columns are hidden in the body (Bitrate stays: it adds the average).
  - *`StatGrid` columns:* as many label/value pairs as fit at natural width with full values (descriptive text counted up to 24 characters, also in `sizeHint`), up to `setMaxColumns` (perf 2, or 3 side by side; output details 2 stacked, 3 in the table), filled column by column; columns packed at their own width. Height via `heightForWidth` (cached per width); `minimumSizeHint` height is one row so the scroll area uses the real height. With one column the 1.0.1 algorithm is unchanged.
  - Test probes on the dock root: `msdLayoutMode`, `msdTableColumns`, `msdOverviewSideBySide`, `msdWide`.
- **Collapse state:** remembered per destination in the plugin's own config file, separately per layout (`collapsed` for stacked, where sections default to expanded; `expanded_table` for the table, where lines default to collapsed).
- **Never-run destinations:** shown as a single header line (only the OBS stream can be listed without a session since 1.1.0).
- **Visibility:** the collector always samples (session counters must stay correct), but OBS-wide stats and the snapshot/UI refresh run only while the dock is visible.

## Performance (measured in the isolated OBS, RelWithDebInfo)

| Part | Cost |
|---|---|
| Collection: discovery, sampling, registry; 4–6 outputs | avg ≈ 35 µs per tick (1 Hz), max 0.13 ms |
| Dock refresh while visible | 1.0.1: median 0.35–0.72 ms, p90 0.5–5.7 ms. 1.0.0: median 0.34–0.50 ms, p90 2.6–10.6 ms. |
| Dock refresh spikes | 1.0.1: 31–48 ms; 1.0.0: 25–31 ms. Only when the dock is first shown or sections are created (outputs appearing); 30 outputs created/destroyed in 4.5 s during the churn test |
| Discovery | pointer walk of the output list, 4 Hz; negligible |
