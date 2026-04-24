# Magnify_SBND: What Changed from Magnify_PDHD

This document records what was changed from `Magnify_PDHD` to establish `Magnify_SBND`.
The original PortoDUNE-HD–specific assumption checklist (originally written for a VD port) was used
as the authoritative guide; all items in that checklist have been addressed here.

SBND geometry used (verify against your WCT magnify ROOT file):
- 2 TPCs, 5632 ch/TPC (1984 U + 1984 V + 1664 W), 11264 total
- 3400 ticks, 12-bit ADC, sticky-code period 64
- Wire pitch 3.0 mm, tick 0.5 µs, drift 1.6 mm/µs

All geometry constants are now centralised in `event/Geometry.h`.
Naming: `apaNo` → `tpcNo` throughout (including `load_geometry()`); Trun branch lookup tries `tpcNo`, `anodeNo`, `apaNo` in order.
SBND WCT signal processing uses a DNN backend — histograms are tagged `dnnsp` instead of `gauss`/`wiener`. The viewer falls back to `dnnsp` automatically; `preprocess.sh` includes a `dnnsp → decon` merge step alongside the traditional `gauss → decon` step.

---

## 1. Per-APA/CRU Channel Block Size: 2560, 800, 1600, 960

**What it is**: ProtoDUNE-HD has 2560 channels per APA, split 800 U + 800 V + 960 W. `GetPlaneNo()` uses these numbers to map a global channel id to a plane index. The APA boundary-line drawing also uses 2560.

**Locations**:

| File | Lines | Code | Meaning |
|---|---|---|---|
| `event/Data.cc` | 106–111 | `chanNo / 2560`, `offset < 800`, `offset < 1600` | `GetPlaneNo()` — U/V/W boundary logic |
| `event/Waveforms.cc` | 259–270 | Same logic, second copy | `Waveforms::GetPlaneNo()` — must stay in sync with `Data::GetPlaneNo()` |
| `event/Waveforms.cc` | 71 | `new TLine(2560*(i+1), 0, 2560*(i+1), 6000)` inside `for i=0..4` | Draws vertical boundary lines between APAs in the TH2 display |
| `test_feature/oscope/buttonTest.C` | 181–184, 216 | `ch % 2560`, `< 800`, `< 1600`, `ch / 2560` | Same plane/APA arithmetic in the standalone oscope viewer |

**For VD**: Identify the VD channel-block unit (CRU or CRP), determine U/V/Y wire counts per unit, and update all four locations. See also §3 (number of units) and §2 (total channel count).

---

## 2. Total Channel Count (15 360) and Tick Count (6000)

**What it is**: 6 APAs × 2560 = 15 360 total channels; 6000 ticks per event.

**Locations**:

| File | Lines | Value | Context |
|---|---|---|---|
| `scripts/preprocess.C` | 108–111 | `xmax=15359.5`, `ymax=6000` | Default arguments to the preprocess function; set the merged-histogram axis range |
| `viewer/ControlWindow.cc` | 25 | `0, 15359` | Channel entry widget hard max |
| `viewer/ControlWindow.cc` | 55 | `0, 6000` | Tick entry widget hard max |
| `viewer/ControlWindow.cc` | 90 | `0, 10000` | Time-range entry max (generous upper bound for channel-axis in time-mode; probably fine as-is but worth reviewing) |
| `viewer/ControlWindow.cc` | 100, 113 | `−1000, 1000` (ADC); `−100, 100` (z/color) | Range widget bounds; detector-independent, likely fine |
| `event/Waveforms.cc` | 71 | `0, 6000` in `TLine(…, 0, …, 6000)` | APA boundary line Y extent |
| `test_feature/evd/preselect.C` | 6–8 | `x_end_bin=2560`, `y_end_bin=6000` | Sub-region end bin for the EVD image tool |

**For VD**: Replace 15 360 with VD total channel count, 6000 with VD tick count. The `preprocess.C` default arguments can be overridden on the command line, so the most important fix is the `ControlWindow` widget limits.

---

## 3. Six-APA Assumption (loops and tree merge)

