#include "RmsAnalyzer.h"

#include "TH2F.h"
#include "TFile.h"
#include "TTree.h"
#include "TSystem.h"
#include "TVirtualFFT.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace std;

// ---- internal helpers (not exposed in header) ----

// Percentile-based RMS on unflagged samples (< 4096).
// Mirrors WCT CalcRMSWithFlags (Microboone.cxx:549):
//   p16/p50/p84 via nth_element, then sqrt(((p84-p50)^2 + (p50-p16)^2) / 2).
// If medianOut is non-null, also returns p50 (used as pedestal estimate for FFT).
static float calcRmsWithFlags(const vector<float>& sig, float* medianOut = nullptr)
{
    vector<float> temp;
    temp.reserve(sig.size());
    for (float s : sig) {
        if (s < 4096.f) temp.push_back(s);
    }
    if ((int)temp.size() < 3) {
        if (medianOut) *medianOut = 0.f;
        return 0.f;
    }

    size_t n   = temp.size();
    size_t i16 = (size_t)(0.16 * n);
    size_t i50 = (size_t)(0.50 * n);
    size_t i84 = (size_t)(0.84 * n);
    if (i84 >= n) i84 = n - 1;
    if (i50 >= n) i50 = n - 1;

    // nth_element rearranges in place; run from highest index down to preserve earlier results
    nth_element(temp.begin(), temp.begin() + i84, temp.end());
    float p84 = temp[i84];
    nth_element(temp.begin(), temp.begin() + i50, temp.begin() + i84);
    float p50 = temp[i50];
    nth_element(temp.begin(), temp.begin() + i16, temp.begin() + i50);
    float p16 = temp[i16];

    if (medianOut) *medianOut = p50;
    return sqrtf(((p84 - p50) * (p84 - p50) + (p50 - p16) * (p50 - p16)) / 2.f);
}

// Mark signal regions and flag them with +20000.
// Mirrors WCT SignalFilter (Microboone.cxx:573):
//   sigFactor=4.0, padBins=8; condition: |ADC| > 4*rms AND ADC < 4096.
// Returns the number of bins flagged.
static int signalFilter(vector<float>& sig, float rms0)
{
    const float sigFactor = 4.0f;
    const int   padBins   = 8;
    int n = (int)sig.size();
    int nFlagged = 0;

    vector<bool> mark(n, false);
    for (int i = 0; i < n; ++i) {
        if (sig[i] < 4096.f && fabsf(sig[i]) > sigFactor * rms0)
            mark[i] = true;
    }
    for (int i = 0; i < n; ++i) {
        if (!mark[i]) continue;
        int lo = (i - padBins > 0)     ? i - padBins : 0;
        int hi = (i + padBins < n - 1) ? i + padBins : n - 1;
        for (int j = lo; j <= hi; ++j) {
            if (sig[j] < 4096.f) {
                sig[j] += 20000.f;
                ++nFlagged;
            }
        }
    }
    return nFlagged;
}

// Build a baseline-subtracted, signal-clamped waveform for FFT input.
// Signal regions (same mask as signalFilter but applied to deviation from baseline)
// are clamped to ±4*rms0 to keep the waveform continuous.
// Sentinel samples (>=4096) are set to zero.
static void clampSignalForFft(const vector<float>& sig, float baseline, float rms0,
                              vector<double>& out)
{
    const float sigFactor = 4.0f;
    const int   padBins   = 8;
    int n = (int)sig.size();
    out.resize(n);

    // Mark signal bins based on deviation from baseline
    vector<bool> flagged(n, false);
    for (int i = 0; i < n; ++i) {
        if (sig[i] < 4096.f && fabsf(sig[i] - baseline) > sigFactor * rms0)
            flagged[i] = true;
    }
    // Expand marks by padBins
    vector<bool> expanded = flagged;
    for (int i = 0; i < n; ++i) {
        if (!flagged[i]) continue;
        int lo = (i - padBins > 0)     ? i - padBins : 0;
        int hi = (i + padBins < n - 1) ? i + padBins : n - 1;
        for (int j = lo; j <= hi; ++j) expanded[j] = true;
    }

    float clampVal = sigFactor * rms0;
    for (int i = 0; i < n; ++i) {
        if (sig[i] >= 4096.f) {
            out[i] = 0.0;  // sentinel / missing channel
        } else if (expanded[i]) {
            // Clamp deviation to ±clampVal (preserves sign, removes large excursions)
            float dev = sig[i] - baseline;
            if      (dev >  clampVal) dev =  clampVal;
            else if (dev < -clampVal) dev = -clampVal;
            out[i] = (double)dev;
        } else {
            out[i] = (double)(sig[i] - baseline);
        }
    }
}

// ---- public API ----

vector<ChannelRms> RmsAnalyzer::AnalyzePlane(TH2F* h)
{
    vector<ChannelRms> result;
    if (!h) return result;

    int nCh = h->GetNbinsX();
    int nTk = h->GetNbinsY();
    int firstCh = (int)(h->GetXaxis()->GetBinCenter(1) + 0.5);
    TString srcName = h->GetName();

    result.reserve(nCh);
    for (int ix = 1; ix <= nCh; ++ix) {
        vector<float> sig(nTk);
        for (int iy = 1; iy <= nTk; ++iy)
            sig[iy - 1] = h->GetBinContent(ix, iy);

        float rms0   = calcRmsWithFlags(sig);
        int   nSig   = signalFilter(sig, rms0);
        float rmsF   = calcRmsWithFlags(sig);

        ChannelRms cr;
        cr.channel     = firstCh + (ix - 1);
        cr.rms_prelim  = rms0;
        cr.rms_final   = rmsF;
        cr.nSignalBins = nSig;
        cr.srcHist     = srcName;
        result.push_back(cr);
    }
    return result;
}

vector<ChannelRms> RmsAnalyzer::AnalyzePlaneWithFft(TH2F* h,
                                                    const char* fftHistName,
                                                    TH2F*& outFft)
{
    outFft = nullptr;
    vector<ChannelRms> result;
    if (!h) return result;

    int nCh = h->GetNbinsX();
    int nTk = h->GetNbinsY();
    int firstCh = (int)(h->GetXaxis()->GetBinCenter(1) + 0.5);
    TString srcName = h->GetName();

    // Frequency axis: Y stores tick indices (integer steps), so use the constant.
    int    nFreqBins  = nTk / 2;
    double nyquistMHz = kNyquistMHz;

    outFft = new TH2F(fftHistName,
        TString::Format(";channel;freq (MHz);|F| (ADC)"),
        nCh, h->GetXaxis()->GetXmin(), h->GetXaxis()->GetXmax(),
        nFreqBins, 0.0, nyquistMHz);
    outFft->SetDirectory(nullptr);

    result.reserve(nCh);
    vector<float>  sig(nTk);
    vector<double> fftIn(nTk);

    // One FFT instance for all channels; "ES K" = estimate plan, keep instance
    int fftN = nTk;
    TVirtualFFT* fft = TVirtualFFT::FFT(1, &fftN, "R2C ES K");

    for (int ix = 1; ix <= nCh; ++ix) {
        for (int iy = 1; iy <= nTk; ++iy)
            sig[iy - 1] = h->GetBinContent(ix, iy);

        // Preliminary RMS + median (pedestal estimate)
        float median = 0.f;
        float rms0   = calcRmsWithFlags(sig, &median);

        // FFT on baseline-subtracted, signal-clamped waveform
        clampSignalForFft(sig, median, rms0, fftIn);
        fft->SetPoints(fftIn.data());
        fft->Transform();

        double re = 0, im = 0;
        for (int k = 0; k < nFreqBins; ++k) {
            fft->GetPointComplex(k, re, im);
            double mag = (k == 0) ? 0.0 : sqrt(re*re + im*im) / fftN;
            outFft->SetBinContent(ix, k + 1, mag);
        }

        // Full signal-flagging RMS pipeline (mutates sig in place)
        int   nSig = signalFilter(sig, rms0);
        float rmsF = calcRmsWithFlags(sig);

        ChannelRms cr;
        cr.channel     = firstCh + (ix - 1);
        cr.rms_prelim  = rms0;
        cr.rms_final   = rmsF;
        cr.nSignalBins = nSig;
        cr.srcHist     = srcName;
        result.push_back(cr);
    }

    delete fft;
    return result;
}

// Internal implementation used by both Save overloads.
static void saveImpl(const vector<ChannelRms>& u,
                     const vector<ChannelRms>& v,
                     const vector<ChannelRms>& w,
                     TH2F* fftU, TH2F* fftV, TH2F* fftW,
                     const char* outFile)
{
    TFile* f = TFile::Open(outFile, "RECREATE");
    if (!f || f->IsZombie()) {
        printf("RmsAnalyzer::Save: cannot open %s for writing\n", outFile);
        delete f;
        return;
    }

    static const char* treeName[3] = {"rms_u", "rms_v", "rms_w"};
    const vector<ChannelRms>* planes[3] = {&u, &v, &w};

    int   channel, nSignalBins;
    float rms_prelim, rms_final;
    char  srcHist[256];

    for (int p = 0; p < 3; ++p) {
        TTree* t = new TTree(treeName[p], treeName[p]);
        t->Branch("channel",     &channel,     "channel/I");
        t->Branch("rms_prelim",  &rms_prelim,  "rms_prelim/F");
        t->Branch("rms_final",   &rms_final,   "rms_final/F");
        t->Branch("nSignalBins", &nSignalBins, "nSignalBins/I");
        t->Branch("srcHist",      srcHist,     "srcHist/C");

        for (const auto& r : *planes[p]) {
            channel     = r.channel;
            rms_prelim  = r.rms_prelim;
            rms_final   = r.rms_final;
            nSignalBins = r.nSignalBins;
            strncpy(srcHist, r.srcHist.Data(), 255);
            srcHist[255] = '\0';
            t->Fill();
        }
        t->Write();
    }

    // Write FFT histograms (skip nulls)
    const char* fftName[3] = {"fft_u", "fft_v", "fft_w"};
    TH2F* ffts[3] = {fftU, fftV, fftW};
    for (int p = 0; p < 3; ++p) {
        if (ffts[p]) ffts[p]->Write(fftName[p]);
    }

    f->Close();
    delete f;
    printf("RmsAnalyzer::Save: wrote %s\n", outFile);
}

void RmsAnalyzer::Save(const vector<ChannelRms>& u,
                       const vector<ChannelRms>& v,
                       const vector<ChannelRms>& w,
                       const char* outFile)
{
    saveImpl(u, v, w, nullptr, nullptr, nullptr, outFile);
}

void RmsAnalyzer::Save(const vector<ChannelRms>& u,
                       const vector<ChannelRms>& v,
                       const vector<ChannelRms>& w,
                       TH2F* fftU, TH2F* fftV, TH2F* fftW,
                       const char* outFile)
{
    saveImpl(u, v, w, fftU, fftV, fftW, outFile);
}

// Internal implementation used by both Load overloads.
static bool loadImpl(const char* inFile,
                     vector<ChannelRms>& u,
                     vector<ChannelRms>& v,
                     vector<ChannelRms>& w,
                     TH2F** fftOut)   // array of 3 pointers, or nullptr to skip
{
    if (gSystem->AccessPathName(inFile)) return false;

    TFile* f = TFile::Open(inFile, "READ");
    if (!f || f->IsZombie()) { delete f; return false; }

    static const char* treeName[3] = {"rms_u", "rms_v", "rms_w"};
    vector<ChannelRms>* planes[3]  = {&u, &v, &w};

    int   channel, nSignalBins;
    float rms_prelim, rms_final;
    char  srcHist[256];

    for (int p = 0; p < 3; ++p) {
        planes[p]->clear();
        TTree* t = (TTree*)f->Get(treeName[p]);
        if (!t) { f->Close(); delete f; return false; }

        t->SetBranchAddress("channel",     &channel);
        t->SetBranchAddress("rms_prelim",  &rms_prelim);
        t->SetBranchAddress("rms_final",   &rms_final);
        t->SetBranchAddress("nSignalBins", &nSignalBins);
        t->SetBranchAddress("srcHist",      srcHist);

        Long64_t nEntries = t->GetEntries();
        planes[p]->reserve(nEntries);
        for (Long64_t i = 0; i < nEntries; ++i) {
            t->GetEntry(i);
            ChannelRms cr;
            cr.channel     = channel;
            cr.rms_prelim  = rms_prelim;
            cr.rms_final   = rms_final;
            cr.nSignalBins = nSignalBins;
            cr.srcHist     = srcHist;
            planes[p]->push_back(cr);
        }
    }

    // Load FFT histograms if requested
    if (fftOut) {
        const char* fftName[3] = {"fft_u", "fft_v", "fft_w"};
        for (int p = 0; p < 3; ++p) {
            fftOut[p] = nullptr;
            TH2F* h = (TH2F*)f->Get(fftName[p]);
            if (h) {
                fftOut[p] = (TH2F*)h->Clone();
                fftOut[p]->SetDirectory(nullptr);
            }
        }
    }

    f->Close();
    delete f;
    return true;
}

