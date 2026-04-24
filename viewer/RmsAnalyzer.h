#ifndef RMS_ANALYZER_H
#define RMS_ANALYZER_H

#include <vector>
#include "TString.h"

class TH2F;

struct ChannelRms {
    int     channel;
    float   rms_prelim;    // RMS before signal flagging
    float   rms_final;     // RMS after signal regions are masked
    int     nSignalBins;   // number of bins flagged as signal
    TString srcHist;       // name of source TH2F histogram
};

// Per-channel noise RMS using the WCT percentile-based algorithm:
//   Step 1: CalcRMSWithFlags  — preliminary RMS on unflagged samples (< 4096)
//   Step 2: SignalFilter      — mark |ADC| > 4*rms bins, pad ±8, flag with +20000
//   Step 3: CalcRMSWithFlags  — final RMS skipping all flagged samples
// Algorithm matches Microboone.cxx:549 (CalcRMSWithFlags) and :573 (SignalFilter).
class RmsAnalyzer {
public:
    // Sampling constants — single source of truth
    // tick = 0.5 µs → fs = 2 MHz → Nyquist = 1 MHz
    static constexpr double kTickMicroseconds = 0.5;
    static constexpr double kNyquistMHz       = 1.0 / (2.0 * kTickMicroseconds);

    // Compute per-channel RMS for all channels in the plane TH2F (channel × tick).
    static std::vector<ChannelRms> AnalyzePlane(TH2F* h);

    // Like AnalyzePlane but also computes per-channel FFT spectra (signal regions
    // clamped to ±4σ, baseline subtracted).  Results are stored in a TH2F with
    // X = channel and Y = frequency [0, kNyquistMHz] detached from gDirectory.
    // Caller takes ownership of outFft (may be nullptr on failure).
    static std::vector<ChannelRms> AnalyzePlaneWithFft(TH2F* h,
                                                       const char* fftHistName,
                                                       TH2F*& outFft);

    // Write RMS trees to outFile (backward-compat, no FFT).
    static void Save(const std::vector<ChannelRms>& u,
                     const std::vector<ChannelRms>& v,
                     const std::vector<ChannelRms>& w,
                     const char* outFile);

    // Write RMS trees + FFT TH2Fs to outFile; null FFT pointers are skipped.
    static void Save(const std::vector<ChannelRms>& u,
                     const std::vector<ChannelRms>& v,
                     const std::vector<ChannelRms>& w,
                     TH2F* fftU, TH2F* fftV, TH2F* fftW,
                     const char* outFile);

    // Load RMS trees from inFile (backward-compat, no FFT).
    static bool Load(const char* inFile,
                     std::vector<ChannelRms>& u,
                     std::vector<ChannelRms>& v,
                     std::vector<ChannelRms>& w);

    // Load RMS trees + FFT histograms from inFile.
    // FFT output pointers are set to nullptr when the histograms are absent
    // (backward-compat for pre-FFT cache files).  Caller owns the returned hists.
    static bool Load(const char* inFile,
                     std::vector<ChannelRms>& u,
                     std::vector<ChannelRms>& v,
                     std::vector<ChannelRms>& w,
                     TH2F*& fftU, TH2F*& fftV, TH2F*& fftW);

    // Returns "<magnifyFile>.rms.root"
    static TString CacheFilename(const char* magnifyFile);

    // Convenience batch entry point: open inFile, discover histograms, run all
    // three planes (RMS + FFT), and write results to CacheFilename(inFile).
    static void AnalyzeFile(const char* inFile);
};

#endif
