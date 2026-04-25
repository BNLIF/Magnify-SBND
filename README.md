# Magnify_SBND

## A magnifier to investigate raw and deconvoluted waveforms for the SBND detector.

SBND geometry: 2 TPCs × 5638 channels/TPC = 11 276 total channels, 3427 time ticks (3400 physics readout + 27 deconvolution-tail ticks with real signal), U/V/W planes (1984 + 1984 + 1670 ch/TPC).
See `docs/geometry.md` for the full channel-range and timing reference.

Derived from `Magnify_PDHD` (itself derived from `Magnify_PDVD` and upstream `Magnify-protodune`).
See `docs/porting_to_sbnd.md` for a record of all changes made from PDHD.

### Preprocess
```
./preprocess.sh /path/to/your/magnify.root
```
This is a wrapper for merging magnify histograms from all TPCs. See more detailed description through `./preprocess.sh -h`.


### Usage

```
cd scripts/
root -l loadClasses.C 'Magnify.C("path/to/rootfile")'
# or
root -l loadClasses.C 'Magnify.C("path/to/rootfile", <threshold>, "<frame>", <rebin>)'
```

The second argument is the default threshold for showing a box.

The third, optional argument names which output from the signal processing to display.  Likely names are:

- `decon` produced by the Wire Cell prototype (default).
- `wiener` produced by the Wire Cell toolkit, used to define ROI or "hits".
- `gauss` produced by the Wire Cell toolkit, used for charge measurement.

The call to ROOT can be called somewhat more easily via a shell
script wrapper.  It assumes to stay in the source directory:

```
/path/to/magnify/magnify.sh /path/to/wcp-rootfile.root
# or
/path/to/magnify/magnify.sh /path/to/wct-rootfile.root 500 gauss 4
```

### Example files

Place SBND magnify ROOT files under `input_data/` (symlinked to `../wcp-porting-validation/sbnd/sbnd_xin/work`).

If one omits the file name, a dialog will open to let user select the file:
```
cd scripts/
root -l loadClasses.C Magnify.C
```

### In-GUI event and anode navigation

The second row of the control window contains a **Navigation** group that lets you
switch events and anodes without restarting the viewer:

| Control | Action |
|---------|--------|
| **Anode** combo | Switch between Anode 0 and Anode 1 for the current event |
| **Event** combo | Jump directly to any event found under `input_data/` (sorted numerically) |
| **`<`** button | Step to the previous event (clamps at the first) |
| **`>`** button | Step to the next event (clamps at the last) |

Events are discovered automatically at startup by scanning the `input_data/` directory
for subdirectories matching `evt*` that contain at least one `magnify-*-anode{0,1}.root`
file.  No code change is needed when new events are added.

### RMS & FFT noise analysis (batch)

Compute per-channel noise RMS and frequency spectra for one or more Magnify files:

```bash
./scripts/run_rms_analysis.sh input_data/magnify-run.root
# Produces: input_data/magnify-run.root.rms.root
```

The cache file contains per-plane RMS TTrees (`rms_u/v/w`) and FFT TH2Fs
(`fft_u/v/w`, channel × frequency in MHz).  It is loaded automatically by the
viewer when you click **RMS Analysis** in the control bar.  See `docs/RMS_FFT.md`
for the full workflow and panel description.

### (Experimental feature) Channel Scan
```
./channelscan.sh path/to/rootfile
```
This is a wrapper for looping over channels where the channel list can be predefined in the `bad tree` or a text file.

See detailed usage via `./channelscan.sh -h`.
