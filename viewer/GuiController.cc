#include "GuiController.h"
#include "MainWindow.h"
#include "ViewWindow.h"
#include "ControlWindow.h"
#include "Data.h"
#include "Waveforms.h"
#include "RawWaveforms.h"
#include "BadChannels.h"
#include "RmsAnalyzer.h"

#include "TApplication.h"
#include "TSystem.h"
#include "TExec.h"
#include "TROOT.h"
#include "TMath.h"
#include "TGFileDialog.h"

#include "TGMenu.h"
#include "TGNumberEntry.h"
#include "TCanvas.h"
#include "TFile.h"
#include "TGraph.h"
#include "TH2F.h"
#include "TH1F.h"
#include "TH2I.h"
#include "TH1I.h"
#include "TBox.h"
#include "TLine.h"
#include "TColor.h"
#include "TStyle.h"
#include "TLegend.h"
#include "TGButton.h"
#include "TGLabel.h"
#include "TGTextEntry.h"
#include "TRootEmbeddedCanvas.h"
#include "TGClient.h"
#include "TPad.h"
#include "TH1D.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
using namespace std;


GuiController::GuiController(const TGWindow *p, int w, int h, const char* fn, double threshold, const char* frame, int rebin)
{
    mw = new MainWindow(p, w, h);
    vw = mw->fViewWindow;
    cw = mw->fControlWindow;

    // data = new Data("../data/2D_display_3455_0_0.root");
    TString filename;
    if (!fn) {
        filename = OpenDialog();
    }
    else {
        filename = fn;
    }
    data = new Data(filename.Data(), threshold, frame, rebin);
    captureMode = CAPTURE_NONE;
    regionWindow = nullptr;
    regionCanvas = nullptr;
    for (int p = 0; p < 3; ++p) {
        regChStart[p] = regChEnd[p] = nullptr;
        regTLowS[p]  = regTHighS[p]  = nullptr;
        regTLowE[p]  = regTHighE[p]  = nullptr;
        for (int e = 0; e < 4; ++e) regionBoundary[p][e] = nullptr;
    }
    rmsWindow      = nullptr;
    rmsStatusLabel = nullptr;
    rmsOverlayCheck = nullptr;
    rmsDistCanvas  = nullptr;
    rmsLoaded      = false;
    rmsTopDistPad  = nullptr;
    rmsTopUvPad    = nullptr;
    rmsTopWPad     = nullptr;
    for (int p = 0; p < 3; ++p) {
        fftSpec[p]       = nullptr;
        fftSelectedCh[p] = -1;
        rmsMidPad[p]     = nullptr;
        rmsBotPad[p]     = nullptr;
    }
    mw->SetWindowName(TString::Format("Magnify: run %i, sub-run %i, event %i, tpc %i",
        data->runNo, data->subRunNo, data->eventNo, data->tpcNo));

    for (int i=0; i<6; i++) {
        vw->can->cd(i+1);
        data->wfs.at(i)->Draw2D();
    }
    for (int i=0; i<3; i++) {
        vw->can->cd(i+7);
        int chanNo = data->wfs.at(i)->firstChannel;
        std::string comment = data->channel_status[chanNo];
        data->wfs.at(i)->Draw1D(chanNo, "", comment.c_str());
        TH1F *h = data->wfs.at(i+3)->Draw1D(chanNo, "same"); // draw calib
        h->SetLineColor(kRed);
        hCurrent[i] = h;
    }

    InitConnections();
}

GuiController::~GuiController()
{
    // gApplication->Terminate(0);
}

void GuiController::InitConnections()
{
    mw->fMenuFile->Connect("Activated(int)", "GuiController", this, "HandleMenu(int)");

    for (int i=0; i<3; i++) {
        cw->threshEntry[i]->SetNumber(data->wfs.at(i)->threshold);
    }
    cw->threshEntry[0]->Connect("ValueSet(Long_t)", "GuiController", this, "ThresholdUChanged()");
    cw->threshEntry[1]->Connect("ValueSet(Long_t)", "GuiController", this, "ThresholdVChanged()");
    cw->threshEntry[2]->Connect("ValueSet(Long_t)", "GuiController", this, "ThresholdWChanged()");
    cw->setThreshButton->Connect("Clicked()", "GuiController", this, "SetChannelThreshold()");
    cw->threshScaleEntry->Connect("ValueSet(Long_t)", "GuiController", this, "SetChannelThreshold()");

    cw->zAxisRangeEntry[0]->SetNumber(data->wfs.at(0)->zmin);
    cw->zAxisRangeEntry[1]->SetNumber(data->wfs.at(0)->zmax);
    cw->zAxisRangeEntry[0]->Connect("ValueSet(Long_t)", "GuiController", this, "ZRangeChanged()");
    cw->zAxisRangeEntry[1]->Connect("ValueSet(Long_t)", "GuiController", this, "ZRangeChanged()");

    cw->timeRangeEntry[0]->SetNumber(0);
    cw->timeRangeEntry[1]->SetNumber(data->wfs.at(0)->nTDCs);

    cw->channelEntry->Connect("ValueSet(Long_t)", "GuiController", this, "ChannelChanged()");
    cw->timeEntry->SetLimits(TGNumberFormat::kNELLimitMinMax, 0, data->wfs.at(0)->nTDCs);
    cw->timeEntry->Connect("ValueSet(Long_t)", "GuiController", this, "TimeChanged()");
    cw->badChanelButton->Connect("Clicked()", "GuiController", this, "UpdateShowBadChannel()");
    cw->badChanelButton->SetToolTipText(TString::Format("U: %lu, V: %lu, W: %lu",
        data->wfs.at(0)->lines.size(),
        data->wfs.at(1)->lines.size(),
        data->wfs.at(2)->lines.size()
    ));
    cw->rawWfButton->Connect("Clicked()", "GuiController", this, "UpdateShowRaw()");
    cw->unZoomButton->Connect("Clicked()", "GuiController", this, "UnZoom()");

    cw->regionSumBtn->Connect("Clicked()", "GuiController", this, "ShowRegionWindow()");
    cw->rmsBtn->Connect("Clicked()", "GuiController", this, "ShowRmsWindow()");

    // stupid way to connect signal and slots
    vw->can->GetPad(1)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis0()");
    vw->can->GetPad(2)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis1()");
    vw->can->GetPad(3)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis2()");
    vw->can->GetPad(4)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis3()");
    vw->can->GetPad(5)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis4()");
    vw->can->GetPad(6)->Connect("RangeChanged()", "GuiController", this, "SyncTimeAxis5()");
    // vw->can->GetPad(7)->Connect("RangeChanged()", "GuiController", this, "WfRangeChanged0()");
    // vw->can->GetPad(8)->Connect("RangeChanged()", "GuiController", this, "WfRangeChanged1()");
    // vw->can->GetPad(9)->Connect("RangeChanged()", "GuiController", this, "WfRangeChanged2()");


    vw->can->Connect(
        "ProcessedEvent(Int_t,Int_t,Int_t,TObject*)",
        "GuiController",
        this,
        "ProcessCanvasEvent(Int_t,Int_t,Int_t,TObject*)"
    );
}

