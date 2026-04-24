# Magnify-SBND: Input File Format

The viewer consumes a ROOT file produced in two stages: first Wire-Cell Toolkit (WCT) writes per-readout-unit (TPC) histograms, then `preprocess.sh` merges them into a single whole-detector file that the viewer actually opens.

---

## Stage A — Wire-Cell Output (per-TPC file)

This is the file you feed to `preprocess.sh`. Wire-Cell writes one or more histogram "tags" per TPC, named with the TPC index appended as a suffix (`0` … `1` for the 2 SBND TPCs).

### 2-D Histograms (TH2)

X axis = channel id (continuous across all channels in that TPC, e.g. 0–5631 for TPC 0).  
Y axis = tick number (0–3399 for 3400-tick readouts).

| Name pattern | ROOT type | Meaning |
|---|---|---|
| `hu_orig{N}`, `hv_orig{N}`, `hw_orig{N}` | `TH2I` | Raw ADC (before any noise filter); U, V, W planes of TPC N |
| `hu_raw{N}`, `hv_raw{N}`, `hw_raw{N}` | `TH2I` | Denoised waveform (after noise filter, before deconvolution) |
| `hu_gauss{N}`, `hv_gauss{N}`, `hw_gauss{N}` | `TH2F` | Deconvoluted + Gaussian-smeared charge (WCT charge measurement output) |
| `hu_wiener{N}`, `hv_wiener{N}`, `hw_wiener{N}` | `TH2F` | Deconvoluted + Wiener-filtered (WCT ROI/hit finding output) |
| `hu_decon{N}`, `hv_decon{N}`, `hw_decon{N}` | `TH2F` | Deconvoluted (Wire-Cell prototype `decon` tag) |
| `hu_baseline{N}`, `hv_baseline{N}`, `hw_baseline{N}` | `TH1I` | Per-channel baseline ADC (1-D, X axis = channel id) |
| `hu_threshold{N}`, `hv_threshold{N}`, `hw_threshold{N}` | `TH1I` | Per-channel noise threshold (1-D, X axis = channel id) |

The plane letters encode the readout plane:
- `u` → U (induction plane 1)
- `v` → V (induction plane 2)
- `w` → W/Y (collection plane)

### TTrees

| Tree name | Branches | Meaning |
|---|---|---|
| `T_bad{N}` | `chid` (int), `start_time` (int), `end_time` (int) | Bad-channel regions for TPC N. One entry per contiguous bad interval. |
| `Trun` | `runNo` (int), `subRunNo` (int), `eventNo` (int) | One entry; used for the window title. **Optional** — defaults to 0/0/0. |

### Which tags are required by `preprocess.sh`

`preprocess.sh` calls `preprocess.C` four times, looking for these input tags:

| Input tag (intag) | Output tag (outtag) | Notes |
|---|---|---|
| `orig` | `orig` | Required. Raw ADC. Baseline NOT subtracted here (set_baseline=false). |
| `raw` | `raw` | Required. Denoised. Baseline IS subtracted during merging (set_baseline=true). |
| `gauss` | `decon` | Required for the default viewer (`frame=decon`). WCT gauss output is renamed to `decon`. |
| `threshold` | `threshold` | Required for threshold overlay feature. |

The `wiener` tag (also produces deconvoluted signal) and various ROI-stage tags (`troi`, `lroi`, etc.) are commented out in `preprocess.sh` but can be enabled by uncommenting lines 48–51. The `tree:T_hm → T_bad` merge is also commented out (line 46) — see note in Stage B.

---

## Stage B — Preprocessed File (what the viewer loads)

After `preprocess.sh`, the output file (default: `data/<basename>-v2.root`) contains unified whole-detector histograms.

### Default histogram dimensions (SBND)

| Parameter | Value | Source |
|---|---|---|
| Number of channel bins | 11264 | 2 TPCs × 5632 ch/TPC |
| Channel axis range | −0.5 … 11263.5 | `preprocess.C:108-109` |
| Number of tick bins | 3400 | |
| Tick axis range | 0 … 3400 | `preprocess.C:110-111` |

These defaults can be overridden by passing `xmin`, `xmax`, `ymin`, `ymax` arguments to `preprocess.C` directly.

### 2-D Histograms Required by the Viewer

X axis = global channel id (0 … 11263).  
Y axis = tick number (0 … 3399).

| Name | ROOT type | Meaning | Loaded by |
|---|---|---|---|
| `hu_raw`, `hv_raw`, `hw_raw` | `TH2I` | Denoised waveforms (baseline-subtracted) | `Data::load_waveform` |
| `hu_decon`, `hv_decon`, `hw_decon` | `TH2F` | Deconvoluted waveforms (default frame name) | `Data::load_waveform` |
| `hu_orig`, `hv_orig`, `hw_orig` | `TH2I` | Raw ADC | `Data::load_rawwaveform` |

The deconvoluted frame name is configurable (`decon`, `wiener`, `gauss`, or any custom tag). It is passed as the `frame` argument to `magnify.sh`/`Magnify.C`:

```
./magnify.sh file.root 500 gauss 4
./magnify.sh file.root 30  decon 1
```

### 1-D Histograms (optional but recommended)

X axis = global channel id.

| Name | ROOT type | Meaning | Behaviour if absent |
|---|---|---|---|
| `hu_baseline`, `hv_baseline`, `hw_baseline` | `TH1I` | Per-channel baseline ADC | Computed on-the-fly from the mode of a 4096-bin frequency histogram over all ticks (`RawWaveforms::SetBaseline`) |
| `hu_threshold`, `hv_threshold`, `hw_threshold` | `TH1I` | Per-channel noise threshold | A dummy 4000-bin TH1I filled with zeros is substituted; threshold overlay will show zero |

### TTree

| Name | Branches | Meaning | Note |
|---|---|---|---|
| `T_bad` | `chid` (int), `start_time` (int), `end_time` (int) | Merged bad-channel regions for the whole detector | The `preprocess.sh` line that creates this (`tree:T_hm → T_bad`) is **commented out** in `preprocess.sh:46`. For bad-channel overlays to work in the viewer, either enable that line, or manually merge `T_bad0..T_bad5` and write `T_bad` into the output file. |

### External Text Files

Loaded at runtime by `Data::load_channelstatus()`. Path is relative to the `scripts/` directory:

| File | Format | Meaning |
|---|---|---|
| `data/badchan.txt` | One channel per line: `<channel_int> # <description>` | Channels known to be bad (hardware/calibration). Annotates the 1-D waveform title. |
| `data/noisychan.txt` | Same format | Channels known to be noisy. |

If either file is missing or empty, the viewer loads silently without channel annotations.

---

## Scaling Conventions

### Deconvoluted histograms

Stored in units of charge per tick (in electrons, scaled by the WCT normalisation). The viewer applies a `scale` factor on load:

```
scale = 1.0 / (500.0 * rebin / 4.0)
```

With the typical WCT convention `rebin=4`, `scale = 1/500`, so histogram bin values in units of 500 electrons are converted to electrons when drawn.

Relevant code: `event/Data.cc:46`.

### Threshold line in the 1-D waveform

The per-channel threshold (from `h{u,v,w}_threshold`) is drawn as a horizontal line at:

```
y = threshold_ADC / 500.0
```

This matches the deconvoluted waveform scale. Relevant code: `viewer/GuiController.cc:321`.

### Raw / denoised histograms

Stored in raw ADC counts (12-bit, range 0–4095 for `orig`; baseline-subtracted ADC for `raw`).  
Baseline computation uses a 4096-bin frequency histogram assuming 12-bit ADC: `event/RawWaveforms.cc:43`.  
Sticky-code reference lines are drawn at every multiple of 64 ADC below the baseline: `event/RawWaveforms.cc:73-79`.

---

## Summary Checklist for Producing a Valid Input File

To produce a file that works with the current (unmodified) viewer:

- [ ] Run Wire-Cell noise filter and signal processing, producing per-APA histograms tagged `orig{N}`, `raw{N}`, `gauss{N}` (or `decon{N}`), `threshold{N}`, `baseline{N}` for N = 0 … 5.
- [ ] Include `T_bad{N}` trees with branches `chid`, `start_time`, `end_time`.
- [ ] Optionally include a `Trun` tree with `runNo`, `subRunNo`, `eventNo`.
- [ ] Run `./preprocess.sh input.root` to merge into a 15360 × 6000 whole-detector file.
- [ ] Optionally uncomment the `tree:T_hm → T_bad` line in `preprocess.sh` (line 46) to merge bad-channel trees.
- [ ] Populate `data/badchan.txt` and `data/noisychan.txt` with detector-specific channel numbers.
- [ ] Launch with `./magnify.sh output-v2.root 500 gauss 4` (for WCT gauss) or `./magnify.sh output-v2.root 30 decon 1` (for prototype decon).