**What it is**: The preprocess script explicitly loops over `T_bad0` … `T_bad5` (6 trees). The APA-boundary line drawing loop runs 5 iterations (drawing 5 dividers for 6 blocks).

**Locations**:

| File | Lines | Code | Meaning |
|---|---|---|---|
| `scripts/preprocess.C` | 128–139 | `for (int i=0; i<6; i++)` over `T_bad{i}` | Merges bad-channel trees; hard-coded to 6 APAs |
| `event/Waveforms.cc` | 70–74 | `for (int i=0; i!=5; i++)` | Draws 5 APA boundary lines (= 6 blocks − 1) |
| `event/Waveforms.cc` | 194–196 | `for (int i=0; i!=5; i++)` | Draws the boundary lines again in `Draw2D()` |
| `viewer/GuiController.cc` | 54, 58, 131, 139, 153, 166, 184, 196, 213 | `for (int i=0; i<6; i++)` or `for (int ind=0; ind<6; ind++)` | Iterates over the 6 `Waveforms` objects (U/V/W × raw/decon) — this is 3 planes × 2 histogram types, not 6 APAs; correct as long as the plane count stays at 3 |

**Clarification on the `ind < 6` loops in `GuiController`**: These iterate over 6 `Waveforms` objects that represent 3 planes × 2 display types (raw and decon). They are NOT iterating over APAs. They do not need to change if VD still has 3 readout planes.

**For VD**: Update `preprocess.C:132` loop limit to the VD number of CRUs/CRPs. Update `Waveforms.cc:70` loop to `(N_BLOCKS − 1)` where `N_BLOCKS` is the VD block count.

---

## 4. Plane Count = 3 and Plane-Letter Encoding (U/V/W → u/v/w)

**What it is**: The three planes are named U, V, W throughout. Histogram names use the letters `u`, `v`, `w`. The decon histograms are loaded with a `Form("h%c_%s", 'u'+iplane, frame)` trick that generates `hu_…`, `hv_…`, `hw_…` for `iplane` 0, 1, 2.

**Locations**:

| File | Lines | Code | Meaning |
|---|---|---|---|
| `event/Data.cc` | 44–47 | `Form("h%c_%s", 'u'+iplane, frame)` for `iplane=0..2` | Constructs decon histogram names |
| `event/Waveforms.cc` | 35–37 | `name.Contains("hu")` / `name.Contains("hv")` | Sets `planeNo` (0=U, 1=V, 2=W) from histogram name |
| `viewer/ViewWindow.cc` | 35 | `can->Divide(3, 3, …)` | **9-pad layout** hardwired to 3 planes × 3 rows |
| `viewer/ControlWindow.cc` | 76–78 | Group titles "U Plane", "V Plane", "W Plane" | UI labels for the 3 per-plane threshold groups |
| `viewer/GuiController.cc` | 100–103 | Bad-channel tooltip: "U: … V: … Y: …" (note: typo in current code — says "Y" instead of "W") | Informational only |
| `test_feature/oscope/buttonTest.C` | 181–216 | Same plane arithmetic | Oscope plane detection |

**For VD**: ProtoDUNE-VD likely has 3 readout planes (X, U, Y in some conventions), so the 3 × 3 pad grid and the 6-wfs loop probably survive. However:
- The `'u'+iplane` character arithmetic breaks if VD uses letters outside `u/v/w` (e.g. `x`, `y`). Replace with an explicit array of plane-name strings (see §12).
- The `name.Contains("hu"/"hv")` branching in `Waveforms.cc:35-37` must match the actual histogram prefix used in the VD WCT output.
- Update the "U Plane / V Plane / W Plane" UI labels in `ControlWindow.cc:76-78`.

---

## 5. Dummy Fallback Numbers for Missing Histograms (stale MicroBooNE remnants)

**What it is**: When a requested histogram is not found in the ROOT file, `Data.cc` creates a dummy empty histogram with hard-coded MicroBooNE-style dimensions (2400 U channels, 2400 V channels starting at channel 2400, 3456 W channels starting at channel 4800). These do not match ProtoDUNE-HD geometry and will not match VD geometry either.