void GuiController::UpdateShowBadChannel()
{
    if (cw->badChanelButton->IsDown()) {
        for (int ind=0; ind<6; ind++) {
            vw->can->cd(ind+1);
            data->wfs.at(ind)->DrawLines();
            vw->can->GetPad(ind+1)->Modified();
            vw->can->GetPad(ind+1)->Update();
        }
    }
    else {
        for (int ind=0; ind<6; ind++) {
            vw->can->cd(ind+1);
            data->wfs.at(ind)->HideLines();
            vw->can->GetPad(ind+1)->Modified();
            vw->can->GetPad(ind+1)->Update();
        }
    }

}

void GuiController::ThresholdChanged(int i)
{
    // newThresh is an ADC-unit cutoff (widget is seeded from denoised display units
    // where fScale=1, so widget value == ADC value).  Apply it as an ADC cutoff to
    // all waveforms for this plane so both denoised and decon cut at the same ADC
    // level: pass newThresh * fScale so that |hOrig*fScale| > newThresh*fScale
    // reduces to |hOrig_ADC| > newThresh.
    // Widget value is the denoised ADC threshold.  Decon is kept at a fixed ratio
    // (decon_scaling / denoised_scaling) higher in ADC space, matching the startup
    // per-channel relationship.
    int newThresh = cw->threshEntry[i]->GetNumber();
    double scalingRatio = data->decon_scaling / data->denoised_scaling;
    for (int ind=i; ind<6; ind+=3) {
        vw->can->cd(ind+1);
        double fS = data->wfs.at(ind)->fScale;
        double adcCutoff = (ind < 3) ? newThresh : newThresh * scalingRatio;
        double displayThresh = adcCutoff * fS;
        data->wfs.at(ind)->SetThreshold(displayThresh);
        data->wfs.at(ind)->Draw2D();
        vw->can->GetPad(ind+1)->Modified();
        vw->can->GetPad(ind+1)->Update();
    }
}

void GuiController::SetChannelThreshold()
{
    // Reset all 6 waveforms (denoised + decon for all 3 planes) to per-channel
    // Wiener thresholds.  Denoised always uses scaling=0.5; decon uses the
    // user-entered multiplier from threshScaleEntry.
    TH1I *ht = 0;
    double decon_scale = cw->threshScaleEntry->GetNumber();
    for (int ind=0; ind<6; ind++) {
        vw->can->cd(ind+1);
        ht = data->thresh_histos.at(ind % 3);
        double scaling = (ind < 3) ? data->denoised_scaling : decon_scale;
        data->wfs.at(ind)->SetThreshold(ht, scaling);
        // update the widget to reflect the new denoised threshold
        if (ind < 3) cw->threshEntry[ind]->SetNumber(data->wfs.at(ind)->threshold);
        data->wfs.at(ind)->Draw2D();
        vw->can->GetPad(ind+1)->Modified();
        vw->can->GetPad(ind+1)->Update();
    }
}

void GuiController::UnZoom()
{
    cw->timeRangeEntry[0]->SetNumber(0);
    cw->timeRangeEntry[1]->SetNumber(data->wfs.at(0)->nTDCs);
    cw->adcRangeEntry[0]->SetNumber(0);
    cw->adcRangeEntry[1]->SetNumber(0);

    for (int ind=0; ind<6; ind++) {
        data->wfs.at(ind)->hDummy->GetXaxis()->UnZoom();
        data->wfs.at(ind)->hDummy->GetYaxis()->UnZoom();
        vw->can->GetPad(ind+1)->Modified();
        vw->can->GetPad(ind+1)->Update();
    }
}

void GuiController::ZRangeChanged()
{
    int min = cw->zAxisRangeEntry[0]->GetNumber();
    int max = cw->zAxisRangeEntry[1]->GetNumber();
    for (int ind=0; ind<6; ind++) {
        vw->can->cd(ind+1);
        data->wfs.at(ind)->SetZRange(min, max);
        data->wfs.at(ind)->Draw2D();
        vw->can->GetPad(ind+1)->Modified();
        vw->can->GetPad(ind+1)->Update();
    }

}

void GuiController::SyncTimeAxis(int i)
{
    double min = data->wfs.at(i)->hDummy->GetYaxis()->GetFirst();
    double max = data->wfs.at(i)->hDummy->GetYaxis()->GetLast();
    // double min = vw->can->GetPad(i+1)->GetUymin();
    // double max = vw->can->GetPad(i+1)->GetUymax();

    for (int ind=0; ind<6; ind++) {
        if (i==ind) continue;
        data->wfs.at(ind)->hDummy->GetYaxis()->SetRange(min, max);
        vw->can->GetPad(ind+1)->Modified();
        vw->can->GetPad(ind+1)->Update();
    }

    // cout << "range changed: " << min << ", " << max << endl;
}

void GuiController::WfRangeChanged(int i)
{
    // can't figureout how to get the axis range in user coordinate ...
    // ( not pad->GetUxmin() etc, nor axis->GetFirst() etc. )
}

void GuiController::UpdateShowRaw()
{
    int channel = cw->channelEntry->GetNumber();
    cout << "channel: " << channel << endl;
    // int wfsNo = 0;
    // if (channel>=data->wfs.at(1)->firstChannel && channel<data->wfs.at(2)->firstChannel) wfsNo = 1;
    // else if (channel>=data->wfs.at(2)->firstChannel) wfsNo = 2;

    int wfsNo = data->GetPlaneNo(channel);
    cout << "plane: " << data->GetPlaneNo(channel) << endl;

    int padNo = wfsNo+7;
    vw->can->cd(padNo);

    TH1I *hh = data->raw_wfs.at(wfsNo)->Draw1D(channel, "same");
    if (cw->rawWfButton->IsDown()) {
        hh->SetLineColor(kBlue);
        // hMain->SetTitle( TString::Format("%s, %s", hMain->GetTitle(), hh->GetTitle()) );
    }
    else {
        gPad->GetListOfPrimitives()->Remove(hh);
        // hMain->SetTitle( TString::Format("%s, %s", hMain->GetTitle(), hh->GetTitle()) );
    }

    vw->can->GetPad(padNo)->Modified();
    vw->can->GetPad(padNo)->Update();
}