bool RmsAnalyzer::Load(const char* inFile,
                       vector<ChannelRms>& u,
                       vector<ChannelRms>& v,
                       vector<ChannelRms>& w)
{
    return loadImpl(inFile, u, v, w, nullptr);
}

bool RmsAnalyzer::Load(const char* inFile,
                       vector<ChannelRms>& u,
                       vector<ChannelRms>& v,
                       vector<ChannelRms>& w,
                       TH2F*& fftU, TH2F*& fftV, TH2F*& fftW)
{
    fftU = fftV = fftW = nullptr;
    TH2F* ffts[3] = {nullptr, nullptr, nullptr};
    bool ok = loadImpl(inFile, u, v, w, ffts);
    if (ok) { fftU = ffts[0]; fftV = ffts[1]; fftW = ffts[2]; }
    return ok;
}

TString RmsAnalyzer::CacheFilename(const char* magnifyFile)
{
    return TString(magnifyFile) + ".rms.root";
}

void RmsAnalyzer::AnalyzeFile(const char* inFile)
{
    TFile* f = TFile::Open(inFile, "READ");
    if (!f || f->IsZombie()) {
        printf("RmsAnalyzer::AnalyzeFile: cannot open %s\n", inFile);
        delete f;
        return;
    }

    // Discover TPC suffix from Trun tree; try multiple branch name conventions
    int tpcNo = 0;
    TTree* trun = (TTree*)f->Get("Trun");
    if (trun) {
        if      (trun->GetBranch("tpcNo"))   trun->SetBranchAddress("tpcNo",   &tpcNo);
        else if (trun->GetBranch("anodeNo")) trun->SetBranchAddress("anodeNo", &tpcNo);
        else if (trun->GetBranch("apaNo"))   trun->SetBranchAddress("apaNo",   &tpcNo);
        if (trun->GetEntries() > 0) trun->GetEntry(0);
    }
    TString suf = TString::Format("%d", tpcNo);

    static const char* prefix[3]    = {"hu_", "hv_", "hw_"};
    static const char* planeName[3] = {"U",   "V",   "W"};
    static const char* tags[]       = {"raw", "decon", "gauss", "wiener", nullptr};
    static const char* fftKey[3]    = {"fft_u", "fft_v", "fft_w"};

    vector<ChannelRms> results[3];
    TH2F*              fftHists[3] = {nullptr, nullptr, nullptr};

    for (int p = 0; p < 3; ++p) {
        TH2F* h = nullptr;
        for (int t = 0; tags[t]; ++t) {
            TString hname = TString(prefix[p]) + tags[t] + suf;
            h = (TH2F*)f->Get(hname);
            if (h) break;
        }
        if (!h) {
            printf("  plane %s: no histogram found, skipping\n", planeName[p]);
            continue;
        }
        printf("  plane %s: %s  (%d ch x %d ticks)\n",
            planeName[p], h->GetName(), h->GetNbinsX(), h->GetNbinsY());
        results[p] = RmsAnalyzer::AnalyzePlaneWithFft(h, fftKey[p], fftHists[p]);
    }
    f->Close();
    delete f;

    TString outFile = CacheFilename(inFile);
    Save(results[0], results[1], results[2],
         fftHists[0], fftHists[1], fftHists[2],
         outFile.Data());

    for (int p = 0; p < 3; ++p) delete fftHists[p];
}