**Locations**:

| File | Lines | Dummy values | Context |
|---|---|---|---|
| `event/Data.cc` | 159–169 | `nChannels=2400`, `nTDCs=6000`, `firstChannel=0` (U); `firstChannel=2400` (V); `firstChannel=4800`, `nChannels=3456` (W) | `load_waveform()` fallback (creates a `TH2F`) |
| `event/Data.cc` | 185–196 | Same values | `load_rawwaveform()` fallback (creates a `TH2I`) |
| `event/Data.cc` | 232 | `TH1I(name, "", 4000, 0, 4000)` | `load_threshold()` fallback — 4000-bin dummy |

**For VD**: Either remove the dummy path entirely (throw an error if a histogram is missing, which is more useful for debugging) or replace the fallback dimensions with VD values. The 4000-bin threshold dummy (line 232) should be at least as wide as the total channel count.

---

## 6. 12-Bit ADC Assumption

**What it is**: Baseline computation and sticky-code reference lines assume 12-bit ADC (0–4095 range, repeating-code period of 64).

**Locations**:

| File | Lines | Value | Meaning |
|---|---|---|---|
| `event/RawWaveforms.cc` | 43 | `TH1I hf("hf","Bin Frequency",4096,0,4096)` | Frequency histogram used for baseline mode computation; must span full ADC range |
| `event/RawWaveforms.cc` | 73–79 | `(baseline−i)%64 == 0` over i=0..63 | Draws sticky-code lines at every 64-ADC boundary below baseline |
| `scripts/preprocess.C` | 35 | `TH1I("baseline","baseline", 4096, 0, 4096)` | Same frequency histogram in the preprocess baseline computation |

**For VD**: Verify the VD electronics ADC bit depth. DUNE VD cold electronics (CRP-based readout) may use a different ADC range (e.g. 14-bit = 0–16383, sticky code period potentially different). Update the histogram range and the modulus accordingly.

---

## 7. Wire-Cell Electron Scaling (500 e⁻/bin at rebin=4)

**What it is**: WCT deconvoluted histograms store charge in units of 500 electrons/bin when produced with `rebin=4`. The viewer applies `scale = 1/(500·rebin/4)` to convert to electrons.

**Locations**:

| File | Line | Code |
|---|---|---|
| `event/Data.cc` | 46 | `1./(500.*rebin/4.0)` |
| `viewer/GuiController.cc` | 321 | `thresh/500.` |

**For VD**: This normalisation is set by the WCT configuration, not the detector geometry. Confirm the VD WCT job uses the same 500 e⁻/bin convention. If a different normalisation is used, update both lines.

---

## 8. Histogram Naming Prefix (hu\_, hv\_, hw\_) and DNN Tag

**What it is**: Every histogram consumed by the viewer and the test tools is named `h{plane}_{tag}` where `{plane}` ∈ {u, v, w}.

**SBND-specific**: SBND WCT jobs use DNN signal processing and write `dnnsp` as the tag instead of the traditional `gauss` or `wiener`. The viewer handles this via a fallback chain in `event/Data.cc`:

```
requested frame  →  gauss  →  dnnsp
```

If `hu_decon0` and `hu_gauss0` are absent but `hu_dnnsp0` is present, the viewer loads the DNN histogram for all frame arguments (`decon`, `wiener`, `gauss`). A console message is printed for each fallback.

`preprocess.sh` includes both a `gauss → decon` and a `dnnsp → decon` merge pass. Only the one that finds matching histograms in the input file produces output; both coexist safely.

**All consumers**:

| File | Usage |
|---|---|
| `event/Data.cc:50-64` | `Form("h%c_%s", 'u'+iplane, frame)` for the decon frame, with fallback to `gauss` then `dnnsp` |
| `event/Data.cc:71-73` | `load_waveform("hu_raw", …)` — explicit strings for raw |
| `event/Data.cc:80-82` | `load_rawwaveform("hu_orig","hu_baseline")` — explicit strings |
| `event/Data.cc:84-86` | `load_threshold("hu_threshold")` — explicit strings |
| `event/Waveforms.cc:35-37` | `name.Contains("hu")` / `name.Contains("hv")` — plane detection |
| `scripts/preprocess.C:163-200` | `Form("hu_%s", outtag)` — creates output histogram names |
| `scripts/preprocess.C:168-170` | `Merge1DByTag(f1, hu, Form("hu_%s", intag))` — searches input file for histograms containing the tag string |
| `test_feature/channelscan/channelscan.C:59-69` | `hu_orig`, `hv_orig`, etc. — explicit names |
| `test_feature/oscope/buttonTest.C` | `hu_orig0`, `hu_raw0`, `hu_wiener0`, etc. — explicit names with TPC suffix |

---

## 9. Drift / Wire-Geometry Constants (EVD tool only)

**What it is**: The `evd-subregion.py` matplotlib export tool uses physical constants to set the correct aspect ratio.

**Location**:

| File | Line | Values |
|---|---|---|
| `test_feature/evd/evd-subregion.py` | 74 | `nBinsX*4.71 / (nBinsY*y_rebin*0.5*1.6)` → wire pitch 4.71 mm, tick 0.5 µs, drift speed 1.6 mm/µs |

**For VD**: Update with VD wire pitch, tick duration, and drift velocity. These are used only for the figure aspect ratio — not for the interactive viewer itself.

---

## 10. Data Text Files (channel lists)

**What it is**: `data/badchan.txt` and `data/noisychan.txt` contain ProtoDUNE-HD channel numbers (currently ranging up to ~13 000) with descriptions. They are loaded at startup and annotate the 1-D waveform title when the selected channel is listed.

**For VD**: Replace both files with VD-specific bad/noisy channel lists. Format is unchanged: one line per channel, `<channel_int> # <description>`.

---

## 11. Commented-Out T_bad Tree Merge in preprocess.sh

**What it is**: Line 46 of `preprocess.sh` is commented out:

```bash
# root -l -b -q preprocess.C+'("…", "…", "tree:T_hm", "T_bad", "…")'
```

This means the merged `T_bad` tree is **never written** to the output file by the current workflow. Without it, bad-channel overlays in the viewer are empty (the `T_bad` tree from the file is loaded in `Data.cc:35` but if it is missing, `BadChannels` receives a null pointer and produces no overlays).

**For VD**: Uncomment and adjust line 46 (replacing `T_hm` with the actual bad-channel tree name from the WCT output, or using `T_bad` directly if WCT writes per-APA `T_bad0` … trees). Alternatively, produce and write `T_bad` directly from the WCT job output.

---

## 12. Suggested Porting Order

1. **Gather VD geometry numbers** from the ProtoDUNE-VD detector design document:
   - Number of CRUs/CRPs (call it `N_BLOCKS`)
   - Channels per block, broken down by plane (`N_CH_U`, `N_CH_V`, `N_CH_W` per block)
   - Total channels = `N_BLOCKS × (N_CH_U + N_CH_V + N_CH_W)`
   - Number of time ticks (`N_TICKS`)
   - ADC bit depth and sticky-code period
   - Wire pitch, tick duration, drift speed (for the EVD tool)

2. **Create `event/Geometry.h`** with named constants for all of the above. Example skeleton:
   ```cpp
   // event/Geometry.h — VD geometry parameters
   static const int N_BLOCKS       = ???;   // number of CRUs/CRPs
   static const int N_CH_U         = ???;   // U wires per block
   static const int N_CH_V         = ???;   // V wires per block
   static const int N_CH_W         = ???;   // W/Y wires per block
   static const int N_CH_PER_BLOCK = N_CH_U + N_CH_V + N_CH_W;
   static const int N_TOTAL_CH     = N_BLOCKS * N_CH_PER_BLOCK;
   static const int N_TICKS        = ???;
   static const int ADC_BITS       = ???;   // e.g. 12 → range 0..4095
   static const int STICKY_PERIOD  = ???;   // e.g. 64 for 12-bit
   // Physical (for EVD only)
   static const double WIRE_PITCH_MM = ???;  // e.g. 4.71
   static const double TICK_US       = ???;  // e.g. 0.5
   static const double DRIFT_MM_PER_US = ???; // e.g. 1.6
   // Plane name strings (for histogram prefix)
   static const char* PLANE_NAMES[] = {"u", "v", "w"}; // adjust if needed
   ```