void GuiController::ChannelChanged()
{
    if (cw->timeModeButton->IsDown()) {
        return; // skip if time mode is selected
    }
    if (cw->badOnlyButton->IsDown()){
        int curr = cw->channelEntry->GetNumber();
        int wfsNum = data->GetPlaneNo(curr);
        BadChannels* bad_channels = data->wfs.at(wfsNum)->bad_channels;
        vector<int> bad_id = bad_channels->bad_id;
        int next = 0;
        auto it = std::find(bad_id.begin(), bad_id.end(), curr);
        if (it!=bad_id.end()){
            next = *it;
        }
        else{
            it = std::upper_bound(bad_id.begin(), bad_id.end(), curr); // find first element greater
            if(it!=bad_id.end()){
                next = *it;
            }            
        }
        cw->channelEntry->SetNumber(next);
    }

    int channel = cw->channelEntry->GetNumber();
    cout << "channel: " << channel << endl;
    // int wfsNo = 0;
    // if (channel>=data->wfs.at(1)->firstChannel && channel<data->wfs.at(2)->firstChannel) wfsNo = 1;
    // else if (channel>=data->wfs.at(2)->firstChannel) wfsNo = 2;
    int wfsNo = data->GetPlaneNo(channel);
    cout << "plane: " << data->GetPlaneNo(channel) << endl;

    int padNo = wfsNo+7;
    vw->can->cd(padNo);

    std::string comment = data->channel_status[channel];
    TH1F *hwf = data->wfs.at(wfsNo)->Draw1D(channel, "", comment.c_str());
    hCurrent[wfsNo] = hwf;
    hwf->SetLineColor(kBlack);


    TString name = TString::Format("hWire_%s_2d_dummy", data->wfs.at(wfsNo)->fName.Data());
    TH2F *hMain = (TH2F*)gDirectory->FindObject(name);
    if (!hMain) {
        cout << "Error: cannot find " << name << endl;
        return;
    }

    hMain->GetXaxis()->SetRangeUser(cw->timeRangeEntry[0]->GetNumber(), cw->timeRangeEntry[1]->GetNumber());
    if (binary_search(data->bad_channels->bad_id.begin(), data->bad_channels->bad_id.end(), channel)) {
        hMain->SetTitle( TString::Format("%s (bad channel)", hMain->GetTitle()) );
    }

    TH1F *h = data->wfs.at(wfsNo+3)->Draw1D(channel, "same" ); // draw decon (red)
    h->SetLineColor(kRed);

    TH1I *ht = data->thresh_histos.at(wfsNo);
    int thresh = ht->GetBinContent(ht->GetXaxis()->FindBin(channel));
    cout << "thresh: " << thresh << endl;
    TLine *l = new TLine(0, thresh/500., data->wfs.at(wfsNo)->nTDCs, thresh/500.);
    l->SetLineColor(kMagenta);
    l->SetLineWidth(2);
    l->Draw();

    // RMS overlay: ±4σ lines when analysis results are loaded and checkbox is on
    if (rmsLoaded && rmsOverlayCheck && rmsOverlayCheck->IsDown()) {
        int idx = channel - data->wfs.at(wfsNo)->firstChannel;
        if (idx >= 0 && idx < (int)rmsResults[wfsNo].size()) {
            float rmsDisplay = rmsResults[wfsNo][idx].rms_final * data->wfs.at(wfsNo)->fScale;
            int nT = data->wfs.at(wfsNo)->nTDCs;
            TLine *rp = new TLine(0,  rmsDisplay * 4, nT,  rmsDisplay * 4);
            TLine *rm = new TLine(0, -rmsDisplay * 4, nT, -rmsDisplay * 4);
            rp->SetLineColor(kCyan + 2); rp->SetLineWidth(2); rp->SetLineStyle(2);
            rm->SetLineColor(kCyan + 2); rm->SetLineWidth(2); rm->SetLineStyle(2);
            rp->Draw(); rm->Draw();
        }
    }

    TH1I *hh = nullptr;
    if (cw->rawWfButton->IsDown()) {
        hh = data->raw_wfs.at(wfsNo)->Draw1D(channel, "same");
        hh->SetLineColor(kBlue);
        hMain->SetTitle( TString::Format("%s, %s", hMain->GetTitle(), hh->GetTitle()) );
    }

    // Smart Y-axis range: derive from actual signal rather than the denoised
    // waveform which may be empty. Manual ADC range entries override auto-range.
    {
        int adc_min = cw->adcRangeEntry[0]->GetNumber();
        int adc_max = cw->adcRangeEntry[1]->GetNumber();
        if (adc_max > adc_min) {
            hMain->GetYaxis()->SetRangeUser(adc_min, adc_max);
        } else {
            // Start from decon waveform (primary signal source)
            double ylo = h->GetMinimum(), yhi = h->GetMaximum();
            // Expand to include denoised if it has real content
            if (hwf->GetMaximum() > hwf->GetMinimum()) {
                ylo = std::min(ylo, hwf->GetMinimum());
                yhi = std::max(yhi, hwf->GetMaximum());
            }
            // Expand to include raw if it is visible
            if (hh && hh->GetMaximum() > hh->GetMinimum()) {
                ylo = std::min(ylo, (double)hh->GetMinimum());
                yhi = std::max(yhi, (double)hh->GetMaximum());
            }
            if (yhi > ylo) {
                double pad = 0.1 * (yhi - ylo);
                hMain->GetYaxis()->SetRangeUser(ylo - pad, yhi + pad);
            } else {
                // Flat / empty channel — show a small symmetric range
                hMain->GetYaxis()->SetRangeUser(-1.0, 1.0);
            }
        }
    }

    // mask the bad channel region
    if (cw->badChanelButton->IsDown()){
        BadChannels* bad_channels = data->wfs.at(wfsNo)->bad_channels;
        vector<int> bad_id = bad_channels->bad_id;
        int idx=0;
        for(auto& ch: bad_id){
            if(ch==channel){
                vector<int> bad_start = bad_channels->bad_start;
                vector<int> bad_end = bad_channels->bad_end;
                TLine* lh = new TLine(bad_start.at(idx), hwf->GetMinimum(), bad_start.at(idx), hwf->GetMaximum());
                TLine* rh = new TLine(bad_end.at(idx), hwf->GetMinimum(), bad_end.at(idx), hwf->GetMaximum());
                lh->SetLineColor(2); lh->SetLineStyle(2); lh->SetLineWidth(2);
                rh->SetLineColor(2); rh->SetLineStyle(2); rh->SetLineWidth(2);
                lh->Draw("same");
                rh->Draw("same");
                cout << "find a bad channel :" << channel << " start: " << bad_start.at(idx) << " end: " << bad_end.at(idx) << endl;

                TBox* breg = new TBox(bad_start.at(idx), hwf->GetMinimum(), bad_end.at(idx), hwf->GetMaximum());
                breg->SetFillStyle(3335);
                breg->SetFillColor(kRed);
                // gStyle->SetHatchesSpacing(5);
                breg->Draw("same");
            }
            idx ++;
        }
    }

    vw->can->GetPad(padNo)->SetGridx();
    vw->can->GetPad(padNo)->SetGridy();
    vw->can->GetPad(padNo)->Modified();
    vw->can->GetPad(padNo)->Update();
    // if (cw->badOnlyButton->IsDown()){ // evil mode & print figures
    //     std:string pwd(gSystem->WorkingDirectory());
    //     pwd += "/../data/Channel" + std::to_string(channel) + ".png";
    //     vw->can->GetPad(padNo)->Print(pwd.c_str());
    //     // std::cerr << "[wgu] print a channel" << channel << " path: " << pwd << endl;
    // }
}

void GuiController::TimeChanged()
{
    if (cw->timeModeButton->IsDown()) {

        int tickNo = cw->timeEntry->GetNumber();
        TH1F *hTick  = 0;
        for (int k=3; k<=5; k++) { // only draw decon signal
            int padNo = k+4;
            vw->can->cd(padNo);
            hTick = data->wfs.at(k)->Draw1DTick(tickNo); // draw time
            hTick->SetLineColor(kRed);

            TString name = TString::Format("hth_%i", k-3);
            TH1I *hth = (TH1I*)gDirectory->FindObject(name);
            if (hth) delete hth;

            hth = (TH1I*)data->thresh_histos.at(k-3)->Clone(name.Data());
            hth->Scale(data->wfs.at(k)->fScale);
            hth->Draw("same");
            hth->SetLineColor(kBlack);

            int channel_min = cw->timeRangeEntry[0]->GetNumber();
            int channel_max = cw->timeRangeEntry[1]->GetNumber();

            if (channel_min>0) {
                hTick->GetXaxis()->SetRangeUser(channel_min, channel_max);
            }

            int adc_min = cw->adcRangeEntry[0]->GetNumber();
            int adc_max = cw->adcRangeEntry[1]->GetNumber();
            if (adc_max > adc_min) {
                hTick->GetYaxis()->SetRangeUser(adc_min, adc_max);
            }

            vw->can->GetPad(padNo)->SetGridx();
            vw->can->GetPad(padNo)->SetGridy();
            vw->can->GetPad(padNo)->Modified();
            vw->can->GetPad(padNo)->Update();
        }
    }
}

