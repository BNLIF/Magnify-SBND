# Threshold Handling in Magnify_SBND

This document traces how the per-channel Wiener threshold flows from
the input ROOT file through every processing stage to the 2D-display
boxes, the 1D threshold line, and the GUI threshold entry widgets.

All numbers below are confirmed against a run log (anode 0, `rebin=1`).

---

## 1. Inputs from the ROOT file

| Histogram name      | Type         | Units | `wfs` index        | Description |
|---------------------|--------------|-------|--------------------|-------------|
| `hu_raw{suf}`       | TH2F         | ADC   | `wfs[0]`           | U denoised (post noise-filter) |
| `hv_raw{suf}`       | TH2F         | ADC   | `wfs[1]`           | V denoised |
| `hw_raw{suf}`       | TH2F         | ADC   | `wfs[2]`           | W denoised |
| `hu_{frame}{suf}`   | TH2F         | ADC   | `wfs[3]`           | U deconvolved (default `hu_gauss`) |
| `hv_{frame}{suf}`   | TH2F         | ADC   | `wfs[4]`           | V deconvolved |
| `hw_{frame}{suf}`   | TH2F         | ADC   | `wfs[5]`           | W deconvolved |
| `hu_threshold{suf}` | TH1I or TH1F | ADC   | `thresh_histos[0]` | Per-channel Wiener threshold (U) |
| `hv_threshold{suf}` | TH1I or TH1F | ADC   | `thresh_histos[1]` | Per-channel Wiener threshold (V) |
| `hw_threshold{suf}` | TH1I or TH1F | ADC   | `thresh_histos[2]` | Per-channel Wiener threshold (W) |

Notes:
- `{suf}` is the anode number string (e.g. `"0"`) when the file contains
  per-anode histograms, empty string otherwise.
- The deconvolved histogram bin values are in the same **ADC** units as the
  raw/denoised histograms; `fScale` (§2) converts them to display units.
- `thresh_histos` is typed as `vector<TH1I*>` in code but `load_threshold`
  also accepts TH1F; in practice the threshold bins may contain float ADC
  values (e.g. 55.37 ADC).

---

## 2. Scaling factors

| Symbol | Value (rebin=1) | Where set | Meaning |
|--------|----------------|-----------|---------|
| `raw_scale` | `1.0` | `Data.cc` | Intended anode-group gain correction (currently placeholder) |
| denoised `fScale` | `raw_scale` = `1.0` | `Data.cc:70-72` arg | Multiplier on `h*_raw` bin content |
| decon `fScale` | `1/(100·rebin/4)·raw_scale` | `Data.cc:76` arg | Converts decon ADC to display units |
| `denoised_scaling` | `0.5` | `Data.cc` / `Data::denoised_scaling` | Applied to `h*_threshold` for denoised |
| `decon_scaling` | `5.0` | `Data.cc` / `Data::decon_scaling` | Applied to `h*_threshold` for decon |
| `/500.` | literal | `GuiController.cc` | Legacy divisor for the 1D threshold line (see §6) |

**Decon fScale for common rebin values:**

| rebin | decon fScale |
|-------|-------------|
| 1 (default) | `1/25 = 0.04` |
| 4 | `1/100 = 0.01` |

---

## 3. Startup: applying per-channel thresholds

`Data.cc` constructor loops over the three planes and calls
`SetThreshold(TH1I*, scaling)` twice each — once for denoised, once for decon:

```cpp
denoised_scaling = 0.5;
decon_scaling    = 5.0;
for (int i = 0; i < 3; ++i) {
    if (thresh_histos[i]->GetMaximum() > 0) {
        wfs[i]->SetThreshold(thresh_histos[i], denoised_scaling);  // denoised
        wfs[i+3]->SetThreshold(thresh_histos[i], decon_scaling);   // decon
    }
}
```

---

## 4. Per-channel box creation: `Waveforms::SetThreshold(TH1I*, scaling)`

For every channel bin `i` and tick bin `j`:

```
step 1: channelThreshold = hThresh->GetBinContent(i) × fScale × scaling
step 2: content          = hOrig->GetBinContent(i, j) × fScale
step 3: if (!isDecon): content = |content|   (denoised takes absolute value; decon does not)
step 4: draw box at (i, j) if content > channelThreshold
```

**fScale cancels** — both sides carry it, so the comparison reduces to:

```
hOrig_ADC > hThresh_ADC × scaling
```

The threshold histogram (ADC) and signal histogram (ADC) are therefore
directly comparable regardless of `fScale`.

### Effective cutoffs at startup (rebin=1, raw_scale=1.0)

For a channel with per-channel Wiener threshold `T` ADC:

| Waveform | fScale | scaling | ADC cutoff | Display-unit cutoff |
|----------|--------|---------|------------|---------------------|
| Denoised | `1.0`  | `0.5`   | `T × 0.5`  | `T × 1.0 × 0.5 = T/2` |
| Decon    | `0.04` | `5.0`   | `T × 5.0`  | `T × 0.04 × 5.0 = T/5` |

