# Magnify-SBND: Architecture

Magnify is a ROOT-based interactive event display for Wire-Cell signal-processing outputs on a liquid-argon TPC. It shows raw ADC waveforms, denoised waveforms, deconvoluted (calibrated) waveforms, bad-channel overlays, and per-channel thresholds for an entire readout session in a single interactive window.

The current deployment targets **SBND**: 2 TPCs × 5632 channels per TPC = 11 264 total channels, 3400 time ticks, planes U/V/W (1984 + 1984 + 1664 ch/TPC).

---

## GUI Layout

The viewer window (1600 × 900 px) is split into:

```
┌─────────────────────────────────────────────────────┐
│ pad 1: U raw (denoised)  │ pad 2: V raw │ pad 3: W raw    │  ← row 1: TH2 colz
│ pad 4: U decon           │ pad 5: V decon│ pad 6: W decon │  ← row 2: TH2 colz
│ pad 7: U 1-D waveform    │ pad 8: V 1-D │ pad 9: W 1-D   │  ← row 3: 1-D wire
├─────────────────────────────────────────────────────┤
│           Control bar (channel, tick, thresholds …)         │
└─────────────────────────────────────────────────────┘
```

- **Pads 1–6** (top two rows): `TH2 colz` — X axis = channel number, Y axis = tick. All 6 pads share a synchronised Y (time) axis; zooming in one pad propagates to the other five.
- **Pads 7–9** (bottom row): 1-D waveform for the currently selected channel.
  - Black line — denoised (post-NF).
  - Red line — deconvoluted / calibrated.
  - Blue line — raw ADC minus baseline (when "raw waveform" toggle is on).
  - Magenta horizontal line — per-channel noise threshold / 500.
  - Dashed red box / lines — time range of a bad-channel region (when "bad channel" toggle is on).
- **Control bar**: channel entry, tick entry, per-plane threshold sliders, color range (z-axis), time/ADC range, UnZoom button, mode toggles.

Clicking any TH2 pad selects the (channel, tick) at the click position, updates the channel entry, and redraws the bottom-row waveforms.

---

## Directory Tree

```
Magnify_SBND/
├── README.md                   quick-start guide
├── magnify.sh                  thin shell wrapper → scripts/Magnify.C
├── preprocess.sh               shell wrapper → scripts/preprocess.C (run 4 times)
├── data/
│   ├── badchan.txt             known bad channel list loaded at runtime
│   └── noisychan.txt           known noisy channel list loaded at runtime
├── docs/                       documentation (this directory)
├── event/                      C++ data-layer classes (ACLiC-compiled by ROOT)
│   ├── Data.{h,cc}
│   ├── Waveforms.{h,cc}
│   ├── RawWaveforms.{h,cc}
│   └── BadChannels.{h,cc}
├── viewer/                     ROOT TGFrame GUI classes
│   ├── MainWindow.{h,cc}
│   ├── ViewWindow.{h,cc}
│   ├── ControlWindow.{h,cc}
│   ├── GuiController.{h,cc}
│   └── RmsAnalyzer.{h,cc}      per-channel RMS + FFT noise analyzer
├── scripts/
│   ├── loadClasses.C           ACLiC-compiles all event/ and viewer/ sources
│   ├── Magnify.C               entry-point ROOT macro
│   ├── preprocess.C            merges per-APA histograms into whole-detector histograms
│   ├── run_rms_analysis.C      batch ROOT macro: RMS + FFT for one Magnify file
│   └── run_rms_analysis.sh     shell wrapper to run run_rms_analysis.C on one or more files
└── test_feature/
    ├── channelscan/            per-channel FFT / PNG export tool
    ├── evd/                    sub-region 2-D image export (matplotlib PDF)
    └── oscope/                 alternate single-channel ROI-stack viewer
```

---

## Source File Map

### `event/` — data layer (no Makefile; ROOT ACLiC compiles on-the-fly)

| File | Purpose |
|------|---------|
| `event/Data.{h,cc}` | Opens the preprocessed ROOT file. Loads run info (`Trun` tree), bad channels (`T_bad` tree), 6 `Waveforms` objects (U/V/W × raw/decon), 3 `RawWaveforms` objects (U/V/W raw ADC + baseline), 3 threshold `TH1I` histograms, and the `badchan.txt`/`noisychan.txt` text files. Also loads per-channel wire length from the `T_geo` + APA-suffix TTree (branches `chid/I`, `length/D` in cm) into a `wire_length` map used by the RMS distribution canvas; silently skipped when the tree is absent. Provides `GetPlaneNo(chanNo)` which maps a global channel number to plane index (0=U, 1=V, 2=W) using the HD geometry constants 2560/800/1600. |
| `event/Waveforms.{h,cc}` | Wraps one `TH2F` plane (either denoised or deconvoluted). On construction it pre-allocates a `TBox` for every (channel, tick) cell with `|content × scale| > threshold` (can be slow for large signals). Draws 5 APA-boundary `TLine` markers. Provides `Draw2D()` (colz box plot), `Draw1D(chanNo)` (1-D wire waveform), `Draw1DTick(tick)` (projection along channel axis), `DrawLines()` (bad-channel overlays). Also contains a private copy of `GetPlaneNo()` that must stay consistent with `Data::GetPlaneNo()`. |
| `event/RawWaveforms.{h,cc}` | Wraps one `TH2I` raw-ADC plane plus a matching `TH1I` baseline histogram. If no baseline histogram is present in the file, computes it from the mode of a 4096-bin (12-bit ADC) frequency histogram per channel. Provides `Draw1D(chanNo)` which subtracts the per-channel baseline and overlays sticky-code reference lines at every 64 ADC below the baseline. |
| `event/BadChannels.{h,cc}` | Reads the `T_bad` TTree (branches: `chid`, `start_time`, `end_time`) into three parallel `std::vector<int>` members: `bad_id`, `bad_start`, `bad_end`. These are used by `Waveforms` to draw vertical gray lines over bad-channel time ranges. |

### `viewer/` — GUI layer

| File | Purpose |
|------|---------|
| `viewer/MainWindow.{h,cc}` | Top-level `TGMainFrame` (1600 × 900). Contains a "File → Exit" menu bar, hosts `ViewWindow` on top and `ControlWindow` (fixed height 100 px) on the bottom. |
| `viewer/ViewWindow.{h,cc}` | `TRootEmbeddedCanvas` holding a single `TCanvas` divided into a 3 × 3 pad grid (`can->Divide(3,3,…)`). Owns the four available color palettes (Rainbow, Gray, Fire, Summer). |
| `viewer/ControlWindow.{h,cc}` | `TGHorizontalFrame` with all user-facing widgets: `channelEntry` (0–15359), `timeEntry` (0–6000), `threshEntry[3]` (per-plane threshold sliders), `zAxisRangeEntry[2]`, `timeRangeEntry[2]`, `adcRangeEntry[2]`, `threshScaleEntry`, and buttons `rawWfButton`, `badChanelButton`, `badOnlyButton`, `timeModeButton`, `setThreshButton`, `unZoomButton`. |
| `viewer/GuiController.{h,cc}` | Owns `Data`, `MainWindow`, `ViewWindow`, `ControlWindow`. Wires ROOT signal/slot connections in `InitConnections()`. Key handlers: `ThresholdChanged()` (redraw TH2 with new threshold), `SetChannelThreshold()` (use per-channel threshold TH1 × scale factor for decon pads 4–6), `ZRangeChanged()` (update color axis on all 6 TH2 pads), `ChannelChanged()` (draw 1-D denoised + decon + threshold line + optional raw + bad-region boxes in the bottom pad for the selected plane), `TimeChanged()` (draw 1-D tick projection across channels for all 3 decon planes when time-mode is on), `SyncTimeAxis(i)` (propagates Y-axis zoom from any TH2 pad to the other five), `ProcessCanvasEvent()` (translates a canvas click to (channel, tick) and triggers `ChannelChanged` + `TimeChanged`). RMS/FFT panel: `ShowRmsWindow()` (floating control panel), `ComputeRms()` / `LoadRmsFromFile()` (compute or load RMS+FFT cache), `ShowRmsDistribution()` (7-pad canvas: top = RMS histogram, middle = RMS vs channel per plane, bottom = FFT spectra per plane), `ProcessRmsCanvasEvent()` (click in a middle pad populates the bottom FFT pad for that plane and jumps the main waveform). |
| `viewer/RmsAnalyzer.{h,cc}` | Stateless noise analysis helper. `AnalyzePlane(h)` — percentile-based RMS (WCT CalcRMSWithFlags + SignalFilter algorithm). `AnalyzePlaneWithFft(h, name, outFft)` — same RMS pipeline plus per-channel FFT: signal regions clamped to ±4σ (baseline-subtracted) rather than flagged, then `TVirtualFFT` R2C (one instance reused across channels), magnitude stored into a `TH2F` (channel × frequency in MHz, DC bin zeroed). `Save/Load` overloads write/read RMS TTrees + FFT TH2Fs in a single cache file (`<file>.rms.root`); Load gracefully handles pre-FFT caches by returning null FFT pointers. `AnalyzeFile()` is the batch entry point. |