void GuiController::ProcessCanvasEvent(Int_t ev, Int_t x, Int_t y, TObject *selected)
{
    if (ev == 11) { // clicked
        if (!(selected->IsA() == TH2F::Class()
            || selected->IsA() == TBox::Class()
            || selected->IsA() == TLine::Class()
        )) return;
        TVirtualPad* pad = vw->can->GetClickSelectedPad();
        int padNo = pad->GetNumber();
        double xx = pad->AbsPixeltoX(x);
        double yy = pad->AbsPixeltoY(y);
        cout << "pad " << padNo << ": (" << xx << ", " << yy << ")" << endl;

        // Region-sum capture mode: fill start/end fields on click in decon pads (4-6)
        if (captureMode != CAPTURE_NONE && padNo >= 4 && padNo <= 6) {
            if (!regionWindow) { captureMode = CAPTURE_NONE; return; }
            int plane = padNo - 4;
            int chanNo = TMath::Nint(xx);
            int tickNo = TMath::Nint(yy);
            if (captureMode == CAPTURE_START) {
                regChStart[plane]->SetNumber(chanNo);
                regTLowS[plane]->SetNumber(tickNo);
                regTHighS[plane]->SetNumber(tickNo);
                cout << "Set Start: plane " << plane << "  ch=" << chanNo << "  tick=" << tickNo << endl;
            } else {
                regChEnd[plane]->SetNumber(chanNo);
                regTLowE[plane]->SetNumber(tickNo);
                regTHighE[plane]->SetNumber(tickNo);
                cout << "Set End: plane " << plane << "  ch=" << chanNo << "  tick=" << tickNo << endl;
            }
            return;  // keep mode active; user clicks Set Start/End again to exit
        }

        int drawPad = (padNo-1) % 3 + 7;
        vw->can->cd(drawPad);
        if (padNo<=6) {
            int wfNo = padNo - 1;
            wfNo = wfNo < 3 ? wfNo : wfNo-3;  // draw raw first
            int chanNo = TMath::Nint(xx); // round
            int tickNo = TMath::Nint(yy); // round
            // data->wfs.at(wfNo)->Draw1D(chanNo);
            // TH1F *h = data->wfs.at(wfNo+3)->Draw1D(chanNo, "same"); // draw calib
            // h->SetLineColor(kRed);
            // TH1I *hh = data->raw_wfs.at(wfNo)->Draw1D(chanNo, "same"); // draw calib
            // hh->SetLineColor(kBlue);
            cw->channelEntry->SetNumber(chanNo);
            cw->timeEntry->SetNumber(tickNo);

            ChannelChanged();
            TimeChanged();

            // cw->timeRangeEntry[0]->SetNumber(0);
            // cw->timeRangeEntry[1]->SetNumber(data->wfs.at(0)->nTDCs);
        }
        // vw->can->GetPad(drawPad)->Modified();
        // vw->can->GetPad(drawPad)->Update();
    }

}

