#ifndef TimingAnalysis_h
#define TimingAnalysis_h

#include "pulse.h"
#include "TStopwatch.h"

#include "TString.h"
#include "TH1D.h"
#include "TF1.h"
#include "TH2D.h"
#include "TH2I.h"
#include "TFileCollection.h"
#include "TMath.h"
#include "TGraph.h"
#include "TCanvas.h"
#include "iostream"
#include <iomanip>
#include <algorithm>
#include <vector>
#include <cmath>

typedef std::vector<TH1D*> TH1DptrVec_t;
typedef std::vector<TH1DptrVec_t> TH1Dptr2dVec_t;

const char* joinString(const std::string &a, const std::string &b) {
  std::string tmp(a);
  tmp.append("_");
  tmp.append(b);
  return tmp.c_str();
};

struct DirStruct {
  TDirectory* dir_max;
  TDirectory* dir_SNR;
  TDirectory* dir_baseline;
  TDirectory* dir_pedestal;
  TDirectory* dir_risetime;
  TDirectory* dir_rate;
  TDirectory* dir_nOfPeaks;

  DirStruct(TFile *root_file) {
    dir_max = root_file->mkdir("max");
    dir_SNR = root_file->mkdir("SNR");
    dir_baseline = root_file->mkdir("baseline");
    dir_pedestal = root_file->mkdir("pedestal");
    dir_risetime = root_file->mkdir("risetime");
    dir_rate = root_file->mkdir("rate");
    dir_nOfPeaks = root_file->mkdir("nOfPeaks");
  }
};

struct HistoStruct {
  double startTime_prev;

  TH1D* h_max;
  TH1D* h_SNR;
  TH1D* h_baseline;
  TH1D* h_pedestal;
  TH1D* h_risetime;
  TH1D* h_rate;
  TH1D* h_nOfPeaks;

  HistoStruct(const std::string name, const DirStruct& dirStruct): startTime_prev(.0) {
    dirStruct.dir_max->cd();
    h_max = new TH1D(joinString("maximum_Ch",name),joinString("maximum_Ch",name),500,0,2);
    dirStruct.dir_SNR->cd();
    h_SNR = new TH1D(joinString("SNRatio_Ch",name), joinString("SNRatio_Ch",name),250,0,250);
    dirStruct.dir_baseline->cd();
    h_baseline = new TH1D(joinString("baseline_Ch",name), joinString("baseline_Ch",name),10000,-0.1,0.1);
    dirStruct.dir_pedestal->cd();
    h_pedestal = new TH1D(joinString("pedestal_Ch",name), joinString("pedestal_Ch",name),10000,-0.1,0.1);
    dirStruct.dir_risetime->cd();
    h_risetime = new TH1D(joinString("risetime_Ch",name), joinString("risetime_Ch",name),100,0,10e-9);
    dirStruct.dir_rate->cd();
    h_rate = new TH1D(joinString("timeseparation_Ch",name), joinString("timeseparation_Ch",name),1e3,0,1e2);
    dirStruct.dir_nOfPeaks->cd();
    h_nOfPeaks = new TH1D(joinString("nOfPeaks_Ch",name),joinString("nOfPeaks_Ch",name),10,0,10);
  };
};

typedef std::map<int, HistoStruct> HistoMap;

//Landau*Gauss to fit max values
Double_t langaufun(Double_t *x, Double_t *par) {
  //Fit parameters:
  //par[0]=Width (scale) parameter of Landau density
  //par[1]=Most Probable (MP, location) parameter of Landau density
  //par[2]=Total area (integral -inf to inf, normalization constant)
  //par[3]=Width (sigma) of convoluted Gaussian function
  //
  //In the Landau distribution (represented by the CERNLIB approximation),
  //the maximum is located at x=-0.22278298 with the location parameter=0.
  //This shift is corrected within this function, so that the actual
  //maximum is identical to the MP parameter.

  // Numeric constants
  Double_t invsq2pi = 0.3989422804014;   // (2 pi)^(-1/2)
  Double_t mpshift  = -0.22278298;       // Landau maximum location

  // Control constants
  Double_t np = 100.0;      // number of convolution steps
  Double_t sc =   5.0;      // convolution extends to +-sc Gaussian sigmas

  // Variables
  Double_t xx;
  Double_t mpc;
  Double_t fland;
  Double_t sum = 0.0;
  Double_t xlow,xupp;
  Double_t step;
  Double_t i;


  // MP shift correction
  mpc = par[1] - mpshift * par[0];

  // Range of convolution integral
  xlow = x[0] - sc * par[3];
  xupp = x[0] + sc * par[3];

  step = (xupp-xlow) / np;

  // Convolution integral of Landau and Gaussian by sum
  for(i=1.0; i<=np/2; i++) {
    xx = xlow + (i-.5) * step;
    fland = TMath::Landau(xx,mpc,par[0]) / par[0];
    sum += fland * TMath::Gaus(x[0],xx,par[3]);

    xx = xupp - (i-.5) * step;
    fland = TMath::Landau(xx,mpc,par[0]) / par[0];
    sum += fland * TMath::Gaus(x[0],xx,par[3]);
  }

  return (par[2] * step * sum * invsq2pi / par[3]);
}

