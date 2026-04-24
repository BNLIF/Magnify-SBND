# RMS & FFT Noise Analysis

Per-channel noise characterisation integrated into the Magnify viewer.
For each wire plane (U, V, W) the tool computes:

- **Noise RMS** — percentile-based RMS with WCT-style signal masking.
- **Frequency spectrum** — magnitude FFT of the signal-removed, baseline-subtracted waveform.

Results are cached in a sidecar ROOT file (`<magnify_file>.rms.root`) and
loaded into the viewer on demand.  The "Show distribution" button opens a
7-panel canvas for exploring noise across the detector.

---

## Quick start

### Step 1 — compute the cache (batch)

From the repository root:

```bash
./scripts/run_rms_analysis.sh input_data/magnify-run.root
# Produces: input_data/magnify-run.root.rms.root
```

Multiple files can be processed in one call:

```bash
./scripts/run_rms_analysis.sh input_data/magnify-run-anode*.root
```

### Step 2 — load into the viewer

1. Launch the viewer normally (`./magnify.sh …`).
2. Click **RMS Analysis** in the control bar.
3. In the floating panel, click **Load from file**.
   The status label confirms `[LOADED+FFT]` when both RMS and FFT data are present.
4. Click **Show distribution** to open the 7-panel canvas.

Alternatively, click **Compute RMS** to re-run the analysis live from the
currently loaded histograms (writes/overwrites the cache file).

---

## RMS algorithm

The algorithm mirrors the WCT `CalcRMSWithFlags` + `SignalFilter` functions
(`Microboone.cxx:549,573`) and runs in two passes per channel:

**Pass 1 — preliminary RMS**

On the raw sample vector (excluding sentinel values ≥ 4096):

1. Find percentiles p16, p50, p84 via `std::nth_element`.
2. `rms_prelim = √( ((p84−p50)² + (p50−p16)²) / 2 )`

**Signal flagging**

Mark bins where `|ADC| > 4 × rms_prelim` and `ADC < 4096`, then expand
each mark by ±8 bins (padding).  Flag marked bins with +20 000 (sentinel).

**Pass 2 — final RMS**

Re-run the percentile RMS, skipping all sentinel values.  Result is `rms_final`.

---

## FFT algorithm

The FFT input is constructed from the same raw waveform, but signal regions
are **clamped** (not flagged with sentinel) so the input stays continuous:

1. Compute `rms0` and the median `p50` (used as the per-channel baseline).
2. Mark signal bins (same 4σ / ±8-bin criterion as above), applied to the
   **deviation** from the baseline.
3. For each sample, subtract the baseline:
   - Unflagged and non-sentinel: `out = ADC − baseline`
   - Flagged: `out = clip(ADC − baseline, ±4 × rms0)`
   - Sentinel (ADC ≥ 4096): `out = 0`
4. Apply a 1-D real-to-complex FFT (`TVirtualFFT`, option `"R2C ES K"`) to the
   `out` vector.  One FFT instance is reused across all channels of a plane.
5. For each frequency bin `k = 0 … N/2−1`:
   - `magnitude = √(re² + im²) / N`
   - `k = 0` (DC) is set to zero before storing.
6. Store into a `TH2F` (channel × frequency in MHz).

**Sampling constants** (defined in `RmsAnalyzer.h`):

| Constant | Value |
|----------|-------|
| `kTickMicroseconds` | 0.5 µs / tick |
| `kNyquistMHz` | 1.0 MHz |
| Frequency bin width | `1 / (N × 0.5 µs)` MHz (N = number of ticks, typically 3400 for SBND) |

---

## Cache file format

The cache file `<magnify_file>.rms.root` contains:

| Object | Type | Key | Content |
|--------|------|-----|---------|
| U plane RMS | TTree | `rms_u` | branches: `channel/I`, `rms_prelim/F`, `rms_final/F`, `nSignalBins/I`, `srcHist/C` |
| V plane RMS | TTree | `rms_v` | same schema |
| W plane RMS | TTree | `rms_w` | same schema |
| U plane FFT | TH2F  | `fft_u` | X = channel, Y = freq (MHz, 0–1), Z = magnitude |
| V plane FFT | TH2F  | `fft_v` | same layout |
| W plane FFT | TH2F  | `fft_w` | same layout |

Old cache files that pre-date the FFT feature contain only the three TTrees.
The viewer detects this automatically and disables the FFT bottom panels without
crashing; re-running the batch script regenerates a complete cache.

---