**Verified against run log (U plane, first channel, T = 55.37 ADC):**
- Denoised display cutoff = `55.37 × 1.0 × 0.5 = 27.68` ✓
- Decon display cutoff    = `55.37 × 0.04 × 5.0 = 11.07` ✓

---

## 5. GUI threshold entry widget

### Initial value (startup)

`SetThreshold(TH1I*, scaling)` stores the mean per-channel threshold
(averaged over non-zero bins) as a representative display-unit value:

```
wfs->threshold = mean(hThresh) × scaling × fScale
```

`GuiController::InitConnections()` seeds the three plane widgets from the
*denoised* waveforms (`wfs[0..2]`):

```cpp
for (int i = 0; i < 3; i++)
    cw->threshEntry[i]->SetNumber(data->wfs.at(i)->threshold);
```

So the widget for plane U shows:

```
widget = mean(hu_threshold) × denoised_scaling × fScale_denoised
       = mean(hu_threshold) × 0.5 × 1.0
       = mean(hu_threshold) / 2
```

**Verified (U plane, mean = 95.95 ADC):**
- `stored threshold = 95.95 × 0.5 × 1.0 = 47.98` → widget displays **48** ✓

### When the user edits a threshold entry

The widget value `N` is interpreted as the **denoised display-unit threshold**
(equivalently, the denoised ADC cutoff since `fScale_den = 1.0`). Both
waveforms for that plane are updated consistently with the startup
`denoised_scaling` / `decon_scaling` ratio:

```
scalingRatio = decon_scaling / denoised_scaling = 5.0 / 0.5 = 10

denoised: ADC cutoff = N           displayThresh = N × fScale_den = N
decon:    ADC cutoff = N × 10      displayThresh = N × 10 × fScale_dec = N × 0.4
```

So entering `N = 48` gives:
- Denoised: displayThresh = 48 (unchanged from startup)
- Decon: displayThresh = `48 × 10 × 0.04 = 19.2` (matches the per-channel startup value)

This is equivalent to treating `N` as half the underlying ADC Wiener threshold
`T` (since `N = T × 0.5`), and applying both scaling formulas consistently:
`T × fScale_den × 0.5` for denoised and `T × fScale_dec × 5.0` for decon.

Note: flat mode always uses `|content|` regardless of `isDecon`, unlike
per-channel mode which preserves the sign for decon.

### "ch. thresh. x" button — per-channel reset

`SetChannelThreshold()` re-applies per-channel Wiener thresholds to all six
waveforms and refreshes the widgets:

- Denoised (`wfs[0..2]`): `scaling = denoised_scaling = 0.5`
- Decon (`wfs[3..5]`): `scaling = threshScaleEntry->GetNumber()` (user-adjustable)

This is the mechanism to return to per-channel mode after a manual edit.

---

## 6. The 1D threshold line in `ChannelChanged()`

When a channel is selected, a magenta horizontal line is drawn at:

```cpp
int thresh = thresh_histos[wfsNo]->GetBinContent(FindBin(channel));  // raw ADC
TLine *l = new TLine(0, thresh/500., nTDCs, thresh/500.);
```

**This does not use `fScale`, `scaling`, or `threshScaleEntry`.**

### Known inconsistency — the `/500` line position is wrong

The `/500` divisor is a legacy from when `decon fScale = 1/500`. The correct
display-unit positions for the current code are:

| Waveform | Correct line y | Current line y |
|----------|---------------|----------------|
| Denoised (fScale=1.0) | `T × 0.5` | `T / 500` |
| Decon (fScale=0.04, rebin=1) | `T × 0.04 × 5.0 = T × 0.2` | `T / 500` |

For `T = 55` ADC: correct denoised y = **27.5**, correct decon y = **11.0**,
current line y = **0.11** — essentially invisible on both plots.

---

## 7. Worked numerical example

Anode 0, U plane, `rebin=1`, `raw_scale=1.0`, `mean(hu_threshold) = 95.95 ADC`.

| Quantity | Calculation | Result |
|----------|-------------|--------|
| Denoised fScale | `raw_scale × 1.0` | **1.0** |
| Decon fScale | `1/(100·1/4)` | **0.04** |
| Denoised ADC cutoff (mean ch.) | `95.95 × 0.5` | **48.0 ADC** |
| Decon ADC cutoff (mean ch.) | `95.95 × 5.0` | **479.8 ADC** |
| Denoised display cutoff (mean ch.) | `95.95 × 1.0 × 0.5` | **48.0** |
| Decon display cutoff (mean ch.) | `95.95 × 0.04 × 5.0` | **19.2** |
| GUI widget (U plane, startup) | `95.95 × 0.5 × 1.0` | **48** (rounded) |
| Widget edit N=48 → denoised displayThresh | `48 × 1.0` | **48.0** |
| Widget edit N=48 → decon displayThresh | `48 × 10 × 0.04` | **19.2** |
| 1D line y (first ch., T=55.37) | `55.37 / 500` | **0.11** (incorrect) |
| Correct denoised line y | `55.37 × 0.5` | **27.7** |
| Correct decon line y | `55.37 × 0.04 × 5.0` | **11.1** |
