// -*- C++ -*-
//
// Package:    PulseStudies/PulseTree
// Class:      PulseTree
// 
/**\class PulseTree PulseTree.cc PulseStudies/PulseTree/plugins/PulseTree.cc

 Description: [one line class summary]

 Implementation:
     [Notes on implementation]
*/


// system include files
#include <memory>
#include <iostream>
#include <algorithm>
#include <vector>

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "DataFormats/EcalDigi/interface/EcalDigiCollections.h"

#include "TTree.h"

//
// class declaration
//

// If the analyzer does not use TFileService, please remove
// the template argument to the base class so the class inherits
// from  edm::one::EDAnalyzer<> and also remove the line from
// constructor "usesResource("TFileService");"
// This will improve performance in multithreaded jobs.

class PulseTree : public edm::one::EDAnalyzer<edm::one::SharedResources>  {
   public:
      explicit PulseTree(const edm::ParameterSet&);
      ~PulseTree();

      static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);


   private:
      virtual void beginJob() override;
      virtual void analyze(const edm::Event&, const edm::EventSetup&) override;
      virtual void endJob() override;

  void FillDigi(EcalDataFrame digi);
  bool FilterBx(unsigned bx);
  void WriteAverageOutput();

      // ----------member data ---------------------------

  edm::EDGetTokenT<EBDigiCollection> tok_ebdigis_;
  edm::EDGetTokenT<EEDigiCollection> tok_eedigis_;
  bool do_EB;
  bool do_EE;
  bool do_average;
  bool split_lumis;
  bool do_filter_bx;
  UInt_t n_pedestal_samples;

  TTree *outTree;
  UInt_t t_run;
  UShort_t t_lumi;
  UShort_t t_bx;
  UInt_t t_id;
  float t_pulse[10];
  float t_gain[10];
  float t_pedestal;
  float t_pedestal_rms;
  UShort_t t_gainmask;
  UInt_t t_nevt;

  std::vector<std::pair<UInt_t,UShort_t> > summed_index; // key: (id,gain[0])
  std::vector<std::vector<double> > summed_pulses;
  std::vector<std::vector<double> > summed_gains;
  std::vector<std::pair<double,double> > summed_pedestal;
  std::vector<long> summed_count;
  float min_amplitude_in_average;

  UShort_t old_lumi;
  std::vector<unsigned> bx_to_keep;
  bool bx_invert_selection;
  std::vector<UShort_t> seen_lumis;

};

//
// constants, enums and typedefs
//

//
// static data member definitions
//

//
// constructors and destructor
//
PulseTree::PulseTree(const edm::ParameterSet& iConfig)

{

   //now do what ever initialization is needed
   usesResource("TFileService");
   edm::Service<TFileService> fs;

   n_pedestal_samples = iConfig.getUntrackedParameter<unsigned int>("nPedestalSamples",3);
   do_average = iConfig.getUntrackedParameter<bool>("doAverage",false);
   split_lumis = iConfig.getUntrackedParameter<bool>("splitByLumi",false) && do_average;
   min_amplitude_in_average = iConfig.getUntrackedParameter<double>("minAmplitudeForAverage",-9e9);
   old_lumi = 0;

   do_EB = iConfig.getUntrackedParameter<bool>("processEB",true);
   do_EE = iConfig.getUntrackedParameter<bool>("processEE",true);
   if (do_EB) tok_ebdigis_ = consumes<EBDigiCollection>(iConfig.getUntrackedParameter<edm::InputTag>("EBDigiCollection",edm::InputTag("ecalDigis","ebDigis")));
   if (do_EE) tok_eedigis_ = consumes<EEDigiCollection>(iConfig.getUntrackedParameter<edm::InputTag>("EEDigiCollection",edm::InputTag("ecalDigis","eeDigis")));

   outTree = fs->make<TTree>("pulses","pulses");
   outTree->Branch("run",&t_run,"run/i");
   if (!do_average || split_lumis) outTree->Branch("lumi",&t_lumi,"lumi/s");
   if (!do_average) outTree->Branch("bx",&t_bx,"bx/s");
   outTree->Branch("id",&t_id,"id/i");
   outTree->Branch("pulse",t_pulse,"pulse[10]/F");
   outTree->Branch("gain",t_gain,"gain[10]/F");
   outTree->Branch("pedestal",&t_pedestal,"pedestal/F");
   if (do_average) outTree->Branch("pedestal_rms",&t_pedestal_rms,"pedestal_rms/F");
   if (!do_average) outTree->Branch("gainmask",&t_gainmask,"gainmask/s");
   if (do_average) outTree->Branch("nevt",&t_nevt,"nevt/i");

   bx_to_keep = iConfig.getUntrackedParameter<std::vector<unsigned> >("filterBx",std::vector<unsigned>());
   bx_invert_selection = iConfig.getUntrackedParameter<bool>("invertBxSelection",false);
   do_filter_bx = (bx_to_keep.size()!=0);

}