void GuiController::ShowRegionWindow()
{
    if (!regionWindow) {
        regionWindow = new TGMainFrame(gClient->GetRoot(), 700, 520);
        regionWindow->SetWindowName("Region Sum");
        regionWindow->DontCallClose();
        regionWindow->Connect("CloseWindow()", "GuiController", this, "HideRegionWindow()");

        // ---- Control group ----
        TGGroupFrame* ctrl = new TGGroupFrame(regionWindow, "Region Selection", kVerticalFrame);
        regionWindow->AddFrame(ctrl, new TGLayoutHints(kLHintsTop | kLHintsExpandX, 5, 5, 5, 2));

        // Header row
        {
            TGHorizontalFrame* hdr = new TGHorizontalFrame(ctrl);
            ctrl->AddFrame(hdr, new TGLayoutHints(kLHintsTop | kLHintsLeft, 2, 2, 2, 0));
            hdr->AddFrame(new TGLabel(hdr, "      "),  new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));
            hdr->AddFrame(new TGLabel(hdr, " ch start"), new TGLayoutHints(kLHintsLeft, 18, 2, 1, 1));
            hdr->AddFrame(new TGLabel(hdr, "  t start low  -  high"), new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));
            hdr->AddFrame(new TGLabel(hdr, "  ch end"), new TGLayoutHints(kLHintsLeft, 18, 2, 1, 1));
            hdr->AddFrame(new TGLabel(hdr, "  t end low  -  high"), new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));
        }

        static const char* planeName[3] = {"U:", "V:", "W:"};
        for (int p = 0; p < 3; ++p) {
            TGHorizontalFrame* row = new TGHorizontalFrame(ctrl);
            ctrl->AddFrame(row, new TGLayoutHints(kLHintsTop | kLHintsLeft, 2, 2, 3, 3));

            row->AddFrame(new TGLabel(row, planeName[p]),
                new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 5, 5, 2, 2));

            // ch_start
            regChStart[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 11263);
            row->AddFrame(regChStart[p], new TGLayoutHints(kLHintsLeft, 5, 2, 1, 1));

            // t_low_s - t_high_s
            regTLowS[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 10000);
            row->AddFrame(regTLowS[p], new TGLayoutHints(kLHintsLeft, 10, 2, 1, 1));
            row->AddFrame(new TGLabel(row, "-"),
                new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 3, 3, 2, 2));
            regTHighS[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 10000);
            row->AddFrame(regTHighS[p], new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));

            // ch_end
            regChEnd[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 11263);
            row->AddFrame(regChEnd[p], new TGLayoutHints(kLHintsLeft, 10, 2, 1, 1));

            // t_low_e - t_high_e
            regTLowE[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 10000);
            row->AddFrame(regTLowE[p], new TGLayoutHints(kLHintsLeft, 10, 2, 1, 1));
            row->AddFrame(new TGLabel(row, "-"),
                new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 3, 3, 2, 2));
            regTHighE[p] = new TGNumberEntry(row, 0, 6, -1, TGNumberFormat::kNESInteger,
                TGNumberFormat::kNEANonNegative, TGNumberFormat::kNELLimitMinMax, 0, 10000);
            row->AddFrame(regTHighE[p], new TGLayoutHints(kLHintsLeft, 2, 2, 1, 1));
        }

        // Button row
        {
            TGHorizontalFrame* btnRow = new TGHorizontalFrame(ctrl);
            ctrl->AddFrame(btnRow, new TGLayoutHints(kLHintsTop | kLHintsCenterX, 5, 5, 5, 5));

            TGTextButton* b;
            b = new TGTextButton(btnRow, "Set Start");
            b->Connect("Clicked()", "GuiController", this, "SetStartMode()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

            b = new TGTextButton(btnRow, "Set End");
            b->Connect("Clicked()", "GuiController", this, "SetEndMode()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

            b = new TGTextButton(btnRow, "Sum");
            b->Connect("Clicked()", "GuiController", this, "SumRegion()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

            b = new TGTextButton(btnRow, "Draw");
            b->Connect("Clicked()", "GuiController", this, "DrawRegion()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

            b = new TGTextButton(btnRow, "Erase");
            b->Connect("Clicked()", "GuiController", this, "EraseRegion()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

            b = new TGTextButton(btnRow, "Clear");
            b->Connect("Clicked()", "GuiController", this, "ClearRegion()");
            btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));
        }

        // ---- Embedded canvas for histogram display ----
        regionCanvas = new TRootEmbeddedCanvas("regionCanvas", regionWindow, 690, 300);
        regionWindow->AddFrame(regionCanvas,
            new TGLayoutHints(kLHintsTop | kLHintsExpandX | kLHintsExpandY, 5, 5, 2, 5));

        regionWindow->MapSubwindows();
        regionWindow->Resize(regionWindow->GetDefaultSize());
        regionWindow->MapWindow();
    } else {
        regionWindow->RaiseWindow();
        regionWindow->MapWindow();
    }
}

void GuiController::HideRegionWindow()
{
    if (regionWindow) regionWindow->UnmapWindow();
}

void GuiController::SetStartMode()
{
    if (!regionWindow) ShowRegionWindow();
    captureMode = (captureMode == CAPTURE_START) ? CAPTURE_NONE : CAPTURE_START;
    cout << "Region sum: " << (captureMode == CAPTURE_START
        ? "click on a decon pad to set start channel/tick (click Set Start again to exit)"
        : "Set Start mode off") << endl;
}

void GuiController::SetEndMode()
{
    if (!regionWindow) ShowRegionWindow();
    captureMode = (captureMode == CAPTURE_END) ? CAPTURE_NONE : CAPTURE_END;
    cout << "Region sum: " << (captureMode == CAPTURE_END
        ? "click on a decon pad to set end channel/tick (click Set End again to exit)"
        : "Set End mode off") << endl;
}

void GuiController::SumRegion()
{
    if (!regionWindow || !regionCanvas) {
        cout << "SumRegion: open Region Sum window first" << endl;
        return;
    }

    // Per-plane time ranges
    double tLowS[3], tHighS[3], tLowE[3], tHighE[3];
    for (int p = 0; p < 3; ++p) {
        tLowS[p]  = regTLowS[p]->GetNumber();
        tHighS[p] = regTHighS[p]->GetNumber();
        tLowE[p]  = regTLowE[p]->GetNumber();
        tHighE[p] = regTHighE[p]->GetNumber();
    }

    // Global tick axis range: union over all planes
    double tMin = tLowS[0], tMax = tHighS[0];
    for (int p = 0; p < 3; ++p) {
        tMin = std::min({tMin, tLowS[p], tHighS[p], tLowE[p], tHighE[p]});
        tMax = std::max({tMax, tLowS[p], tHighS[p], tLowE[p], tHighE[p]});
    }
    if (tMax <= tMin) {
        cout << "SumRegion: time range is empty -- set t start/end values first" << endl;
        return;
    }

    static const char* planeLetter[3] = {"U", "V", "W"};
    static const Color_t planeColor[3] = {kRed, kBlue, kGreen+2};

    TCanvas* sc = regionCanvas->GetCanvas();
    sc->cd();
    sc->Clear();

    TH2F* hRef = data->wfs.at(3)->hOrig;
    int nTDCs  = hRef->GetNbinsY();
    double yAxisLow  = hRef->GetYaxis()->GetBinLowEdge(1);
    double yAxisHigh = hRef->GetYaxis()->GetBinUpEdge(nTDCs);

    TH1F* sumHist[3] = {};
    for (int p = 0; p < 3; ++p) {
        Waveforms* w = data->wfs.at(p + 3);
        TH2F* h = w->hOrig;

        double csVal = regChStart[p]->GetNumber();
        double ceVal = regChEnd[p]->GetNumber();
        if (csVal > ceVal) std::swap(csVal, ceVal);

        int csBin = h->GetXaxis()->FindBin(csVal);
        int ceBin = h->GetXaxis()->FindBin(ceVal);
        if (csBin > ceBin) std::swap(csBin, ceBin);

        TString sname = TString::Format("hRegionSum_%s", planeLetter[p]);
        TH1F* hExist = (TH1F*)gDirectory->FindObject(sname);
        if (hExist) delete hExist;

        TH1F* sum = new TH1F(sname, sname, nTDCs, yAxisLow, yAxisHigh);

        for (int i = csBin; i <= ceBin; ++i) {
            double frac = (ceBin == csBin) ? 0.0
                : (double)(i - csBin) / (double)(ceBin - csBin);
            double tl = tLowS[p]  + frac * (tLowE[p]  - tLowS[p]);
            double th = tHighS[p] + frac * (tHighE[p] - tHighS[p]);
            if (tl > th) std::swap(tl, th);
            int jLow  = h->GetYaxis()->FindBin(tl);
            int jHigh = h->GetYaxis()->FindBin(th);
            for (int j = jLow; j <= jHigh; ++j) {
                sum->AddBinContent(j, h->GetBinContent(i, j) * w->fScale);
            }
        }
        sumHist[p] = sum;
        cout << "SumRegion " << planeLetter[p] << ": ch "
             << (int)csVal << "-" << (int)ceVal
             << "  t[" << tLowS[p] << "-" << tHighS[p] << " -> "
             << tLowE[p] << "-" << tHighE[p] << "]"
             << "  integral=" << sum->Integral() << endl;
    }

    // Shared y range across all planes
    double yHi = -1e30, yLo = 1e30;
    for (int p = 0; p < 3; ++p) {
        if (sumHist[p]->GetMaximum() > yHi) yHi = sumHist[p]->GetMaximum();
        if (sumHist[p]->GetMinimum() < yLo) yLo = sumHist[p]->GetMinimum();
    }
    double yPad = (yHi > yLo) ? 0.1 * (yHi - yLo) : 1.0;

    TLegend* leg = new TLegend(0.75, 0.7, 0.95, 0.9);
    for (int p = 0; p < 3; ++p) {
        sumHist[p]->SetLineColor(planeColor[p]);
        sumHist[p]->SetLineWidth(2);
        sumHist[p]->GetXaxis()->SetRangeUser(tMin, tMax);
        sumHist[p]->GetXaxis()->SetTitle("ticks");
        sumHist[p]->GetYaxis()->SetTitle("summed signal");
        if (yHi > yLo)
            sumHist[p]->GetYaxis()->SetRangeUser(yLo - yPad, yHi + yPad);
        sumHist[p]->Draw(p == 0 ? "hist" : "hist same");

        TString entry = TString::Format("%s ch %d-%d", planeLetter[p],
            (int)regChStart[p]->GetNumber(),
            (int)regChEnd[p]->GetNumber());
        leg->AddEntry(sumHist[p], entry, "l");
    }
    leg->Draw();
    sc->Update();
}

void GuiController::DrawRegion()
{
    EraseRegion();  // remove any previous drawing first
    for (int p = 0; p < 3; ++p) {
        if (!regChStart[p]) continue;
        double cs  = regChStart[p]->GetNumber();
        double ce  = regChEnd[p]->GetNumber();
        double tls = regTLowS[p]->GetNumber();
        double ths = regTHighS[p]->GetNumber();
        double tle = regTLowE[p]->GetNumber();
        double the = regTHighE[p]->GetNumber();

        int padNo = p + 4;  // pads 4/5/6 = U/V/W decon
        vw->can->cd(padNo);

        // edge 0: left side (at ch_start), t_low_s to t_high_s
        regionBoundary[p][0] = new TLine(cs, tls, cs, ths);
        // edge 1: right side (at ch_end), t_low_e to t_high_e
        regionBoundary[p][1] = new TLine(ce, tle, ce, the);
        // edge 2: top — t_high_s to t_high_e
        regionBoundary[p][2] = new TLine(cs, ths, ce, the);
        // edge 3: bottom — t_low_s to t_low_e
        regionBoundary[p][3] = new TLine(cs, tls, ce, tle);

        for (int e = 0; e < 4; ++e) {
            regionBoundary[p][e]->SetLineColor(kOrange+7);
            regionBoundary[p][e]->SetLineWidth(2);
            regionBoundary[p][e]->SetLineStyle(2);  // dashed
            regionBoundary[p][e]->Draw();
        }
        vw->can->GetPad(padNo)->Modified();
        vw->can->GetPad(padNo)->Update();
    }
}

void GuiController::EraseRegion()
{
    for (int p = 0; p < 3; ++p) {
        int padNo = p + 4;
        TVirtualPad* pad = vw->can->GetPad(padNo);
        bool changed = false;
        for (int e = 0; e < 4; ++e) {
            if (regionBoundary[p][e]) {
                pad->GetListOfPrimitives()->Remove(regionBoundary[p][e]);
                delete regionBoundary[p][e];
                regionBoundary[p][e] = nullptr;
                changed = true;
            }
        }
        if (changed) {
            pad->Modified();
            pad->Update();
        }
    }
}

void GuiController::ClearRegion()
{
    EraseRegion();
    for (int p = 0; p < 3; ++p) {
        if (regChStart[p])  regChStart[p]->SetNumber(0);
        if (regChEnd[p])    regChEnd[p]->SetNumber(0);
        if (regTLowS[p])    regTLowS[p]->SetNumber(0);
        if (regTHighS[p])   regTHighS[p]->SetNumber(0);
        if (regTLowE[p])    regTLowE[p]->SetNumber(0);
        if (regTHighE[p])   regTHighE[p]->SetNumber(0);
    }
    captureMode = CAPTURE_NONE;
    if (regionCanvas) {
        TCanvas* sc = regionCanvas->GetCanvas();
        sc->Clear();
        sc->Update();
    }
}

// ---- RMS Analysis panel ----

void GuiController::ShowRmsWindow()
{
    if (!rmsWindow) {
        rmsWindow = new TGMainFrame(gClient->GetRoot(), 440, 200);
        rmsWindow->SetWindowName("RMS Analysis");
        rmsWindow->DontCallClose();
        rmsWindow->Connect("CloseWindow()", "GuiController", this, "HideRmsWindow()");

        // Status row
        TGHorizontalFrame* statusRow = new TGHorizontalFrame(rmsWindow);
        rmsWindow->AddFrame(statusRow, new TGLayoutHints(kLHintsTop | kLHintsExpandX, 8, 8, 8, 2));
        statusRow->AddFrame(new TGLabel(statusRow, "Cache: "),
            new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 2, 2, 2, 2));
        rmsStatusLabel = new TGLabel(statusRow, "(not loaded)");
        statusRow->AddFrame(rmsStatusLabel,
            new TGLayoutHints(kLHintsLeft | kLHintsCenterY, 2, 2, 2, 2));

        // Button row
        TGHorizontalFrame* btnRow = new TGHorizontalFrame(rmsWindow);
        rmsWindow->AddFrame(btnRow, new TGLayoutHints(kLHintsTop | kLHintsCenterX, 8, 8, 6, 4));

        TGTextButton* b;
        b = new TGTextButton(btnRow, "Compute RMS");
        b->Connect("Clicked()", "GuiController", this, "ComputeRms()");
        btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

        b = new TGTextButton(btnRow, "Load from file");
        b->Connect("Clicked()", "GuiController", this, "LoadRmsFromFile()");
        btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

        b = new TGTextButton(btnRow, "Show distribution");
        b->Connect("Clicked()", "GuiController", this, "ShowRmsDistribution()");
        btnRow->AddFrame(b, new TGLayoutHints(kLHintsLeft, 5, 5, 2, 2));

        // Overlay checkbox
        TGHorizontalFrame* overlayRow = new TGHorizontalFrame(rmsWindow);
        rmsWindow->AddFrame(overlayRow, new TGLayoutHints(kLHintsTop | kLHintsLeft, 8, 8, 4, 8));
        rmsOverlayCheck = new TGCheckButton(overlayRow, "Overlay 4σ on 1D waveform");
        rmsOverlayCheck->SetState(kButtonUp);
        rmsOverlayCheck->Connect("Toggled(Bool_t)", "GuiController", this, "ToggleRmsOverlay()");
        overlayRow->AddFrame(rmsOverlayCheck, new TGLayoutHints(kLHintsLeft, 2, 2, 2, 2));

        rmsWindow->MapSubwindows();
        rmsWindow->Resize(rmsWindow->GetDefaultSize());
        rmsWindow->MapWindow();
    } else {
        rmsWindow->RaiseWindow();
        rmsWindow->MapWindow();
    }

    // Refresh status label to show current cache filename
    TString cacheFile = RmsAnalyzer::CacheFilename(data->rootFile->GetName());
    bool exists = !gSystem->AccessPathName(cacheFile.Data());
    rmsStatusLabel->SetText(
        exists ? TString::Format("%s  [EXISTS]", cacheFile.Data()).Data()
               : TString::Format("%s  [not found]", cacheFile.Data()).Data()
    );
    rmsWindow->Layout();
}