### `scripts/`

| File | Purpose |
|------|---------|
| `scripts/loadClasses.C` | ROOT macro that ACLiC-compiles all source files in `event/` and `viewer/` in dependency order (including `RmsAnalyzer.cc`), and adds both directories to the include path. Must be loaded before `Magnify.C`. |
| `scripts/Magnify.C` | Entry-point macro. Defines `Magnify(filename, threshold, frame, rebin)` which instantiates `GuiController` with a 1600 × 900 window. Default arguments: `threshold=600`, `frame="decon"`, `rebin=4`. |
| `scripts/preprocess.C` | Defines `preprocess(inPath, outDir, intag, outtag, suffix, set_baseline, file_open_mode, xmin, xmax, ymin, ymax)`. Iterates all TH2 keys in the input file whose name contains `intag` (e.g. `"hu_orig"` matches `hu_orig0`, `hu_orig1`, …), and fills a single whole-detector TH2 (`hall`) at the correct (X, Y) bin for each matched per-APA histogram. Separately handles tree merging (`tree:T_badN` → `T_bad`) and 1-D threshold histograms (`Merge1DByTag`). |
| `scripts/run_rms_analysis.C` | Headless batch macro. Calls `loadClasses.C` then `RmsAnalyzer::AnalyzeFile(inFile)` to compute per-channel RMS and FFT for one Magnify file. Output: `<inFile>.rms.root`. |
| `scripts/run_rms_analysis.sh` | Shell wrapper around `run_rms_analysis.C`. Accepts one or more Magnify ROOT files and processes them in sequence. |

### `test_feature/`

| Directory | Purpose |
|-----------|---------|
| `channelscan/` | Standalone tool (`channelscan.sh` / `channelscan.C` / `userdef.h`). Iterates a list of channels (from a text file or the `T_bad` tree), loads the merged `hu/v/w_orig` and `hu/v/w_raw` histograms, calls a user-defined `execute()` function per channel (default: FFT + PNG export). |
| `evd/` | 2-D image exporter. `preselect.C` carves a (channel, tick) sub-region from the merged histograms; `evd-subregion.py` renders a publication-quality PDF via ROOT + matplotlib. Hard-codes wire pitch (4.71 mm), tick duration (0.5 µs), drift speed (1.6 mm/µs). |
| `oscope/` | Alternative single-channel viewer (`buttonTest.C`). Reads per-APA tagged histograms directly (without preprocessing) and overlays multiple processing stages (orig, raw/denoised, wiener, gauss, various ROI stages) for one channel. |

