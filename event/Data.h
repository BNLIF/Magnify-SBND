#ifndef DATA_H
#define DATA_H

class TH2F;
class TH2;
class TH1I;
class TFile;
class Waveforms;
class RawWaveforms;
class BadChannels;

#include <vector>
#include <map>
#include <string>

class Data {
public:
    Data();
    Data(const char* filename, double threshold, const char* frame, int rebin);

    virtual ~Data();

    TFile *rootFile;
    vector<Waveforms*> wfs;
    BadChannels* bad_channels;
    // vector<int> bad_channels;
    // vector<int> bad_start;
    // vector<int> bad_end;
    vector<RawWaveforms*> raw_wfs;
    vector<TH1I*> thresh_histos;
    int runNo;
    int subRunNo;
    int eventNo;
    int tpcNo;
    int total_time_bin;
    double denoised_scaling;  // scaling applied to h*_threshold for denoised wfs
    double decon_scaling;     // scaling applied to h*_threshold for decon wfs

    std::map<int, std::string> channel_status;
    std::map<int, double>      wire_length;    // chid -> length (cm); empty if T_geo absent

    int GetPlaneNo(int chanNo);


private:
    void load_waveform(const char* name, const char* title="", double scale=1, double threshold=600, TH2* ref=nullptr);
    void load_rawwaveform(const char* name, const char* baseline_name);
    void load_threshold(const char* name);
    // void load_badchannels();
    void load_runinfo();
    void load_channelstatus();
    void load_geometry();
};

#endif