3. **Replace hard-coded constants** using the checklist above:
   - `event/Data.cc:106-111` → `GetPlaneNo()` using `N_CH_PER_BLOCK`, `N_CH_U`, `N_CH_V`
   - `event/Waveforms.cc:259-270` → same (keep both `GetPlaneNo` copies in sync)
   - `event/Waveforms.cc:70-74` → `for (int i=0; i < N_BLOCKS-1; i++)`, use `N_CH_PER_BLOCK*(i+1)` and `N_TICKS`
   - `event/RawWaveforms.cc:43,73-79` → `ADC_BITS` and `STICKY_PERIOD`
   - `scripts/preprocess.C:108-111` → pass VD `xmax` and `ymax` explicitly; alternatively change the defaults
   - `scripts/preprocess.C:128-139` → replace `6` with `N_BLOCKS`
   - `viewer/ControlWindow.cc:25` → `N_TOTAL_CH − 1`
   - `viewer/ControlWindow.cc:55` → `N_TICKS`
   - `viewer/ControlWindow.cc:76-78` → update plane-label strings if VD uses different plane names

4. **Update histogram naming** if VD WCT output uses different prefixes (see §8). Recommended: keep `hu_`/`hv_`/`hw_` on the WCT side.

5. **Refresh data files**: regenerate `data/badchan.txt` and `data/noisychan.txt` with VD channel numbers.

6. **Enable `T_bad` tree merge**: uncomment `preprocess.sh:46` and adjust the tree name to match the VD WCT output.

7. **Update the dummy fallback** in `event/Data.cc:159-196` (or remove it and let missing histograms throw an error during development).

8. **Update `test_feature/evd/evd-subregion.py:74`** with VD physical constants.

9. **Smoke-test** with a single VD event file:
   - Run `preprocess.sh` and verify the merged file has the correct channel/tick axis range.
   - Launch `magnify.sh` and verify all 6 TH2 pads are populated.
   - Click a wire in each plane and confirm the 1-D waveform appears in the correct bottom pad.
   - Toggle "bad channel" and verify overlays appear (confirms `T_bad` merge is working).

---

## Quick Reference: HD Constants to Replace

| Constant | HD Value | Appears in |
|---|---|---|
| Channels per APA/CRU | 2560 | Data.cc:106, Waveforms.cc:71,260, oscope/buttonTest.C:181,216 |
| U boundary offset | 800 | Data.cc:108, Waveforms.cc:262 |
| V boundary offset | 1600 | Data.cc:111, Waveforms.cc:265 |
| W channel count per APA | 960 | (implicit: 2560−1600) |
| Number of APAs | 6 | preprocess.C:132, Waveforms.cc:70,194 |
| APA boundary line count | 5 | Waveforms.cc:70 (`i!=5`) |
| Total channels | 15 360 | preprocess.C:109 (xmax=15359.5), ControlWindow.cc:25 |
| Total ticks | 6000 | preprocess.C:111, Waveforms.cc:71, ControlWindow.cc:55 |
| ADC bits | 12 (range 0–4095) | RawWaveforms.cc:43, preprocess.C:35 |
| Sticky-code period | 64 | RawWaveforms.cc:74 |
| Dummy U start/count | 0 / 2400 | Data.cc:159,161 |
| Dummy V start/count | 2400 / 2400 | Data.cc:162–163 |
| Dummy W start/count | 4800 / 3456 | Data.cc:165–167 |
| Wire pitch (EVD) | 4.71 mm | evd/evd-subregion.py:74 |
| Tick duration (EVD) | 0.5 µs | evd/evd-subregion.py:74 |
| Drift speed (EVD) | 1.6 mm/µs | evd/evd-subregion.py:74 |