void GuiController::HideRmsWindow()
{
    if (rmsWindow) rmsWindow->UnmapWindow();
}

void GuiController::ComputeRms()
{
    printf("RMS Analysis: computing (with FFT) ...\n");
    static const char* fftKey[3] = {"fft_u", "fft_v", "fft_w"};
    TH2F* newFft[3] = {nullptr, nullptr, nullptr};

    for (int p = 0; p < 3; ++p) {
        TH2F* h = data->wfs.at(p)->hOrig;
        if (!h) {
            printf("RMS Analysis: plane %d hOrig is null, skipping\n", p);
            rmsResults[p].clear();
            continue;
        }
        printf("  plane %d (%s): %d channels x %d ticks\n",
            p, h->GetName(), h->GetNbinsX(), h->GetNbinsY());
        rmsResults[p] = RmsAnalyzer::AnalyzePlaneWithFft(h, fftKey[p], newFft[p]);
        gSystem->ProcessEvents();
    }

    // Replace stored FFT spectra
    for (int p = 0; p < 3; ++p) {
        delete fftSpec[p];
        fftSpec[p] = newFft[p];
        fftSelectedCh[p] = -1;
    }

    TString cacheFile = RmsAnalyzer::CacheFilename(data->rootFile->GetName());
    RmsAnalyzer::Save(rmsResults[0], rmsResults[1], rmsResults[2],
                      fftSpec[0], fftSpec[1], fftSpec[2], cacheFile.Data());
    rmsLoaded = true;

    if (rmsStatusLabel)
        rmsStatusLabel->SetText(
            TString::Format("%s  [EXISTS]", cacheFile.Data()).Data()
        );
    if (rmsWindow) rmsWindow->Layout();
    printf("RMS Analysis: done.\n");
}

