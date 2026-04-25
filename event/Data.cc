#include "Data.h"
#include "Geometry.h"
#include "Waveforms.h"
#include "RawWaveforms.h"
#include "BadChannels.h"

#include "TH2F.h"
#include "TH2I.h"
#include "TH1I.h"
#include "TFile.h"
#include "TTree.h"
#include "TString.h"
#include "TSystem.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>

using namespace std;


Data::Data()
{}

Data::Data(const char* filename, double threshold, const char* frame, int rebin)
{
    rootFile = TFile::Open(filename);
    if (!rootFile) {
    	string msg = "Unable to open ";
    	msg += filename;
    	throw runtime_error(msg.c_str());
    }

    // load_runinfo first so tpcNo is available for all histogram name lookups
    load_runinfo();
    load_geometry();

    // suffix is the tpc number when present (e.g. "0"), otherwise empty string
    TString suf = (tpcNo >= 0) ? Form("%d", tpcNo) : "";
    if (tpcNo >= 0)
        cout << "TPC number: " << tpcNo << " (using suffix \"" << suf << "\")" << endl;

    bad_channels = new BadChannels( (TTree*)rootFile->Get("T_bad" + suf) );

    load_channelstatus();

    // Pre-fetch decon TH2 objects (with fallback) so their geometry can be used
    // as the dummy dimensions for the raw/denoised histograms that may be missing.
    TH2* decon_ref[3] = {};
    TString decon_name[3];
    for (int iplane = 0; iplane < 3; ++iplane) {
        decon_name[iplane] = Form("h%c_%s", 'u'+iplane, frame) + suf;
        if (!rootFile->Get(decon_name[iplane])) {
            TString fb_gauss = Form("h%c_gauss", 'u'+iplane) + suf;
            TString fb_dnn   = Form("h%c_dnnsp", 'u'+iplane) + suf;
            if (rootFile->Get(fb_gauss)) {
                cout << decon_name[iplane] << " not found, falling back to " << fb_gauss << endl;
                decon_name[iplane] = fb_gauss;
            } else if (rootFile->Get(fb_dnn)) {
                cout << decon_name[iplane] << " not found, falling back to " << fb_dnn << endl;
                decon_name[iplane] = fb_dnn;
            }
        }
        decon_ref[iplane] = dynamic_cast<TH2*>(rootFile->Get(decon_name[iplane]));
    }

    double raw_scale = 1.0;

    // std::cout << "Loading histograms with raw_scale = " << raw_scale << " and threshold = " << threshold << " " << rebin << endl;

    // denoised (post noise-filter) — use decon geometry for dummies when missing
    load_waveform("hu_raw" + suf, "U Plane (Denoised)", raw_scale, threshold, decon_ref[0]);
    load_waveform("hv_raw" + suf, "V Plane (Denoised)", raw_scale, threshold, decon_ref[1]);
    load_waveform("hw_raw" + suf, "W Plane (Denoised)", raw_scale, threshold, decon_ref[2]);

    // deconvoluted, scaled down by 1/25 ... 
    for (int iplane = 0; iplane < 3; ++iplane) {
        load_waveform(decon_name[iplane], Form("%c Plane (Deconvoluted)", 'U'+iplane), 1./(100.*rebin/4.0) * raw_scale, threshold);
    }

    load_rawwaveform("hu_orig" + suf, "hu_baseline" + suf);
    load_rawwaveform("hv_orig" + suf, "hv_baseline" + suf);
    load_rawwaveform("hw_orig" + suf, "hw_baseline" + suf);

    load_threshold("hu_threshold" + suf);
    load_threshold("hv_threshold" + suf);
    load_threshold("hw_threshold" + suf);

    // Apply per-channel Wiener thresholds with plane-type-specific scaling.
    // Raw signal scale by 0.5 ...
    // decon  5.0 *
    denoised_scaling = 0.5;
    decon_scaling    = 5.0;
    for (int i = 0; i < 3; ++i) {
        if (thresh_histos[i]->GetMaximum() > 0) {
            wfs[i]->SetThreshold(thresh_histos[i], denoised_scaling);
            wfs[i+3]->SetThreshold(thresh_histos[i], decon_scaling);
        }
    }

}