PulseTree::~PulseTree()
{
 
   // do anything here that needs to be done at desctruction time
   // (e.g. close files, deallocate resources etc.)

}


//
// member functions
//

// ------------ method called for each event  ------------
void
PulseTree::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup)
{
   using namespace edm;

   t_run = iEvent.eventAuxiliary().run();
   t_lumi = iEvent.eventAuxiliary().luminosityBlock();
   t_bx = iEvent.eventAuxiliary().bunchCrossing();
   if (do_filter_bx && !FilterBx(t_bx)) return;

   if (split_lumis && (t_lumi!=old_lumi) && old_lumi!=0) {
     if (std::find(seen_lumis.begin(),seen_lumis.end(),t_lumi)!=seen_lumis.end()){
       std::cout << "Warning: lumisection " << t_lumi << " already found previously, will be filled two times" << std::endl;
     }
     WriteAverageOutput();
     summed_index.clear();
     summed_pulses.clear();
     summed_gains.clear();
     summed_pedestal.clear();
     summed_count.clear();
     old_lumi=t_lumi;
   }

   Handle<EBDigiCollection> ebdigihandle;
   Handle<EEDigiCollection> eedigihandle;
   if (do_EB){
     iEvent.getByToken(tok_ebdigis_,ebdigihandle);
     auto ebdigis = ebdigihandle.product();
     for (uint i=0; i<ebdigis->size(); i++) FillDigi((*ebdigis)[i]);
   }
   if (do_EE){
     iEvent.getByToken(tok_eedigis_,eedigihandle);
     auto eedigis = eedigihandle.product();
     for (uint i=0; i<eedigis->size(); i++) FillDigi((*eedigis)[i]);
   }

}

void
PulseTree::FillDigi(EcalDataFrame digi){

  t_id = UInt_t(digi.id());
  t_gainmask = 0;

  for (int j=0; j<10; j++){ 
    t_pulse[j] = float(digi[j]&0xFFF);
    t_gain[j] = float((digi[j]>>12)&0x3);
    t_gainmask |= 1<<(int(t_gain[j]));
  }

  t_pedestal = 0;
  for (uint j=0; j<n_pedestal_samples; j++) t_pedestal += t_pulse[j];
  t_pedestal/=n_pedestal_samples;
  
  t_pedestal_rms = 0;
  t_nevt = 0;

  if (!do_average) {
    outTree->Fill();
  }
  else {
    if ((-t_pedestal + *std::max_element(t_pulse,t_pulse+10)) < min_amplitude_in_average) return; // do not average empty pulses
    auto thispair = std::pair<UInt_t,UShort_t>(t_id,int(t_gain[0]));
    auto it = find(summed_index.begin(),summed_index.end(),thispair);
    if (it==summed_index.end()){
      summed_index.push_back(thispair);
      summed_pulses.push_back(std::vector<double>(10,0));
      summed_gains.push_back(std::vector<double>(10,0));
      summed_pedestal.push_back(std::pair<double,double>(0,0));
      summed_count.push_back(0);
      it = find(summed_index.begin(),summed_index.end(),thispair);
    }
    uint idx = it-summed_index.begin();
    for (int j=0; j<10; j++){
      summed_pulses[idx][j]+=t_pulse[j];
      summed_gains[idx][j]+=t_gain[j];
    }
    summed_pedestal[idx] = std::pair<double,double>(summed_pedestal[idx].first+t_pedestal,summed_pedestal[idx].second+std::pow(t_pedestal,2));
    summed_count[idx]+=1;
  }

}

bool
PulseTree::FilterBx(unsigned bx){
  for (auto x : bx_to_keep) if (bx==x) return (!bx_invert_selection);
  return bx_invert_selection;
}

void
PulseTree::WriteAverageOutput(){

  t_gainmask = 0;
  t_bx = 0;

  for (uint idx = 0; idx<summed_count.size(); idx++){
    float den = summed_count[idx];
    for (int j=0; j<10; j++){
      t_pulse[j] = summed_pulses[idx][j]/den;
      t_gain[j] = summed_gains[idx][j]/den;
    }
    t_pedestal = summed_pedestal[idx].first/den;
    t_pedestal_rms = std::sqrt(summed_pedestal[idx].second/den - std::pow(summed_pedestal[idx].first/den,2));
    t_id = summed_index[idx].first;
    t_nevt = summed_count[idx];
    outTree->Fill();
  }

}

// ------------ method called once each job just before starting event loop  ------------
void 
PulseTree::beginJob()
{
}

// ------------ method called once each job just after ending the event loop  ------------
void 
PulseTree::endJob() 
{

  if (do_average) WriteAverageOutput();

}

// ------------ method fills 'descriptions' with the allowed parameters for the module  ------------
void
PulseTree::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  //The following says we do not know what parameters are allowed so do no validation
  // Please change this to state exactly what you do use, even if it is no parameters
  edm::ParameterSetDescription desc;
  desc.setUnknown();
  descriptions.addDefault(desc);
}

//define this as a plug-in
DEFINE_FWK_MODULE(PulseTree);