void GuiController::LoadRmsFromFile()
{
    TString cacheFile = RmsAnalyzer::CacheFilename(data->rootFile->GetName());
    TH2F* newFft[3] = {nullptr, nullptr, nullptr};
    if (!RmsAnalyzer::Load(cacheFile.Data(),
                           rmsResults[0], rmsResults[1], rmsResults[2],
                           newFft[0], newFft[1], newFft[2])) {
        printf("RMS Analysis: failed to load %s\n", cacheFile.Data());
        rmsLoaded = false;
        if (rmsStatusLabel)
            rmsStatusLabel->SetText(
                TString::Format("%s  [LOAD FAILED]", cacheFile.Data()).Data()
            );
    } else {
        // Replace stored FFT spectra (may be nullptr for pre-FFT cache files)
        for (int p = 0; p < 3; ++p) {
            delete fftSpec[p];
            fftSpec[p] = newFft[p];
            fftSelectedCh[p] = -1;
        }
        rmsLoaded = true;
        bool hasFft = (fftSpec[0] || fftSpec[1] || fftSpec[2]);
        printf("RMS Analysis: loaded %s  (U:%zu V:%zu W:%zu channels)%s\n",
            cacheFile.Data(),
            rmsResults[0].size(), rmsResults[1].size(), rmsResults[2].size(),
            hasFft ? "  [FFT OK]" : "  [no FFT]");
        if (rmsStatusLabel)
            rmsStatusLabel->SetText(
                TString::Format("%s  [LOADED%s]", cacheFile.Data(),
                    hasFft ? "+FFT" : "").Data()
            );
    }
    if (rmsWindow) rmsWindow->Layout();
}

void GuiController::ShowRmsDistribution()
{
    if (!rmsLoaded) {
        printf("RMS Analysis: no results loaded — compute or load first\n");
        return;
    }

    static const char* planeLetter[3] = {"U", "V", "W"};
    static const Color_t planeColor[3] = {kRed, kBlue, kGreen + 2};

    TString canvName = "rmsDistCanvas";
    if (!rmsDistCanvas || !gROOT->FindObject(canvName)) {
        rmsDistCanvas = new TCanvas(canvName, "RMS Noise Distribution", 1100, 900);
        rmsDistCanvas->Connect(
            "ProcessedEvent(Int_t,Int_t,Int_t,TObject*)",
            "GuiController", this,
            "ProcessRmsCanvasEvent(Int_t,Int_t,Int_t,TObject*)"
        );
    } else {
        rmsDistCanvas->RaiseWindow();
    }

    // Clear canvas and null the pad pointers (Clear() deletes child pads)
    rmsDistCanvas->cd();
    rmsDistCanvas->Clear();
    rmsTopDistPad = nullptr; rmsTopUvPad = nullptr; rmsTopWPad = nullptr;
    for (int p = 0; p < 3; ++p) { rmsMidPad[p] = nullptr; rmsBotPad[p] = nullptr; }

    // ---- 9-pad manual layout ----
    // Top row: three thirds — RMS dist | RMS-vs-length U+V | RMS-vs-length W  [y 0.67..1.00]
    // Mid: three side-by-side RMS-vs-channel      [y 0.34..0.67]
    // Bot: three side-by-side FFT spectra         [y 0.00..0.34]
    rmsDistCanvas->cd();
    rmsTopDistPad = new TPad("rms_top_dist", "", 0.000, 0.67, 0.333, 1.00);
    rmsTopDistPad->SetBottomMargin(0.12); rmsTopDistPad->SetTopMargin(0.10);
    rmsTopDistPad->Draw();
    rmsTopUvPad = new TPad("rms_top_uv", "", 0.333, 0.67, 0.667, 1.00);
    rmsTopUvPad->SetBottomMargin(0.12); rmsTopUvPad->SetTopMargin(0.10);
    rmsTopUvPad->SetLeftMargin(0.12);
    rmsTopUvPad->Draw();
    rmsTopWPad = new TPad("rms_top_w", "", 0.667, 0.67, 1.000, 1.00);
    rmsTopWPad->SetBottomMargin(0.12); rmsTopWPad->SetTopMargin(0.10);
    rmsTopWPad->SetLeftMargin(0.12);
    rmsTopWPad->Draw();

    for (int p = 0; p < 3; ++p) {
        double x1 = p / 3.0, x2 = (p + 1) / 3.0;

        rmsMidPad[p] = new TPad(TString::Format("rms_mid_%c", "uvw"[p]), "",
                                x1, 0.34, x2, 0.67);
        rmsMidPad[p]->SetBottomMargin(0.12); rmsMidPad[p]->SetTopMargin(0.10);
        if (p > 0) rmsMidPad[p]->SetLeftMargin(0.12);
        rmsMidPad[p]->Draw();

        rmsBotPad[p] = new TPad(TString::Format("rms_bot_%c", "uvw"[p]), "",
                                x1, 0.00, x2, 0.34);
        rmsBotPad[p]->SetBottomMargin(0.14); rmsBotPad[p]->SetTopMargin(0.10);
        if (p > 0) rmsBotPad[p]->SetLeftMargin(0.12);
        rmsBotPad[p]->Draw();
    }

    // ---- Top-left pad: RMS distribution histogram (U/V/W overlaid) ----
    float maxRms = 0.f;
    for (int p = 0; p < 3; ++p)
        for (const auto& r : rmsResults[p])
            if (r.rms_final > maxRms) maxRms = r.rms_final;
    if (maxRms <= 0.f) maxRms = 10.f;
    float rmsHi = maxRms * 1.2f;

    rmsTopDistPad->cd();
    TH1F* hDist[3] = {};
    for (int p = 0; p < 3; ++p) {
        TString hname = TString::Format("hRmsDist_%s", planeLetter[p]);
        TH1F* hOld = (TH1F*)gROOT->FindObject(hname);
        if (hOld) delete hOld;
        hDist[p] = new TH1F(hname, "", 100, 0, rmsHi);
        for (const auto& r : rmsResults[p])
            hDist[p]->Fill(r.rms_final);
        hDist[p]->SetLineColor(planeColor[p]);
        hDist[p]->SetLineWidth(2);
    }
    float yMax = 0.f;
    for (int p = 0; p < 3; ++p)
        if (hDist[p]->GetMaximum() > yMax) yMax = hDist[p]->GetMaximum();
    for (int p = 0; p < 3; ++p) {
        hDist[p]->GetYaxis()->SetRangeUser(0, yMax * 1.15f);
        hDist[p]->SetTitle("Per-channel noise RMS; RMS (ADC); Channels");
        hDist[p]->Draw(p == 0 ? "hist" : "hist same");
    }
    TLegend* leg = new TLegend(0.65, 0.70, 0.97, 0.92);
    for (int p = 0; p < 3; ++p)
        leg->AddEntry(hDist[p], TString::Format("%s  (n=%d)", planeLetter[p],
            (int)rmsResults[p].size()), "l");
    leg->Draw();
    rmsTopDistPad->SetGridx(); rmsTopDistPad->SetGridy();

    // ---- Top-middle pad: RMS vs wire length (U + V overlaid) ----
    // ---- Top-right  pad: RMS vs wire length (W) ----
    auto buildLenGraph = [&](int plane, int color, const char* name) -> TGraph* {
        if (auto old = (TGraph*)gROOT->FindObject(name)) delete old;
        TGraph* g = new TGraph();
        g->SetName(name);
        int k = 0;
        for (const auto& r : rmsResults[plane]) {
            auto it = data->wire_length.find(r.channel);
            if (it == data->wire_length.end()) continue;
            g->SetPoint(k++, it->second, r.rms_final);
        }
        g->SetMarkerStyle(20);
        g->SetMarkerSize(0.4);
        g->SetMarkerColor(color);
        return g;
    };

    rmsTopUvPad->cd();
    TGraph* gU = buildLenGraph(0, kRed,  "gRmsLenU");
    TGraph* gV = buildLenGraph(1, kBlue, "gRmsLenV");
    TGraph* gFirstUV = (gU->GetN() > 0) ? gU : (gV->GetN() > 0 ? gV : nullptr);
    if (gFirstUV) {
        gFirstUV->SetTitle("RMS vs wire length (U,V); Wire length (cm); RMS (ADC)");
        gFirstUV->Draw("AP");
        if (gFirstUV == gU && gV->GetN() > 0) gV->Draw("P SAME");
        TLegend* legUV = new TLegend(0.75, 0.78, 0.97, 0.92);
        legUV->AddEntry(gU, "U", "p");
        legUV->AddEntry(gV, "V", "p");
        legUV->Draw();
    } else {
        if (auto old = gROOT->FindObject("hLenUVNoData")) delete old;
        TH1F* fr = new TH1F("hLenUVNoData",
            "RMS vs wire length (U,V) — T_geo not loaded; Wire length (cm); RMS (ADC)",
            10, 0, 1);
        fr->SetMinimum(0); fr->SetMaximum(1); fr->Draw("axis");
    }
    rmsTopUvPad->SetGridx(); rmsTopUvPad->SetGridy();

    rmsTopWPad->cd();
    TGraph* gW = buildLenGraph(2, kGreen+2, "gRmsLenW");
    if (gW->GetN() > 0) {
        gW->SetTitle("RMS vs wire length (W); Wire length (cm); RMS (ADC)");
        gW->Draw("AP");
    } else {
        if (auto old = gROOT->FindObject("hLenWNoData")) delete old;
        TH1F* fr = new TH1F("hLenWNoData",
            "RMS vs wire length (W) — T_geo not loaded; Wire length (cm); RMS (ADC)",
            10, 0, 1);
        fr->SetMinimum(0); fr->SetMaximum(1); fr->Draw("axis");
    }
    rmsTopWPad->SetGridx(); rmsTopWPad->SetGridy();

    // ---- Middle pads: RMS vs channel ----
    for (int p = 0; p < 3; ++p) {
        rmsMidPad[p]->cd();
        const auto& res = rmsResults[p];
        int n = (int)res.size();
        TGraph* gr = new TGraph(n);
        for (int i = 0; i < n; ++i)
            gr->SetPoint(i, res[i].channel, res[i].rms_final);
        gr->SetTitle(TString::Format(
            "%s plane RMS vs channel; Channel; RMS (ADC)", planeLetter[p]));
        gr->SetMarkerStyle(20);
        gr->SetMarkerSize(0.5);
        gr->SetMarkerColor(planeColor[p]);
        gr->SetLineColor(planeColor[p]);
        gr->Draw("AP");
        rmsMidPad[p]->SetGridx(); rmsMidPad[p]->SetGridy();
    }

    // ---- Bottom pads: FFT spectra (initially empty frames) ----
    for (int p = 0; p < 3; ++p) {
        rmsBotPad[p]->cd();
        TString frameName = TString::Format(fftSpec[p] ? "hFftFrame_%c" : "hFftNoData_%c",
                                            "UVW"[p]);
        TH1F* hOld2 = (TH1F*)gROOT->FindObject(frameName);
        if (hOld2) delete hOld2;
        if (fftSpec[p]) {
            TH1F* hFrame = new TH1F(frameName,
                TString::Format("%s plane FFT (click channel above); freq (MHz); |F| (ADC)",
                    planeLetter[p]),
                10, 0, RmsAnalyzer::kNyquistMHz);
            hFrame->SetMinimum(0);
            hFrame->Draw("axis");
        } else {
            TH1F* hFrame = new TH1F(frameName,
                TString::Format("%s plane — no FFT in cache; freq (MHz); |F| (ADC)",
                    planeLetter[p]),
                10, 0, RmsAnalyzer::kNyquistMHz);
            hFrame->Draw("axis");
        }
        rmsBotPad[p]->SetGridx(); rmsBotPad[p]->SetGridy();
    }

    rmsDistCanvas->Update();
}