void Data::load_runinfo()
{
    tpcNo = -1;
    total_time_bin = 0;
    TTree *t = (TTree*)rootFile->Get("Trun");
    if (t) {
        t->SetBranchAddress("runNo", &runNo);
        t->SetBranchAddress("subRunNo", &subRunNo);
        t->SetBranchAddress("eventNo", &eventNo);
        // Try multiple branch name conventions for the per-TPC index
        if      (t->GetBranch("tpcNo"))   t->SetBranchAddress("tpcNo",   &tpcNo);
        else if (t->GetBranch("anodeNo")) t->SetBranchAddress("anodeNo", &tpcNo);
        else if (t->GetBranch("apaNo"))   t->SetBranchAddress("apaNo",   &tpcNo);
        if (t->GetBranch("total_time_bin")) t->SetBranchAddress("total_time_bin", &total_time_bin);
        t->GetEntry(0);
    }
    else {
        runNo = 0;
        subRunNo = 0;
        eventNo = 0;
    }
}

void Data::load_geometry() {
    TString suf = (tpcNo >= 0) ? Form("%d", tpcNo) : "";
    TTree* t = (TTree*)rootFile->Get("T_geo" + suf);
    if (!t) {
        cout << "T_geo" << suf << " not found; wire-length plot disabled" << endl;
        return;
    }
    int    chid   = 0;
    double length = 0.;
    t->SetBranchAddress("chid",   &chid);
    t->SetBranchAddress("length", &length);
    Long64_t n = t->GetEntries();
    for (Long64_t i = 0; i < n; ++i) {
        t->GetEntry(i);
        wire_length[chid] = length;
    }
    cout << "Loaded " << (int)wire_length.size()
         << " wire lengths from T_geo" << suf << endl;
}

void Data::load_channelstatus(){
    string currentDir(gSystem->WorkingDirectory());

    std::ifstream in(currentDir + "/../data/badchan.txt");
    std::string input;
    while(std::getline(in, input)){
        std::stringstream stream(input);
        int nchan;
        stream >> nchan;
        std::string description = stream.str();
        size_t ind = description.find_first_of("#");
        description = description.substr(ind+1);
        channel_status[nchan] = "(Bad) " + description;
    }
    in.close();

    in.open(currentDir + "/../data/noisychan.txt");
    while(std::getline(in, input)){
        std::stringstream stream(input);
        int nchan;
        stream >> nchan;
        std::string description = stream.str();
        size_t ind = description.find_first_of("#");
        description = description.substr(ind+1);
        channel_status[nchan] = "(Noisy)" + description;
    }
    in.close();
}

int Data::GetPlaneNo(int chanNo)
{
    // Prefer decon histograms (wfs[3..5]) for range detection since they carry
    // the real channel numbers; fall back to raw (wfs[0..2]) if needed.
    for (int p = 0; p < 3; ++p) {
        if (p+3 < (int)wfs.size()) {
            Waveforms* w = wfs[p+3];
            if (chanNo >= w->firstChannel && chanNo < w->firstChannel + w->nChannels)
                return p;
        }
    }
    for (int p = 0; p < 3; ++p) {
        if (p < (int)wfs.size()) {
            Waveforms* w = wfs[p];
            if (chanNo >= w->firstChannel && chanNo < w->firstChannel + w->nChannels)
                return p;
        }
    }
    // SBND-geometry formula as last resort
    int tpcNo = chanNo / N_CH_PER_BLOCK;
    int offset = chanNo - tpcNo * N_CH_PER_BLOCK;
    if (offset < N_CH_U) return 0;
    else if (offset < N_CH_U + N_CH_V) return 1;
    else return 2;
}

// Wrap up some ROOT pointer dancing.
template<class NEED, class WANT>
WANT* maybe_cast(TObject* obj, const std::vector<std::string>& okay_types, bool throw_on_err=false)
{
    NEED* base = dynamic_cast<NEED*>(obj);
    if (!base) {
	return nullptr;
    }
    bool ok = false;
    for (auto type_name : okay_types) {
	if (base->InheritsFrom(type_name.c_str())) {
	    ok = true;
	    break;
	}
    }
    if (ok) {
	return static_cast<WANT*>(base);
    }
    if (throw_on_err) {
	stringstream ss;
	ss << "TObject not one of type: [";
	string comma = "";
	for (auto type_name : okay_types) {
	    ss << comma << type_name;
	    comma = ", ";
	}
        throw runtime_error(ss.str().c_str());
    }
    return nullptr;
}