## RMS Analysis panel

Opened via the **RMS Analysis** button in the control bar.

| Button / widget | Action |
|-----------------|--------|
| **Compute RMS** | Run RMS + FFT on the currently loaded in-memory histograms; write cache |
| **Load from file** | Read the existing cache file; status label shows `[LOADED+FFT]` or `[LOADED]` for pre-FFT caches |
| **Show distribution** | Open the 7-panel noise canvas (see below) |
| **Overlay 4σ** checkbox | Draw ±4 × `rms_final` dashed cyan lines on the 1-D waveform pad for the currently selected channel |

---

## Distribution canvas (9 panels)

Opened by **Show distribution**.  Layout (1100 × 900 px):

```
┌───────────────┬───────────────┬───────────────┐
│  Top-left     │  Top-mid      │  Top-right    │  ← top row
│  RMS dist     │  RMS vs len   │  RMS vs len   │
│  (rms_top_dist│  U + V        │  W only       │
│   U/V/W over.)│ (rms_top_uv)  │ (rms_top_w)   │
├───────────────┼───────────────┼───────────────┤
│  Mid-U        │  Mid-V        │  Mid-W        │  ← RMS vs channel scatter
│  (rms_mid_u)  │  (rms_mid_v)  │  (rms_mid_w)  │
├───────────────┼───────────────┼───────────────┤
│  Bot-U        │  Bot-V        │  Bot-W        │  ← FFT spectrum for selected channel
│  (rms_bot_u)  │  (rms_bot_v)  │  (rms_bot_w)  │
└───────────────┴───────────────┴───────────────┘
```

**Top-left pad** (`rms_top_dist`) — stacked histogram of `rms_final` values,
one `TH1F` per plane (U = red, V = blue, W = green), auto-scaled Y axis, grid on.

**Top-middle pad** (`rms_top_uv`) — scatter plot of `rms_final` vs wire length (cm),
U (red) and V (blue) overlaid.  Wire lengths are read from the `T_geo` tree in the
input file.  If the tree is absent the pad shows an empty axis labelled
"T_geo not loaded".

**Top-right pad** (`rms_top_w`) — same scatter for the W (collection) plane only
(green).  W (collection) wires are ~constant length in SBND, so the scatter is roughly vertical.
Falls back to "T_geo not loaded" when the tree is absent.

**Middle pads** — scatter graph of `rms_final` vs global channel number,
one pad per plane.  Clicking a channel in a middle pad:
  - Updates the corresponding bottom FFT pad with the frequency spectrum of
    that channel (linear Y scale, X axis in MHz).
  - Jumps the main viewer waveform display to the clicked channel.

**Bottom pads** — frequency spectrum for the most recently clicked channel.
Initially shows an empty axis labelled "click channel above" (or "no FFT in
cache" if the cache pre-dates FFT support).  Y scale is linear.

---

## Implementation notes

| Symbol | Location | Purpose |
|--------|----------|---------|
| `calcRmsWithFlags(sig, medianOut)` | `viewer/RmsAnalyzer.cc` | Percentile RMS; optionally returns p50 as the per-channel baseline for FFT clamping |
| `signalFilter(sig, rms0)` | `viewer/RmsAnalyzer.cc` | Marks and flags signal bins with +20 000 sentinel in-place |
| `clampSignalForFft(sig, baseline, rms0, out)` | `viewer/RmsAnalyzer.cc` | Builds the baseline-subtracted, signal-clamped FFT input without modifying `sig` |
| `RmsAnalyzer::AnalyzePlaneWithFft(h, name, outFft)` | `viewer/RmsAnalyzer.cc` | Main per-plane entry point; returns `vector<ChannelRms>` and fills `outFft` |
| `RmsAnalyzer::AnalyzeFile(inFile)` | `viewer/RmsAnalyzer.cc` | Batch entry point; auto-discovers plane histograms via tag search, writes cache |
| `GuiController::fftSpec[3]` | `viewer/GuiController.h` | Owned `TH2F*` pointers to the loaded FFT histograms (nullptr if absent) |
| `GuiController::rmsMidPad[3]`, `rmsBotPad[3]` | `viewer/GuiController.h` | Named `TPad*` pointers inside `rmsDistCanvas`; addressed by name in the click handler |
| `GuiController::ProcessRmsCanvasEvent` | `viewer/GuiController.cc` | Dispatches on pad name (`rms_mid_u/v/w`); calls `TH2F::ProjectionY` to extract the FFT slice |
