> ## ⚠ This project has moved — it is now **Shack-Bench**
>
> ### 👉 https://github.com/nigelfenton/shack-bench
>
> This repository is **archived and read-only**. Nothing has been lost: the
> complete history, including everything here, continues in Shack-Bench.
>
> **Why the rename.** The app outgrew its name. "TCI Monitor" described a TCI
> protocol inspector; it now also does closed-loop TX calibration and drives
> bench instruments that have nothing to do with TCI — a RigExpert antenna
> analyser, a NanoVNA, a tinySA spectrum analyser and a Hantek oscilloscope.
> TCI monitoring is still there, it is simply no longer the whole story.
>
> **Also note:** Shack-Bench is **GPL-3.0**, where this repository was MIT.
>
> Open issues were moved across before archiving.

# TCI Monitor

A lightweight diagnostic tool for the **TCI** (Transceiver Control Interface)
WebSocket protocol used by AetherSDR, ExpertSDR2, SunSDR, and similar SDR
applications.

TCI Monitor connects to a TCI server, lets you see every raw message the
server sends, and pulls structured information out of common events (VFO,
mode, spots).  Useful when you're debugging a TCI client, exploring what
events a server emits in a given workflow, or just curious about the wire
protocol.

## Download

Pre-built binaries — including a Windows installer — are on the
[**Releases page**](https://github.com/nigelfenton/tci-monitor/releases/latest):

- **Windows installer** — `TciMonitor-Setup-<version>-windows-x64.exe`
  (double-click to install)
- **Windows portable zip** — `TciMonitor-<version>-windows-x64.zip`
  (unzip and run `TciMonitor.exe`)
- **Linux AppImage** — `TciMonitor-<version>-linux-x86_64.AppImage`
  (`chmod +x` and run)
- **macOS DMG (Apple Silicon)** — `TciMonitor-<version>-macos-arm64.dmg`

Or build from source — see [Build](#build) below.

## Features

- Connect to any TCI server over WebSocket — host + port, defaults to
  `127.0.0.1:40001` (AetherSDR's TCI default; ExpertSDR2 / SunSDR use 50001)
- Real-time scrolling log of every message received, with timestamps
- Parsed views in the side panel:
  - **Current VFO / mode** — updated as the radio is tuned
  - **Spot table** — every `spot:` event the server emits, with callsign,
    frequency, mode, and source (DX cluster / parks / RBN / etc.)
  - **SWR scan** — captures an antenna SWR sweep over TCI, buckets it
    per band, overlays bands, and exports CSV
- Save the raw log to a file for offline analysis
- Filter / search the message stream, suppress flooding command types
- Auto-reconnect with backoff if the connection drops
- A **toolkit** of diagnostic tabs (see below) for deeper work

## Toolkit tabs

As of v0.3 the window is a tabbed workbench.  The **Monitor** tab is the
classic view above; the connect bar at the top is shared by the tabs
that observe the live connection.

### Inspect

A structured fold of the stream: one row per command instead of a flat
scroll.  For each command you get a live **count**, a smoothed
**rate/min**, the most-recent **value**, the reference **syntax**, and an
advisory **compliance** flag:

- `ok` — argument count looks consistent with the reference syntax
- `few args (N<M)` — fewer arguments than the syntax declares (suspect)
- `unknown cmd` — not in the built-in command reference (vendor-specific
  or new); informational, not an error

Purely passive — it never sends anything.  "Reset stats" clears the
table.

### Console

A free-text TCI command sender.  This is the **only** part of TCI
Monitor that can key your radio, so it is deliberately defensive:

- **Dry-run by default.**  The arm state is *not* remembered — it resets
  to OFF every launch.  Unarmed, a command is only logged as
  "would send", never transmitted.
- **TX-class confirm.**  `trx` / `tune` require a second explicit
  confirmation even when armed.
- **Watchdog.**  Keying a carrier starts a watchdog that auto-sends
  `trx:0,false; tune:0,false;` if SWR exceeds the configured limit or
  TX persists past the timeout.
- **UNKEY NOW.**  Always available while armed; sends the same
  unkey pair on demand.

Command-name autocomplete is driven by the built-in reference table.
Replies arriving shortly after a send are echoed into the transcript so
they line up with the command that caused them.

### Compare

Connects 2–4 **independent, read-only** observers, each with its own
host/port — so you can compare:

- **same server, N observers** — "do all clients see identical frames?"
- **cross-server** — "how do two radios' dialects differ?" (e.g.
  AetherSDR `:40001` vs SunSDR `:50001`)

The **value matrix** shows one row per command and one column per
observer; rows where observers disagree are flagged red.  A merged,
colour-coded stream log underneath gives the timeline.

### Replay

Record the live stream to a capture file, and replay any capture back
as a **local TCI server** so another client (WSJT-X, a logger, a second
TCI Monitor) can connect to it offline — no radio involved.

- **Record** writes a `.tcicap` file (format below).
- **Replay** stands up a `QWebSocketServer` and feeds the file with its
  original inter-message timing.  Speed factor scales the timing; Loop
  restarts at the end.  It also accepts a plain saved raw log
  (timestamps are stripped and a fixed gap is used).

  The server defaults to **port 40010** and binds **127.0.0.1 only** —
  deliberately *not* 40001/50001, and not the LAN, so it can never take
  a live TCI port from the real radio.  Tick **Expose on LAN** to bind
  all interfaces (for WSJT-X on another machine); choosing 40001/50001
  requires an explicit confirm.

### TX Cal

Closed-loop **WSJT-X TX-drive calibration**.  Sweeps `tx_gain` against
the radio's ALC peak meter, finds the highest setting that keeps ALC at
or below a configurable target (default −10 dBFS), and recommends a
single `tx_gain` value to apply.  The injected test tone is generated
inside the panel as TCI TX-audio frames — no external harness required.

![TX Cal panel — calibration curve and "how to apply" guidance](docs/tx-cal-curve.png)

The result is shown as a curve (forward power and ALC peak vs `tx_gain`)
alongside a step-by-step "how to apply" panel — including the exact TCI
command, the matching WSJT-X / JTDX settings, and the operator
pre-flight checks (PROC off, RN2 off, dummy load, etc.).

Same defensive model as the Console tab — armed-to-send (resets OFF
each launch), per-keydown SWR/timeout watchdog, always-live
**STOP / UNKEY NOW**.  On a live armed start the panel also auto-sets
the slice to DIGU (and restores the original mode at the end) so
AetherSDR reliably routes TCI audio via the DAX TX path regardless of
the slice's operating mode.

Every saved CSV carries:

- run timestamp
- TCI Monitor build identity (git hash, branch, dirty flag, commit
  date, compile date, host OS — baked at CMake configure time)
- AetherSDR server identity (`device:` / `protocol:` / `software:`)
- a **WARNING** block if forward power stayed at 0 W across the whole
  sweep (no carrier produced → TX audio routing problem in AetherSDR);
  the recommendation is suppressed in that case

Requires an AetherSDR carrying
[aethersdr/AetherSDR#2950](https://github.com/aethersdr/AetherSDR/pull/2950)
(the `tx_gain` TCI command and the `alc` field on `tx_sensors`); the
panel detects a missing `alc` field on the first keyed point and
refuses the run with a clear diagnostic message.

## Capture file format

A `.tcicap` file is plain UTF-8 text:

```
# tcimon-capture v1  2026-05-18T14:32:01
0	protocol:AetherSDR,0.9
12	ready
48	vfo:0,0,14074000
...
```

- The first line is a `#` comment header with the ISO-8601 capture time.
  Any line starting with `#` is ignored on replay.
- Each data line is `<delayMs>` + a TAB + the raw TCI message (no
  trailing `;` — replay re-adds it).  `delayMs` is the gap since the
  previous recorded line; the first line is `0`.

Because the format is just delay-prefixed lines, a saved raw log
(`Save log…`) also replays — its `HH:mm:ss.zzz` prefix is detected and
stripped, and lines play with a fixed default gap.

## Build

Requires Qt 6.2+ with the WebSockets module, CMake 3.20+, and a C++17
compiler.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows: `.\build.bat` does the configure + build in one shot.

## Why standalone?

Originally written to investigate what AetherSDR sends when an operator
clicks a DX-cluster spot on the panadapter — useful information that no
documentation seemed to cover.  Spinning it out as its own tool means it
stays available whenever a TCI question comes up, separate from any
particular logger or controller it might inform.

## License

MIT.  See [LICENSE](LICENSE).

73 de G0JKN / W3.