void GuiController::ProcessRmsCanvasEvent(Int_t ev, Int_t x, Int_t y, TObject* selected)
{
    (void)selected;
    if (ev != 11) return;
    if (!rmsDistCanvas) return;

    TVirtualPad* pad = rmsDistCanvas->GetClickSelectedPad();
    if (!pad) return;

    // Only act on clicks in the middle (RMS-vs-channel) pads
    const char* padName = pad->GetName();
    int plane = -1;
    if (strcmp(padName, "rms_mid_u") == 0) plane = 0;
    else if (strcmp(padName, "rms_mid_v") == 0) plane = 1;
    else if (strcmp(padName, "rms_mid_w") == 0) plane = 2;
    if (plane < 0) return;

    double xx = pad->AbsPixeltoX(x);
    int chanNo = TMath::Nint(xx);
    cout << "RMS canvas click: " << padName << "  channel=" << chanNo << endl;

    fftSelectedCh[plane] = chanNo;

    // Update the corresponding bottom FFT pad
    if (fftSpec[plane] && rmsBotPad[plane]) {
        static const char* planeLetter[3] = {"U", "V", "W"};
        static const Color_t planeColor[3] = {kRed, kBlue, kGreen + 2};

        int binX = fftSpec[plane]->GetXaxis()->FindBin((double)chanNo);
        TString sliceName = TString::Format("hFftSlice_%c", "UVW"[plane]);

        // Remove any previous slice from gROOT's object table
        TObject* hOld = gROOT->FindObject(sliceName);
        if (hOld) delete hOld;

        TH1D* hSlice = fftSpec[plane]->ProjectionY(sliceName, binX, binX);
        hSlice->SetTitle(TString::Format(
            "%s plane FFT ch %d; freq (MHz); |F| (ADC)",
            planeLetter[plane], chanNo));
        hSlice->SetLineColor(planeColor[plane]);
        hSlice->SetLineWidth(2);

        rmsBotPad[plane]->cd();
        rmsBotPad[plane]->Clear();
        hSlice->SetMinimum(0);
        hSlice->Draw("hist");
        rmsBotPad[plane]->SetGridx(); rmsBotPad[plane]->SetGridy();
        rmsBotPad[plane]->Modified();
        rmsBotPad[plane]->Update();
        rmsDistCanvas->Modified();
        rmsDistCanvas->Update();
    }

    // Also jump the main waveform display to this channel
    cw->channelEntry->SetNumber(chanNo);
    ChannelChanged();
}

void GuiController::ToggleRmsOverlay()
{
    ChannelChanged();
}

void GuiController::HandleMenu(int id)
{
    // const char *filetypes[] = {"ROOT files", "*.root", 0, 0};
    switch (id) {
        case M_FILE_EXIT:
            gApplication->Terminate(0);
            break;
    }
}

TString GuiController::OpenDialog()
{
    const char *filetypes[] = {"ROOT files", "*.root", 0, 0};
    TString currentDir(gSystem->WorkingDirectory());
    static TString dir(".");
    TGFileInfo fi;
    fi.fFileTypes = filetypes;
    fi.fIniDir    = StrDup(dir);
    new TGFileDialog(gClient->GetRoot(), mw, kFDOpen, &fi);
    dir = fi.fIniDir;
    gSystem->cd(currentDir.Data());

    if (fi.fFilename) {
        // UnZoom();
        cout << "open file: " << fi.fFilename << endl;
        return fi.fFilename;
    }
    else {
        gApplication->Terminate(0);
    }
    return "";

}