class TimingAnalysis : public pulse
{

  public :

    TimingAnalysis(TTree * tree=0);

    void initialize();

    template <class T>
    double executeTimeDifferenceWithMCP(TFile * out_f, double (*computeExactTime) (std::vector<double>&, std::vector<double>&, T&), T& parameters, int ChannelMeasureA=0, int ChannelMeasureB=1) {
      std::cout << "\nTime measurement started for channels " << ChannelMeasureA << " and " << ChannelMeasureB << std::endl;

      int eventNumber=0;
      int eventCounter=0;
      int prevHitNumber=10;

      double T_Sample_A = .0;
      double T_Sample_B = .0;
      double T_threshold_A = .0;
      double T_threshold_B = .0;
      double ch1_maximum = .0;
      double ch2_maximum = .0;
      double ch1_tot = .0;
      double ch2_tot = .0;
      double ch1_baselineRms = .0;
      double ch2_baselineRms = .0;


      // TFile outTreeFile("/eos/user/n/nminafra/www/ML/tree_v1.root","recreate");
      // outTreeFile.cd();
      // TTree outTree("dataset","dataset_Nov2018");
      Int_t time_reference=0;
      Int_t time_samples[500];
      Int_t voltage_samples[500];
      // outTree.Branch("time_reference", &time_reference, "time_reference/I");
      // outTree.Branch("time_samples", time_samples, "time_samples[500]/I");
      // outTree.Branch("voltage_samples", voltage_samples, "voltage_samples[500]/I");

      out_f->cd();


      // Histo declaration, create new for a new file
      TH1D h_deltat_SimpleThreshold("h_deltat_SimpleThreshold","Time Difference wrt MCP using simple threshold; t(s)",2000,-10e-9,10e-9);
      TH1D h_deltat_Smart("h_deltat_Smart","Time Difference wrt MCP; t(s)",5000,-10e-9,10e-9);
      TH1D h_TimeFromTrigger_Det0("h_TimeFromTrigger_Det0","Time Difference bw MCP and trigger; t(s)",1000,-150e-9,-50e-9);
      TH1D h_TimeFromTrigger_Det1("h_TimeFromTrigger_Det1","Time Difference bw DUT and trigger; t(s)",1000,-150e-9,-50e-9);

      TH1D h_max_Det0("h_max_Det0","Amplitude MCP; V",200,0,parameters.rangeMax_ch0);
      TH1D h_max_Det1("h_max_Det1","",100,0,parameters.rangeMax_ch1);
      TH1D h_max_selected_Det0("h_max_selected_Det0","Amplitude of selected samples (MCP); V",200,0,parameters.rangeMax_ch0);
      TH1D h_max_selected_Det1("h_max_selected_Det1","Amplitude of selected samples (DUT); V",100,0,parameters.rangeMax_ch1);
      TH1D integral_Det1("integral_Det1","Integral (DUT); ??",1000,-1,1);
      TH1D h_baseline_Det0("h_baseline_Det0","Baseline of MCP; V",200,-0.01,0.01);
      TH1D h_baseline_Det1("h_baseline_Det1","Baseline of DUT; V",200,-0.01,0.01);
      TH1D h_pedestal_Det0("h_pedestal_Det0","Pedestal (RMS before pulse) of MCP; V",1000,-0.1,0.1);
      TH1D h_pedestal_Det1("h_pedestal_Det1","Pedestal (RMS before pulse) of DUT; V",1000,-0.1,0.1);
      TH1D h_SNR_Det0("h_SNR_Det0","SNR of MCP; SNR",150,0,150);
      TH1D h_SNR_Det1("h_SNR_Det1","SNR of DUT; SNR",150,0,150);

      TH1D h_risetime_Det0("h_risetime_Det0","Risetime of MCP; rt (s)",200,0,4e-9);
      TH1D h_risetime_Det1("h_risetime_Det1","Risetime of DUT; rt (s)",200,0,4e-9);

      TH1D h_nOfPeaks_Det0("h_nOfPeaks_Det0","Number of peaks found per trigger (MCP); number of peaks",10,0,10);
      TH1D h_nOfPeaks_Det1("h_nOfPeaks_Det1","Number of peaks found per trigger (DUT); number of peaks",10,0,10);
      TH1D h_coincidences("h_coincidences","Number of channels with signal; #",4,0,4);

      int numOfBinX = 7;
      int numOfBinY = (parameters.rangeMax_ch1-parameters.rangeMin_ch1)/0.01;
      TH2D bidimHistogram("bidimHistogram","Time difference bw MCP and DUT; ampl MCP (V); ampl DUT (V)",numOfBinX,parameters.rangeMin_ch0,parameters.rangeMax_ch0,numOfBinY,parameters.rangeMin_ch1,parameters.rangeMax_ch1);
      TH2D bidimHistogramRMS("bidimHistogramRMS","RMS of time difference bw MCP and DUT (ps); ampl MCP (V); ampl DUT (V)",numOfBinX,parameters.rangeMin_ch0,parameters.rangeMax_ch0,numOfBinY,parameters.rangeMin_ch1,parameters.rangeMax_ch1);
      TH2D bidimHistogramSigma("bidimHistogramSigma","Sigma of time difference bw MCP and DUT (ps); ampl MCP (V); ampl DUT (V)",numOfBinX,parameters.rangeMin_ch0,parameters.rangeMax_ch0,numOfBinY,parameters.rangeMin_ch1,parameters.rangeMax_ch1);
      TH2I bidimHistogramN("bidimHistogramN","Number of entries; ampl MCP (V); ampl DUT (V)",numOfBinX,parameters.rangeMin_ch0,parameters.rangeMax_ch0,numOfBinY,parameters.rangeMin_ch1,parameters.rangeMax_ch1);
      TH1DptrVec_t bidimHistogramVec(bidimHistogram.GetNbinsX()*bidimHistogram.GetNbinsY());
      std::cout<<"bins: "<<bidimHistogram.GetNbinsX()*bidimHistogram.GetNbinsY()<<std::endl;
      for(int itx=0; itx<bidimHistogramVec.size(); ++itx) {
      	TString histName("tmpHist_");
      	histName+=itx;
      	bidimHistogramVec.at(itx) = new TH1D(histName,histName,2000,-10e-9,10e-9);
      }

      int corrections_point=0;
      TGraph g_correctionsX;
      g_correctionsX.SetName("CorrectionsX_TOT");
      TGraph g_correctedX;
      g_correctedX.SetName("CorrectedX_TOT");

      int corrections_point_Y=0;
      TGraph g_correctionsY;
      g_correctionsY.SetName("CorrectionsY_TOT");
      TGraph g_correctedY;
      g_correctedY.SetName("CorrectedY_TOT");

      int ChannelsCorrelations_i=0;
      TGraph g_channels_corrlations;
      g_channels_corrlations.SetName("Correlations");

      TGraph g_deltaTwithTime;
      g_deltaTwithTime.SetName("DeltaT");
      TGraph g_rmswithTime;
      g_rmswithTime.SetName("RMS");
      TGraph g_noiseDet1WithTime;
      g_noiseDet1WithTime.SetName("noise_ch1");
      TGraph g_maximum0;
      g_maximum0.SetName("MaximumMCPVsTime");
      int g_maximum0_point=0;
      TGraph g_maximum1;
      g_maximum1.SetName("MaximumDUTVsTime");
      int g_maximum1_point=0;
      TGraph g_tot_ch0;
      int point_tot_ch0=0;
      g_tot_ch0.SetName("ToT_ch0");
      TGraph g_tot_ch1;
      int point_tot_ch1=0;
      g_tot_ch1.SetName("ToT_ch1");
      int point=0;
      int pointCorr=0;
      int pointCorr2=0;
      parameters.found = 0;
      int plotted=0;

      // Loop on the files
      if (fChain == 0) return .0;

      // Long64_t nentries = fChain->GetEntries();
      Long64_t nentries = fChain->GetEntries();
      std::cout<<"RESULTS: Entries found: "<< fChain->GetEntries() << std::endl;
      std::cout<<"Entries found: "<< fChain->GetEntriesFast() << std::endl;

      // Check whether events start from 0 or 1
      fChain->GetEntry(0);
      // eventNumber=event-1;

      Long64_t jentry=0;
      while (jentry<nentries) {
      	if (LoadTree(jentry) < 0) break;
      	fChain->GetEntry(jentry++);

      	eventCounter=i_evt;
      	if (eventCounter%1000==0  && eventCounter>0) {
      	  TF1 gausDt_tmp("gausDt_tmp","gaus",h_deltat_Smart.GetMean() - 2*h_deltat_Smart.GetRMS(),h_deltat_Smart.GetMean() + 2*h_deltat_Smart.GetRMS());
      	  h_deltat_Smart.Fit(&gausDt_tmp,"RFQ");
      	  std::cout<<"Processing event number "<<eventCounter<<"\t"<< parameters.found <<" found \tRMS: "<<h_deltat_Smart.GetRMS()<<"\tsigma:\t" << gausDt_tmp.GetParameter(2)<<std::endl;
      	}
      	// if (eventCounter>20) break;

      	TGraph* interp_th = new TGraph();
      	int point_gr=0;

      	T_Sample_A=-1;
      	T_Sample_B=-1;
      	T_threshold_A=-1;
      	T_threshold_B=-1;
      	ch1_maximum=.0;
      	ch2_maximum=.0;

      	ch1_tot=.0;
      	ch2_tot=.0;
      	ch1_baselineRms=.0;
      	ch2_baselineRms=.0;
      	++point;
      	int coincidences=0;
      	int ChannelMeasureA_position=0;
      	int ChannelMeasureB_position=0;
      	int chfound=0;


      	std::vector<double> TimeSamplesA;
              std::vector<double> TimeSamplesB;
      	std::vector<double> DataSamplesA;
      	std::vector<double> DataSamplesB;
      	for (int i=0; i< 1600; ++i) {
          TimeSamplesA.push_back( (double) (time[0][i]-time[0][0]));
          TimeSamplesB.push_back( (double) (time[0][i]-time[0][0]));
      	  DataSamplesA.push_back( 1e-3 * (double) channel[ChannelMeasureA][i] );
      	  DataSamplesB.push_back( 1e-3 * (double) channel[ChannelMeasureB][i] );
      	}
        for (int i=0; i< 500; ++i) {
          time_samples[i] = (Int_t) (1e12 * (time[0][i+200]-time[0][0]));
          voltage_samples[i] = (Int_t) (1e6 * channel[ChannelMeasureB][i+200]);
      	}


    	  parameters.detectorNumber=0;
    	  T_Sample_A = computeExactTime(TimeSamplesA, DataSamplesA, parameters);
    	  ch1_maximum = parameters.maximum;
    	  h_max_Det0.Fill(parameters.maximum);
    	  ch1_tot = parameters.timeOverThreshold;
    	  ch1_baselineRms = parameters.baseline_rms;
    	  h_nOfPeaks_Det0.Fill(parameters.numberOfpeaks);
    	  h_baseline_Det0.Fill(parameters.baseline_rms);
    	  T_threshold_A = parameters.thresholdTime;

    	  //Pedestal
    	  if (parameters.numberOfpeaks<=1) {
    	    for (int shift=1; shift<TimeSamplesA.size(); ++shift) {
    	      if (TimeSamplesA.at(shift)-TimeSamplesA.at(0) > (parameters.thresholdTime-TimeSamplesA.at(0))/3) break;
    	      h_pedestal_Det0.Fill(DataSamplesA.at(shift));
    	    }
    	  }

    	  if (T_Sample_A!=-1) {
    	    h_max_selected_Det0.Fill(parameters.maximum);
          g_maximum0.SetPoint(point_tot_ch0,((double) eventCounter)/10000,parameters.maximum);
    	    h_SNR_Det0.Fill(parameters.maximum / ch1_baselineRms);
    	    h_risetime_Det0.Fill(parameters.risetime);
      	    // if (gauspeak[0] != 0 ) h_TimeFromReference_Det0.Fill(T_threshold_A-1e-9* (double)gauspeak[0]);
      	    h_TimeFromTrigger_Det0.Fill(T_Sample_A);
      	    if (TMath::Abs(parameters.timeOverThreshold)<1) g_tot_ch0.SetPoint(point_tot_ch0++,parameters.timeOverThreshold,parameters.maximum);
      	    ++coincidences;
      	  }


      	  parameters.detectorNumber=1;
      	  T_Sample_B = computeExactTime(TimeSamplesB, DataSamplesB, parameters);
      	  ch2_maximum = parameters.maximum;
      	  ch2_tot = parameters.timeOverThreshold;
      	  ch2_baselineRms = parameters.baseline_rms;
          if ( ch2_baselineRms>0.003 && plotted<40 && T_Sample_B!=-1) {
            std::string namegr("ev");
            namegr+=std::to_string(i_evt);
            interp_th->SetName(namegr.c_str());
            for (int shift=0; shift<TimeSamplesA.size(); ++shift) {
                interp_th->SetPoint(point_gr++,1e9*(TimeSamplesA.at(shift)-TimeSamplesA.at(0)),-1+DataSamplesA.at(shift));
            }
            for (int shift=0; shift<TimeSamplesB.size(); ++shift) {
              interp_th->SetPoint(point_gr++,1e9*(TimeSamplesB.at(shift)-TimeSamplesB.at(0)),DataSamplesB.at(shift));
            }
            interp_th->Write();
            ++plotted;
          }
          if (ch2_baselineRms>0.003) T_Sample_B=-1;

      	  h_nOfPeaks_Det1.Fill(parameters.numberOfpeaks);
      	  h_baseline_Det1.Fill(parameters.baseline_rms);
      	  T_threshold_B = parameters.thresholdTime;
      	  h_max_Det1.Fill(parameters.maximum);

      	  //Pedestal
      	  if (parameters.numberOfpeaks<=1) {
      	    for (int shift=1; shift<TimeSamplesB.size(); ++shift) {
      	      if (TimeSamplesB.at(shift)-TimeSamplesB.at(0) > (parameters.thresholdTime-TimeSamplesB.at(0))/3) break;
      	      h_pedestal_Det1.Fill(DataSamplesB.at(shift));
      	    }
      	  }

      	  if (T_Sample_B!=-1) {
      	    if (TMath::Abs(parameters.timeOverThreshold) < 1) g_tot_ch1.SetPoint(point_tot_ch1++,parameters.timeOverThreshold,parameters.maximum);
      	    h_max_selected_Det1.Fill(parameters.maximum);
            g_maximum1.SetPoint(point_tot_ch1,((double) eventCounter)/10000,parameters.maximum);
      	    h_SNR_Det1.Fill(parameters.maximum / ch2_baselineRms);
      	    h_risetime_Det1.Fill(parameters.risetime);
      	    // if (gauspeak[0] != 0 ) h_TimeFromReference_Det1.Fill(T_threshold_B-1e-9* (double) gauspeak[0]);
      	    h_TimeFromTrigger_Det1.Fill(T_threshold_B);
      	    ++coincidences;

            TGraph charge_tg;
            // for (int shift=0; shift<TimeSamplesB.size(); ++shift) {
            for (int shift=500; shift<550; ++shift) {
              charge_tg.SetPoint(shift-500,1e9*(TimeSamplesB.at(shift)-TimeSamplesB.at(0)),DataSamplesB.at(shift));
            }
            integral_Det1.Fill(charge_tg.Integral());


        	  // if (plotted<10 ) {
        	  //   for (int shift=0; shift<TimeSamplesB.size(); ++shift) {
        	  //     interp_th->SetPoint(point_gr++,1e9*(TimeSamplesB.at(shift)-TimeSamplesB.at(0)),-1+DataSamplesB.at(shift));
        	  //   }
        	  // }
          }


        	//Select couple of hits
        	h_coincidences.Fill(coincidences);
        	g_rmswithTime.SetPoint(pointCorr2, eventCounter, h_deltat_Smart.GetRMS());
        	g_noiseDet1WithTime.SetPoint(pointCorr2++, eventCounter, ch2_baselineRms);

        	if (coincidences==2 && TMath::Abs(T_Sample_B-T_Sample_A)<10e-9 ) {
        	  if (T_Sample_A!=-1 && T_Sample_B!=-1) {
              time_reference = (Int_t) (1e12 * T_Sample_A);
              // outTreeFile.cd();
              // outTree.Fill();
              // out_f->cd();
        	    g_deltaTwithTime.SetPoint(pointCorr++, ((double) eventCounter)/10000, (T_Sample_B-T_Sample_A)*1e9);
        	    h_deltat_Smart.Fill(T_Sample_B-T_Sample_A);
        	    g_channels_corrlations.SetPoint(ChannelsCorrelations_i++,T_Sample_A,T_Sample_B);

        	    if(ch2_tot>0.3e-9 && ch2_tot<20e-9 && ch1_tot>0.3e-9 && ch1_tot<20e-9 && TMath::Abs(T_threshold_A-T_Sample_B)<50e-9) {
        	      g_correctionsX.SetPoint(corrections_point,ch1_tot*1e9,(T_threshold_A-T_Sample_B)*1e9);
        	      g_correctedX.SetPoint(corrections_point++,ch1_tot*1e9,(T_Sample_A-T_Sample_B)*1e9);
        	    }

        	    if(ch1_tot>0.3e-9 && ch1_tot<20e-9 && ch2_tot>0.3e-9 && ch2_tot<20e-9 && TMath::Abs(T_threshold_B-T_Sample_A)<50e-9) {
        	      g_correctionsY.SetPoint(corrections_point_Y,ch2_tot*1e9,(T_threshold_B-T_Sample_A)*1e9);
        	      g_correctedY.SetPoint(corrections_point_Y++,ch2_tot*1e9,(T_Sample_B-T_Sample_A)*1e9);
        	    }

        	    try {
          	    bidimHistogramVec.at(bidimHistogram.FindFixBin(ch1_maximum,ch2_maximum))->Fill(T_Sample_B-T_Sample_A);
        	    }
        	    catch (...) {}
        	  }
        	  if ( T_threshold_B != -1 && T_threshold_A != -1 ) {
              h_deltat_SimpleThreshold.Fill(T_threshold_B-T_threshold_A);
          	  parameters.found++;
            }
        	}
        } //Loop end

        std::cout << "Completed!" << std::endl;
        std::cout << "RMS Det1-Det0 using simple threshold:\t" << h_deltat_SimpleThreshold.GetRMS() << std::endl;
        std::cout << "RMS Det1-Det0 using smart algorithm:\t" << h_deltat_Smart.GetRMS() << std::endl;
        float centerOfFit = h_deltat_Smart.GetBinCenter(h_deltat_Smart.GetMaximumBin());
        TF1 gausDt("gausDt","gaus",centerOfFit - 0.5e-9,centerOfFit + 0.5e-9);
        h_deltat_Smart.Fit(&gausDt,"RFQ");
        TF1 gausDt2("gausDt2","gaus",gausDt.GetParameter(1) - 2*gausDt.GetParameter(2), gausDt.GetParameter(1) + 2*gausDt.GetParameter(2));
        h_deltat_Smart.Fit(&gausDt2,"RFQ");
        parameters.errorOnSigma = 1e12*gausDt2.GetParError(2);

        std::cout << "Integral: " << integral_Det1.GetRMS() << " ??" << std::endl;
        std::cout << "NOISE of the baseline: " << 1e3 * h_baseline_Det1.GetMean() << " +- " << 1e3 * h_baseline_Det1.GetRMS() << " mV" << std::endl;
        std::cout << "############ RESULTS: Time difference " << 1e12*gausDt2.GetParameter(2) << " +- " << 1e12*gausDt2.GetParError(2) << " ps, using " << parameters.found << " coincidences" << std::endl;

        for(int itx=0; itx<bidimHistogramVec.size(); ++itx) {
        	if (bidimHistogramVec.at(itx)->GetEntries() > 10) {
            TF1 gausDtemp("gausDtemp","gaus", bidimHistogramVec.at(itx)->GetMean() - 2*bidimHistogramVec.at(itx)->GetRMS(), bidimHistogramVec.at(itx)->GetMean() + 2*bidimHistogramVec.at(itx)->GetRMS());
            bidimHistogramVec.at(itx)->Fit(&gausDtemp,"RFQ");
        	  bidimHistogram.SetBinContent(itx,(bidimHistogramVec.at(itx)->GetMean()-gausDt.GetParameter(1))*1e12);
        	  if (bidimHistogramVec.at(itx)->GetEntries() > 25) bidimHistogramSigma.SetBinContent(itx,gausDtemp.GetParameter(2)*1e12);
            bidimHistogramRMS.SetBinContent(itx,bidimHistogramVec.at(itx)->GetRMS()*1e12);
        	}
        	bidimHistogramN.SetBinContent(itx,bidimHistogramVec.at(itx)->GetEntries());
        }

        g_correctionsX.Write();
        g_correctedX.Write();
        g_correctionsY.Write();
        g_correctedY.Write();
        g_channels_corrlations.Write();

        for(int itx=0; itx<bidimHistogramVec.size(); ++itx) {
        	bidimHistogramVec.at(itx)->Delete();
        }

        parameters.sigmaOfCh1Amplitude = h_max_Det1.GetRMS();

        // Normalize all histograms
        // h_deltat_SimpleThreshold.Scale(1./h_deltat_SimpleThreshold.GetMaximum(),"nosw2");
        // h_deltat_Smart.Scale(1./h_deltat_Smart.GetMaximum(),"nosw2");
        // h_TimeFromReference_Det0.Scale(1./h_TimeFromReference_Det0.GetMaximum(),"nosw2");
        // h_TimeFromReference_Det1.Scale(1./h_TimeFromReference_Det1.GetMaximum(),"nosw2");
        //
        // h_pedestal_Det0.Scale(1./h_pedestal_Det0.GetMaximum(),"nosw2");
        // h_pedestal_Det1.Scale(1./h_pedestal_Det1.GetMaximum(),"nosw2");
        // h_max_Det0.Scale(1./h_max_Det0.GetMaximum(),"nosw2");
        // h_max_Det1.Scale(1./h_max_Det1.GetMaximum(),"nosw2");
        // h_max_selected_Det0.Scale(1./h_max_selected_Det0.GetMaximum(),"nosw2");
        // h_max_selected_Det1.Scale(1./h_max_selected_Det1.GetMaximum(),"nosw2");

  //       TF1* langau = new TF1("langau",langaufun,parameters.rangeMin_ch0,parameters.rangeMax_ch0,4);
  //       langau->SetNpx(10000);
  //       langau->SetParNames("Width","MP","Area","GSigma");
  //       langau->SetParameters(0.05*parameters.rangeMax_ch0,0.25*parameters.rangeMax_ch0,50,h_pedestal_Det0.GetRMS());
  //       langau->SetRange(parameters.rangeMin_ch0,parameters.rangeMax_ch0);
  // //       langau->FixParameter(3,h_pedestal_Det0.GetRMS());
  //       h_max_selected_Det0.Fit(langau,"RFBQ");
        TF1* langau1 = new TF1("langau1",langaufun,parameters.rangeMin_ch1,parameters.rangeMax_ch1,4);
        langau1->SetNpx(10000);
        langau1->SetParNames("Width","MP","Area","GSigma");
        // langau1->SetParameters(0.05*parameters.rangeMax_ch0,0.25*parameters.rangeMax_ch0,50,h_pedestal_Det1.GetRMS());
        langau1->SetParameters(0.015,0.015,0.5,h_pedestal_Det1.GetRMS());
	langau1->SetParLimits(0,0,0.01);
	langau1->SetParLimits(1,0,0.2);
	langau1->SetParLimits(2,0,10);
	langau1->SetParLimits(3,0,0.05);
        langau1->SetRange(parameters.rangeMin_ch1,parameters.rangeMax_ch1);
        // langau1->FixParameter(3,h_pedestal_Det0.GetRMS());
        h_max_selected_Det1.Fit(langau1,"RFB");

        // h_baseline_Det0.Scale(1./h_baseline_Det0.GetMaximum(),"nosw2");
        // h_baseline_Det1.Scale(1./h_baseline_Det1.GetMaximum(),"nosw2");
        // h_SNR_Det0.Scale(1./h_SNR_Det0.GetMaximum(),"nosw2");
        // h_SNR_Det1.Scale(1./h_SNR_Det1.GetMaximum(),"nosw2");
        //
        // h_risetime_Det0.Scale(1./h_risetime_Det0.GetMaximum(),"nosw2");
        // h_risetime_Det1.Scale(1./h_risetime_Det1.GetMaximum(),"nosw2");

        g_deltaTwithTime.Write();
        g_rmswithTime.Write();
        g_noiseDet1WithTime.Write();
        g_maximum0.Write();
        g_maximum1.Write();
        g_tot_ch0.Write();
        g_tot_ch1.Write();
        out_f->Write();

        // outTreeFile.Write();
        m_timer.Stop();
        std::cout << "********************************" << std::endl;
        std::cout << "INFORMATION :: Performance : " << std::endl;
        std::cout << "RealTime= " << m_timer.RealTime() << "seconds, CpuTime= " << m_timer.CpuTime() << "seconds" << std::endl;
        m_timer.Reset();
        m_timer.Start();
        return gausDt2.GetParameter(2);
    }



    template <class T>
    double executeAll(TFile * out_f, double (*computeExactTime) (std::vector<double>&, std::vector<double>&, T&), T& parameters) {
      std::cout << "\nCharacterization of all channels... " << std::endl;

      double ch1_maximum = .0;
      double ch1_baselineRms = .0;

      // Histo declaration, create new for a new file

      DirStruct dirStruct(out_f);
      HistoMap histograms;

      int point=0;
      parameters.found = 0;

      // Loop on the files
      if (fChain == 0) return .0;

      Long64_t nentries = fChain->GetEntries();
      int internalcount=0;
      for (Long64_t jentry=0; jentry<nentries;++jentry) {
	        if (LoadTree(jentry) < 0) break;

        	TGraph* interp_th = new TGraph();
        	int point_gr=0;

        	ch1_maximum=.0;
        	ch1_baselineRms=.0;
        	++point;

        	fChain->GetEntry(jentry);

        	if (jentry%1000==0 && jentry>0) std::cout<<"Processing entry number "<<jentry<<"\t\tfounded:\t"<< parameters.found<<std::endl;

        	for (int Channel=0; Channel<4; ++Channel) {
        	  std::vector<double> TimeSamples;
        	  std::vector<double> DataSamples;
        	  for (int i=0; i< 1000; ++i) {
        	    TimeSamples.push_back(1e-9 * time[0][i] );
        	    DataSamples.push_back( 1./ 1000 * channel[Channel][i] );
        	  }
        	  if (histograms.find(Channel)==histograms.end())
        	    histograms.insert( HistoMap::value_type( Channel, HistoStruct(std::to_string(Channel),dirStruct) ) );

        	  histograms[Channel].h_rate->Fill((TimeSamples.at(0) - histograms[Channel].startTime_prev));
        	  histograms[Channel].startTime_prev = TimeSamples.at(0);

            if (Channel==0)
          	  parameters.detectorNumber=0;
            else
              parameters.detectorNumber=1;

        	  computeExactTime(TimeSamples, DataSamples, parameters);
        	  histograms[Channel].h_max->Fill(parameters.maximum);
        	  ch1_maximum = parameters.maximum;
        	  ch1_baselineRms = parameters.baseline_rms;
        	  histograms[Channel].h_nOfPeaks->Fill(parameters.numberOfpeaks);
        	  histograms[Channel].h_baseline->Fill(parameters.baseline_rms);
        // 	    std::cout<<parameters.numberOfpeaks<<std::endl;

      	  //Pedestal
      	  if (parameters.numberOfpeaks<=1) {
      	    for (int shift=1; shift<TimeSamples.size(); ++shift) {
      	      if (TimeSamples.at(shift)-TimeSamples.at(0) > (parameters.thresholdTime-TimeSamples.at(0))/4)
      		break;
      	      histograms[Channel].h_pedestal->Fill(-DataSamples.at(shift));
      	    }
      	  }

      	  if (parameters.numberOfpeaks==1) {
      	    histograms[Channel].h_SNR->Fill(parameters.maximum / ch1_baselineRms);
      	    histograms[Channel].h_risetime->Fill(parameters.risetime);

      	    if (parameters.found<20) {
      	      for (int shift=0; shift<TimeSamples.size(); ++shift) {
            		interp_th->SetPoint(point_gr++,1e9*TimeSamples.at(shift),-DataSamples.at(shift));
      	      }
      	      interp_th->Write();
      	    }
      	    ++(parameters.found);
      	  }
      	}
      } //Loop end

      // Normalize all histograms
      for (HistoMap::iterator it=histograms.begin(); it != histograms.end(); ++it) {
      	it->second.h_pedestal->Scale(1./it->second.h_pedestal->GetMaximum());
      	it->second.h_SNR->Scale(1./it->second.h_SNR->GetMaximum());
      	it->second.h_baseline->Scale(1./it->second.h_baseline->GetMaximum());
      	it->second.h_max->Scale(1./it->second.h_max->GetMaximum());
      	it->second.h_risetime->Scale(1./it->second.h_risetime->GetMaximum());
      }

      out_f->cd();
      TH1D* allRates = new TH1D("allRates","allRates",32,0,32);
      TH1D* meanNOfPeaks = new TH1D("meanNOfPeaks","meanNOfPeaks",32,0,32);
      TH1D* noiseRMS = new TH1D("noiseRMS","noiseRMS",32,0,32);
      TH1D* allRiseTime = new TH1D("allRiseTime","allRiseTime",32,0,32);
      TH1D* allSNR = new TH1D("allRiseTime","allRiseTime",32,0,32);
      TCanvas* c=new TCanvas();

      std::cout << "\n\n//************************************************************************//" << std::endl;
      std::cout<< "Channel\t\trate (Hz)\t\tavg nOfPeaks\t\tnoise RMS (V)\n"<< std::endl;

      TF1* rateFunc = new TF1("rateFunc","[0] * exp( - [1] * x)",1e-6,100);
      for (HistoMap::iterator it=histograms.begin(); it != histograms.end(); ++it) {
      	it->second.h_rate->Fit("rateFunc","RQ","",1e-6,10);
      	if (rateFunc->GetParameter(1) == rateFunc->GetParameter(1))
      	  allRates->Fill(it->first,rateFunc->GetParameter(1));

      	if (it->second.h_nOfPeaks->GetMean() == it->second.h_nOfPeaks->GetMean())
      	  meanNOfPeaks->Fill(it->first, it->second.h_nOfPeaks->GetMean());

      	if (it->second.h_pedestal->GetRMS() == it->second.h_pedestal->GetRMS())
      	  noiseRMS->Fill(it->first, it->second.h_pedestal->GetRMS());

      	if (it->second.h_risetime->GetMean() == it->second.h_risetime->GetMean())
      	  allRiseTime->Fill(it->first, it->second.h_risetime->GetMean());

      	if (it->second.h_SNR->GetMean() == it->second.h_SNR->GetMean())
      	  allSNR->Fill(it->first, it->second.h_SNR->GetMean());

      	std::cout<<it->first<<"\t\t"<<std::setprecision(4)<< rateFunc->GetParameter(1) << "\t\t\t" << it->second.h_nOfPeaks->GetMean() << "\t\t\t" << it->second.h_pedestal->GetRMS()<< std::endl;
      }

      std::cout << "\n//************************************************************************//" << std::endl;
      std::cout << "\n\nCompleted!\t\tfounded:\t"<< parameters.found<<std::endl;

      out_f->Write();
      m_timer.Stop();
      std::cout << "********************************" << std::endl;
      std::cout << "INFORMATION :: Performance : " << std::endl;
      std::cout << "RealTime= " << m_timer.RealTime() << "seconds, CpuTime= " << m_timer.CpuTime() << "seconds" << std::endl;
      m_timer.Reset();
      m_timer.Start();
      return 0;
    }


    //empty
    void finalize();


  private :

    //clock
    TStopwatch m_timer;

};

#endif