void Data::load_waveform(const char* name, const char* title, double scale, double threshold, TH2* ref)
{
    TObject* obj = rootFile->Get(name);
    if (!obj) {
        cout << "Failed to get waveform " << name << ", create dummy ..." << endl;
        if (ref) {
            // Match the geometry of the reference (decon) histogram exactly
            obj = new TH2F(name, title,
                ref->GetNbinsX(), ref->GetXaxis()->GetXmin(), ref->GetXaxis()->GetXmax(),
                ref->GetNbinsY(), ref->GetYaxis()->GetXmin(), ref->GetYaxis()->GetXmax());
        } else {
            // SBND fallback dimensions for missing histograms
            int nChannels = N_CH_U, firstChannel = 0;
            TString msg(name);
            if (msg.Contains("hv")) { firstChannel = N_CH_U; nChannels = N_CH_V; }
            else if (msg.Contains("hw")) { firstChannel = N_CH_U + N_CH_V; nChannels = N_CH_W; }
            obj = new TH2F(name, title, nChannels, firstChannel-0.5, firstChannel+nChannels-0.5, N_TICKS, 0, N_TICKS);
        }
    }
    auto hist = maybe_cast<TH2, TH2F>(obj, {"TH2F", "TH2I"}, true);
    hist->SetXTitle("channel");
    hist->SetYTitle("ticks");
    wfs.push_back( new Waveforms(hist, bad_channels, name, title, scale, threshold) );
}

void Data::load_rawwaveform(const char* name, const char* baseline_name)
{
    TObject* obj = rootFile->Get(name);
    if (!obj) {
        TString msg = "Failed to get waveform ";
        msg += name;
        msg += ", create dummy ...";
        cout << msg << endl;
        // throw runtime_error(msg.c_str());
        int nChannels = N_CH_U;
        int firstChannel = 0;
        if (msg.Contains("hv")) { firstChannel = N_CH_U; nChannels = N_CH_V; }
        else if (msg.Contains("hw")) { firstChannel = N_CH_U + N_CH_V; nChannels = N_CH_W; }
        obj = new TH2I(name, "", nChannels,firstChannel-0.5,firstChannel+nChannels-0.5,N_TICKS,0,N_TICKS);
    }

    auto hist = maybe_cast<TH2, TH2I>(obj, {"TH2I", "TH2F"}, true);
    hist->SetXTitle("channel");
    hist->SetYTitle("ticks");

    TObject* obj2 = rootFile->Get(baseline_name);
    if (!obj2) {
        // string msg = "Failed to get baseline ";
        // msg += baseline_name;
        // msg += ", create dummy ...";
        // // throw runtime_error(msg.c_str());
        // obj2 = new TH1I(baseline_name, "", hist->GetNbinsX(),0,hist->GetNbinsX());
        raw_wfs.push_back( new RawWaveforms(hist, 0) );
    }
    else {
        TH1I* hist2 = dynamic_cast<TH1I*>(obj2);
        if (!hist2) {
            string msg = "Not a TH1I: ";
            msg += name;
            throw runtime_error(msg.c_str());
        }
        raw_wfs.push_back( new RawWaveforms(hist, hist2) );
    }

}

void Data::load_threshold(const char* name)
{
    TObject* obj = rootFile->Get(name);
    if (!obj) {
        string msg = "Failed to get threshold ";
        msg += name;
        msg += ", create dummy ...";
        // throw runtime_error(msg.c_str());
        obj = new TH1I(name, "", N_TOTAL_CH, 0, N_TOTAL_CH);
    }
    auto hist = maybe_cast<TH1, TH1I>(obj, {"TH1I", "TH1F"}, true);
    thresh_histos.push_back( hist );
}

Data::~Data()
{
    for (auto* w : wfs)     delete w;
    wfs.clear();
    for (auto* r : raw_wfs) delete r;
    raw_wfs.clear();
    delete bad_channels;
    bad_channels = nullptr;
    thresh_histos.clear();  // owned by rootFile; freed when rootFile closes
    delete rootFile;
}
// Local Variables:
// mode: c++
// c-basic-offset: 4
// End:
