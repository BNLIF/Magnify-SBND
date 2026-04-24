// Headless batch macro: compute per-channel RMS + FFT for one Magnify file and
// write <inFile>.rms.root alongside it.  The cache file contains:
//   • TTrees rms_u / rms_v / rms_w  — per-channel RMS values
//   • TH2Fs  fft_u / fft_v / fft_w  — per-channel FFT spectra (ch × freq-MHz)
//
// Must be run from the scripts/ directory (same convention as magnify.sh) so
// that loadClasses.C can find ../event and ../viewer via relative paths.
//
// Usage (from Magnify_PDHD root):
//   root -b -q 'scripts/run_rms_analysis.C("input_data/magnify-run.root")'

void run_rms_analysis(const char* inFile)
{
    // Load compiled classes first; all type usage dispatched through ProcessLine
    // so that RmsAnalyzer is resolved after compilation, not at macro parse time.
    gROOT->ProcessLine(".x loadClasses.C");
    TString cmd = TString::Format("RmsAnalyzer::AnalyzeFile(\"%s\")", inFile);
    gROOT->ProcessLine(cmd);
}