---

## Runtime Flow

```
User
 │
 ├── ./preprocess.sh input.root [outdir] [ext]
 │       │
 │       ├── root preprocess.C(…, "orig",  "orig",      …, false, "recreate")
 │       ├── root preprocess.C(…, "raw",   "raw",       …, true,  "update")
 │       ├── root preprocess.C(…, "gauss", "decon",     …, false, "update")
 │       └── root preprocess.C(…, "threshold","threshold",…,false,"update")
 │               └── writes  <basename>-v2.root  (15360×6000 merged TH2s)
 │
 └── ./magnify.sh <basename>-v2.root [threshold] [frame] [rebin]
         │
         └── cd scripts/
             root -l loadClasses.C 'Magnify.C(file, thresh, frame, rebin)'
                     │
                     ├── loadClasses.C  — ACLiC-compiles event/ and viewer/
                     └── Magnify.C
                             └── new GuiController(root, 1600, 900, file, thresh, frame, rebin)
                                     ├── new MainWindow
                                     │       ├── new ViewWindow  (3×3 canvas)
                                     │       └── new ControlWindow
                                     ├── new Data(file, thresh, frame, rebin)
                                     │       ├── new BadChannels(T_bad tree)
                                     │       ├── load_waveform × 6   → new Waveforms × 6
                                     │       ├── load_rawwaveform × 3 → new RawWaveforms × 3
                                     │       └── load_threshold × 3  → TH1I × 3
                                     ├── Draw2D() on pads 1–6
                                     ├── Draw1D() on pads 7–9 (first channel)
                                     └── InitConnections()  — wires signal/slots
```

---

## How to Run

### Step 1: Preprocess

```bash
./preprocess.sh /path/to/wire-cell-output.root
# Output: data/<basename>-v2.root
```

Optional arguments:
```bash
./preprocess.sh /path/to/input.root /path/to/outdir v3
#                                    ^^^^^^^^^^^^  ^^
#                                    output dir    suffix (default: v2)
```

### Step 2: Launch viewer

```bash
./magnify.sh data/<basename>-v2.root
# Defaults: threshold=30, frame=decon, rebin=1

./magnify.sh data/<basename>-v2.root 500 gauss 4
# For Wire-Cell Toolkit (WCT) gauss/wiener outputs: rebin=4 gives ~1 e-/bin scale
```

Or directly:
```bash
cd scripts
root -l loadClasses.C 'Magnify.C("/path/to/file.root", 30, "decon", 1)'
```

If no filename is given, a file-open dialog appears.

### Step 3: Interactive controls

| Action | Effect |
|--------|--------|
| Click on any TH2 pad | Selects (channel, tick); redraws bottom waveform pads |
| Channel entry | Jump to channel; redraws bottom waveform |
| Tick entry + "time mode" | Draws a channel-axis projection at that tick |
| Threshold entries (U/V/W) | Redraws TH2 box overlays with new threshold |
| "ch. thresh. x" button + scale | Uses per-channel threshold TH1 × scale for decon pads |
| "raw waveform" toggle | Overlays raw-ADC (blue) on the 1-D waveform pads |
| "bad channel" toggle | Overlays gray lines on TH2 pads and dashed boxes on 1-D pads |
| "evil mode" toggle | When navigating channels, skips to next bad channel |
| **Region Sum** button | Opens the trapezoidal region-sum tool (see `docs/REGION_SUM.md`) |
| **RMS Analysis** button | Opens the RMS/FFT noise analysis panel (see `docs/RMS_FFT.md`) |
| Color range entries | Sets z-axis (color scale) min/max on all 6 TH2 pads |
| x/y range entries | Clips the 1-D waveform axis |
| UnZoom button | Resets all 6 TH2 pad axes to full range |
| Y-axis zoom on any TH2 pad | Propagates to all other TH2 pads (synchronized tick axis) |
