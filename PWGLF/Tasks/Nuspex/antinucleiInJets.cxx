// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
///
/// \file antinucleiInJets.cxx
///
/// \brief task for analysis of antinuclei in jets using Fastjet
/// \author Alberto Caliva (alberto.caliva@cern.ch), Chiara Pinto (chiara.pinto@cern.ch)
/// \since February 13, 2025

#include <vector>
#include <string>
#include <cmath>
#include <TList.h>
#include <TPDGCode.h>
#include <TRandom.h>
#include <TVector2.h>
#include <TVector3.h>
#include "TGrid.h"
#include <random>

#include "CCDB/BasicCCDBManager.h"
#include "CCDB/CcdbApi.h"
#include "Framework/runDataProcessing.h"
#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/ASoA.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/HistogramRegistry.h"
#include "Framework/RunningWorkflowInfo.h"
#include "Framework/DataTypes.h"
#include "Framework/Logger.h"
#include "ReconstructionDataFormats/Track.h"
#include "ReconstructionDataFormats/PID.h"
#include "ReconstructionDataFormats/DCA.h"
#include "Common/Core/trackUtilities.h"
#include "Common/Core/TrackSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"
#include "Common/DataModel/EventSelection.h"
#include "Common/DataModel/Centrality.h"
#include "Common/DataModel/PIDResponse.h"
#include "Common/DataModel/PIDResponseITS.h"

#include <fastjet/ClusterSequence.hh>
#include <fastjet/ClusterSequenceArea.hh>
#include <fastjet/tools/JetMedianBackgroundEstimator.hh>
#include <fastjet/tools/Subtractor.hh>
#include <fastjet/Selector.hh>
#include <fastjet/PseudoJet.hh>
#include <fastjet/AreaDefinition.hh>
#include <fastjet/GhostedAreaSpec.hh>
#include "PWGJE/Core/JetBkgSubUtils.h"
#include "PWGJE/Core/JetDerivedDataUtilities.h"
#include "PWGJE/DataModel/JetReducedData.h"
#include "PWGJE/DataModel/Jet.h"

using namespace std;
using namespace o2;
using namespace o2::soa;
using namespace o2::aod;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::constants::physics;
using namespace o2::constants::math;
using std::array;

using SelectedCollisions = soa::Join<aod::Collisions, aod::EvSels>;
using SimCollisions = soa::Join<aod::Collisions, aod::EvSels, aod::McCollisionLabels>;

using FullNucleiTracks = soa::Join<aod::Tracks, aod::TracksExtra, aod::TrackSelection, aod::TrackSelectionExtension, aod::TracksDCA, aod::pidTPCFullPr, aod::pidTPCFullDe, aod::pidTPCFullHe, aod::pidTOFFullPr, aod::pidTOFFullDe, aod::pidTOFFullHe>;

using MCTracks = soa::Join<aod::Tracks, aod::TracksExtra, aod::TrackSelection, aod::TrackSelectionExtension, aod::TracksDCA, aod::pidTPCFullPr, aod::pidTPCFullDe, aod::pidTPCFullHe, aod::pidTOFFullPr, aod::pidTOFFullDe, aod::pidTOFFullHe, aod::McTrackLabels>;

struct AntinucleiInJets {

  // histogram registries
  HistogramRegistry registryData{"registryData", {}, OutputObjHandlingPolicy::AnalysisObject, true, true};
  HistogramRegistry registryMC{"registryMC", {}, OutputObjHandlingPolicy::AnalysisObject, true, true};
  HistogramRegistry registryQC{"registryQC", {}, OutputObjHandlingPolicy::AnalysisObject, true, true};

  // global parameters
  Configurable<double> minJetPt{"minJetPt", 10.0, "Minimum pt of the jet"};
  Configurable<double> rJet{"rJet", 0.3, "Jet resolution parameter R"};
  Configurable<double> zVtx{"zVtx", 10.0, "Maximum zVertex"};
  Configurable<double> deltaEtaEdge{"deltaEtaEdge", 0.05, "eta gap from the edge"};

  // track parameters
  Configurable<bool> requirePvContributor{"requirePvContributor", false, "require that the track is a PV contributor"};
  Configurable<bool> applyItsPid{"applyItsPid", true, "apply ITS PID"};
  Configurable<bool> rejectEvents{"rejectEvents", false, "reject some events"};
  Configurable<int> rejectionPercentage{"rejectionPercentage", 3, "percentage of events to reject"};
  Configurable<int> minItsNclusters{"minItsNclusters", 5, "minimum number of ITS clusters"};
  Configurable<int> minTpcNcrossedRows{"minTpcNcrossedRows", 80, "minimum number of TPC crossed pad rows"};
  Configurable<double> minTpcNcrossedRowsOverFindable{"minTpcNcrossedRowsOverFindable", 0.8, "crossed rows/findable"};
  Configurable<double> maxChiSquareTpc{"maxChiSquareTpc", 4.0, "maximum TPC chi^2/Ncls"};
  Configurable<double> maxChiSquareIts{"maxChiSquareIts", 36.0, "maximum ITS chi^2/Ncls"};
  Configurable<double> minPt{"minPt", 0.3, "minimum pt of the tracks"};
  Configurable<double> minEta{"minEta", -0.8, "minimum eta"};
  Configurable<double> maxEta{"maxEta", +0.8, "maximum eta"};
  Configurable<double> maxDcaxy{"maxDcaxy", 0.05, "Maximum DCAxy"};
  Configurable<double> maxDcaz{"maxDcaz", 0.05, "Maximum DCAz"};
  Configurable<double> minNsigmaTpc{"minNsigmaTpc", -3.0, "Minimum nsigma TPC"};
  Configurable<double> maxNsigmaTpc{"maxNsigmaTpc", +3.0, "Maximum nsigma TPC"};
  Configurable<double> minNsigmaTof{"minNsigmaTof", -3.0, "Minimum nsigma TOF"};
  Configurable<double> maxNsigmaTof{"maxNsigmaTof", +3.5, "Maximum nsigma TOF"};
  Configurable<double> ptMaxItsPidProt{"ptMaxItsPidProt", 1.0, "maximum pt for ITS PID for protons"};
  Configurable<double> ptMaxItsPidDeut{"ptMaxItsPidDeut", 1.0, "maximum pt for ITS PID for deuterons"};
  Configurable<double> ptMaxItsPidHel{"ptMaxItsPidHel", 1.0, "maximum pt for ITS PID for helium"};
  Configurable<double> nSigmaItsMin{"nSigmaItsMin", -2.0, "nSigmaITS min"};
  Configurable<double> nSigmaItsMax{"nSigmaItsMax", +2.0, "nSigmaITS max"};

  // reweighting
  Configurable<bool> applyReweighting{"applyReweighting", true, "apply reweighting"};
  Configurable<std::string> urlToCcdb{"urlToCcdb", "http://alice-ccdb.cern.ch", "url of the personal ccdb"};
  Configurable<std::string> pathToFile{"pathToFile", "", "path to file with reweighting"};
  Configurable<std::string> histoNameWeightAntipJet{"histoNameWeightAntipJet", "", "reweighting histogram: antip in jet"};
  Configurable<std::string> histoNameWeightAntipUe{"histoNameWeightAntipUe", "", "reweighting histogram: antip in ue"};
  TH2F* twoDweightsAntipJet;
  TH2F* twoDweightsAntipUe;

  // jet pt unfolding
  Configurable<bool> applyPtUnfolding{"applyPtUnfolding", true, "apply jet pt unfolding"};
  Configurable<std::string> urlToCcdbPtUnfolding{"urlToCcdbPtUnfolding", "http://alice-ccdb.cern.ch", "url of the personal ccdb"};
  Configurable<std::string> pathToFilePtUnfolding{"pathToFilePtUnfolding", "Users/c/chpinto/My/Object/ResponseMatrix", "path to file with pt unfolding"};
  Configurable<std::string> histoNamePtUnfolding{"histoNamePtUnfolding", "detectorResponseMatrix", "pt unfolding histogram"};
  TH2F* responseMatrix;

  Service<o2::ccdb::BasicCCDBManager> ccdb;
  o2::ccdb::CcdbApi ccdbApi;

  JetBkgSubUtils backgroundSub;

  void init(InitContext const&)
  {
    ccdb->setURL(urlToCcdb.value);
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();
    ccdb->setCreatedNotAfter(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    ccdb->setFatalWhenNull(false);

    if (applyReweighting) {
      getReweightingHistograms(ccdb, TString(pathToFile), TString(histoNameWeightAntipJet), TString(histoNameWeightAntipUe));
    } else {
      twoDweightsAntipJet = nullptr;
      twoDweightsAntipUe = nullptr;
    }

    if (applyPtUnfolding) {
      getPtUnfoldingHistogram(ccdb, TString(pathToFilePtUnfolding), TString(histoNamePtUnfolding));
    } else {
      responseMatrix = nullptr;
    }

    // binning
    double min = 0.0;
    double max = 6.0;
    int nbins = 120;

    // QC histograms
    if (doprocessQC) {
      registryQC.add("deltaEta_deltaPhi_jet", "deltaEta_deltaPhi_jet", HistType::kTH2F, {{200, -0.5, 0.5, "#Delta#eta"}, {200, 0, PIHalf, "#Delta#phi"}});
      registryQC.add("deltaEta_deltaPhi_ue", "deltaEta_deltaPhi_ue", HistType::kTH2F, {{200, -0.5, 0.5, "#Delta#eta"}, {200, 0, PIHalf, "#Delta#phi"}});
      registryQC.add("eta_phi_jet", "eta_phi_jet", HistType::kTH2F, {{200, -0.5, 0.5, "#eta_{jet}"}, {200, 0, TwoPI, "#phi_{jet}"}});
      registryQC.add("eta_phi_ue", "eta_phi_ue", HistType::kTH2F, {{200, -0.5, 0.5, "#eta_{UE}"}, {200, 0, TwoPI, "#phi_{UE}"}});
      registryQC.add("NchJetCone", "NchJetCone", HistType::kTH1F, {{100, 0, 100, "#it{N}_{ch}"}});
      registryQC.add("NchJet", "NchJet", HistType::kTH1F, {{100, 0, 100, "#it{N}_{ch}"}});
      registryQC.add("NchUE", "NchUE", HistType::kTH1F, {{100, 0, 100, "#it{N}_{ch}"}});
      registryQC.add("sumPtJetCone", "sumPtJetCone", HistType::kTH1F, {{500, 0, 50, "#it{p}_{T} (GeV/#it{c})"}});
      registryQC.add("sumPtJet", "sumPtJet", HistType::kTH1F, {{500, 0, 50, "#it{p}_{T} (GeV/#it{c})"}});
      registryQC.add("sumPtUE", "sumPtUE", HistType::kTH1F, {{500, 0, 50, "#it{p}_{T} (GeV/#it{c})"}});
      registryQC.add("nJetsFound", "nJetsFound", HistType::kTH1F, {{50, 0, 50, "#it{n}_{Jet}"}});
      registryQC.add("nJetsInAcceptance", "nJetsInAcceptance", HistType::kTH1F, {{50, 0, 50, "#it{n}_{Jet}"}});
      registryQC.add("nJetsSelectedHighPt", "nJetsSelectedHighPt", HistType::kTH1F, {{50, 0, 50, "#it{n}_{Jet}"}});
      registryQC.add("jetEffectiveArea", "jetEffectiveArea", HistType::kTH1F, {{2000, 0, 2, "Area/#piR^{2}"}});
      registryQC.add("jetPtDifference", "jetPtDifference", HistType::kTH1F, {{200, -1, 1, "#Deltap_{T}^{jet}"}});
    }

    // data histograms
    if (doprocessData) {

      // event counter data
      registryData.add("number_of_events_data", "number of events in data", HistType::kTH1F, {{10, 0, 10, "counter"}});
      registryData.add("number_of_rejected_events", "check on number of events rejected", HistType::kTH1F, {{10, 0, 10, "counter"}});

      // antiprotons
      registryData.add("antiproton_jet_tpc", "antiproton_jet_tpc", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("antiproton_jet_tof", "antiproton_jet_tof", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});
      registryData.add("antiproton_ue_tpc", "antiproton_ue_tpc", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("antiproton_ue_tof", "antiproton_ue_tof", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});
      registryData.add("antiproton_dca_jet", "antiproton_dca_jet", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {200, -0.5, 0.5, "DCA_{xy} (cm)"}});
      registryData.add("antiproton_dca_ue", "antiproton_dca_ue", HistType::kTH2F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {200, -0.5, 0.5, "DCA_{xy} (cm)"}});

      // antideuterons
      registryData.add("antideuteron_jet_tpc", "antideuteron_jet_tpc", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("antideuteron_jet_tof", "antideuteron_jet_tof", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});
      registryData.add("antideuteron_ue_tpc", "antideuteron_ue_tpc", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("antideuteron_ue_tof", "antideuteron_ue_tof", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});

      // deuterons
      registryData.add("deuteron_jet_tof", "deuteron_jet_tof", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});
      registryData.add("deuteron_ue_tof", "deuteron_ue_tof", HistType::kTH2F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}});

      // antihelium-3
      registryData.add("antihelium3_jet_tpc", "antihelium3_jet_tpc", HistType::kTH2F, {{nbins, min * 3, max * 3, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("antihelium3_ue_tpc", "antihelium3_ue_tpc", HistType::kTH2F, {{nbins, min * 3, max * 3, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});

      // helium-3
      registryData.add("helium3_jet_tpc", "helium3_jet_tpc", HistType::kTH2F, {{nbins, min * 3, max * 3, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
      registryData.add("helium3_ue_tpc", "helium3_ue_tpc", HistType::kTH2F, {{nbins, min * 3, max * 3, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}});
    }

    // monte carlo histograms
    if (doprocessEfficiency) {

      // event counter MC
      registryMC.add("number_of_events_mc", "number of events in mc", HistType::kTH1F, {{10, 0, 10, "counter"}});

      // generated spectra
      registryMC.add("antiproton_incl_gen", "antiproton_incl_gen", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("deuteron_incl_gen", "deuteron_incl_gen", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antideuteron_incl_gen", "antideuteron_incl_gen", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("helium3_incl_gen", "helium3_incl_gen", HistType::kTH1F, {{nbins, 3 * min, 3 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antihelium3_incl_gen", "antihelium3_incl_gen", HistType::kTH1F, {{nbins, 3 * min, 3 * max, "#it{p}_{T} (GeV/#it{c})"}});

      // reconstructed TPC
      registryMC.add("antiproton_incl_rec_tpc", "antiproton_incl_rec_tpc", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antideuteron_incl_rec_tpc", "antideuteron_incl_rec_tpc", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("deuteron_incl_rec_tpc", "deuteron_incl_rec_tpc", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antihelium3_incl_rec_tpc", "antihelium3_incl_rec_tpc", HistType::kTH1F, {{nbins, 3 * min, 3 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("helium3_incl_rec_tpc", "helium3_incl_rec_tpc", HistType::kTH1F, {{nbins, 3 * min, 3 * max, "#it{p}_{T} (GeV/#it{c})"}});

      // reconstructed TOF
      registryMC.add("antiproton_incl_rec_tof", "antiproton_incl_rec_tof", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antideuteron_incl_rec_tof", "antideuteron_incl_rec_tof", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("deuteron_incl_rec_tof", "deuteron_incl_rec_tof", HistType::kTH1F, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}});

      // fraction of primary antiprotons from MC
      registryMC.add("antiproton_incl_prim", "antiproton_incl_prim", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_incl_all", "antiproton_incl_all", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});

      // antiproton reweighting
      registryMC.add("antiproton_eta_pt_pythia", "antiproton_eta_pt_pythia", HistType::kTH2F, {{200, 0.0, 10.0, "#it{p}_{T} (GeV/#it{c})"}, {20, -1.0, 1.0, "#it{#eta}"}});
    }

    if (doprocessJetsMCgen) {
      registryMC.add("antiproton_jet_gen", "antiproton_jet_gen", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_ue_gen", "antiproton_ue_gen", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_eta_pt_jet", "antiproton_eta_pt_jet", HistType::kTH2F, {{200, 0.0, 10.0, "#it{p}_{T} (GeV/#it{c})"}, {20, -1.0, 1.0, "#it{#eta}"}});
      registryMC.add("antiproton_eta_pt_ue", "antiproton_eta_pt_ue", HistType::kTH2F, {{200, 0.0, 10.0, "#it{p}_{T} (GeV/#it{c})"}, {20, -1.0, 1.0, "#it{#eta}"}});
    }

    if (doprocessJetsMCrec) {
      registryMC.add("antiproton_jet_prim", "antiproton_jet_prim", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_jet_all", "antiproton_jet_all", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_ue_prim", "antiproton_ue_prim", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_ue_all", "antiproton_all_ue", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_jet_rec_tpc", "antiproton_jet_rec_tpc", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_ue_rec_tpc", "antiproton_ue_rec_tpc", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_jet_rec_tof", "antiproton_jet_rec_tof", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_ue_rec_tof", "antiproton_ue_rec_tof", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});

      // detector response matrix
      registryMC.add("detectorResponseMatrix", "detectorResponseMatrix", HistType::kTH2F, {{1000, 0.0, 100.0, "#it{p}_{T}^{rec} (GeV/#it{c})"}, {2000, -20.0, 20.0, "#it{p}_{T}^{gen} - #it{p}_{T}^{rec} (GeV/#it{c})"}});
    }

    // systematic uncertainties
    if (doprocessSystematicsData) {
      registryData.add("number_of_rejected_events_syst", "check on number of events rejected", HistType::kTH1F, {{10, 0, 10, "counter"}});
      registryData.add("antiproton_tpc_syst", "antiproton_tpc_syst", HistType::kTHnSparseF, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}, {10, 0, 10, "systematic uncertainty"}});
      registryData.add("antiproton_tof_syst", "antiproton_tof_syst", HistType::kTHnSparseF, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}, {10, 0, 10, "systematic uncertainty"}});
      registryData.add("antideuteron_tpc_syst", "antideuteron_tpc_syst", HistType::kTHnSparseF, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TPC}"}, {10, 0, 10, "systematic uncertainty"}});
      registryData.add("antideuteron_tof_syst", "antideuteron_tof_syst", HistType::kTHnSparseF, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}, {400, -20.0, 20.0, "n#sigma_{TOF}"}, {10, 0, 10, "systematic uncertainty"}});
    }

    if (doprocessSystematicsEfficiency) {
      registryMC.add("antiproton_incl_gen_syst", "antiproton_incl_gen_syst", HistType::kTH1F, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antideuteron_incl_gen_syst", "antideuteron_incl_gen_syst", HistType::kTH1F, {{nbins, min * 2, max * 2, "#it{p}_{T} (GeV/#it{c})"}});
      registryMC.add("antiproton_incl_prim_syst", "antiproton_incl_prim_syst", HistType::kTHnSparseF, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {10, 0, 10, "systematic uncertainty"}});
      registryMC.add("antiproton_incl_rec_tpc_syst", "antiproton_incl_rec_tpc_syst", HistType::kTHnSparseF, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {10, 0, 10, "systematic uncertainty"}});
      registryMC.add("antiproton_incl_rec_tof_syst", "antiproton_incl_rec_tof_syst", HistType::kTHnSparseF, {{nbins, min, max, "#it{p}_{T} (GeV/#it{c})"}, {10, 0, 10, "systematic uncertainty"}});
      registryMC.add("antideuteron_incl_rec_tpc_syst", "antideuteron_incl_rec_tpc_syst", HistType::kTHnSparseF, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}, {10, 0, 10, "systematic uncertainty"}});
      registryMC.add("antideuteron_incl_rec_tof_syst", "antideuteron_incl_rec_tof_syst", HistType::kTHnSparseF, {{nbins, 2 * min, 2 * max, "#it{p}_{T} (GeV/#it{c})"}, {10, 0, 10, "systematic uncertainty"}});
    }
  }

  void getPerpendicularAxis(const TVector3& p, TVector3& u, double sign)
  {
    double px = p.X();
    double py = p.Y();
    double pz = p.Z();

    double px2 = px * px;
    double py2 = py * py;
    double pz2 = pz * pz;
    double pz4 = pz2 * pz2;

    // px and py are both zero
    if (px == 0 && py == 0) {
      u.SetXYZ(0, 0, 0);
      return;
    }

    // protection 1
    if (px == 0 && py != 0) {
      double ux = sign * std::sqrt(py2 - pz4 / py2);
      double uy = -pz2 / py;
      u.SetXYZ(ux, uy, pz);
      return;
    }

    // protection 2
    if (py == 0 && px != 0) {
      double ux = -pz2 / px;
      double uy = sign * std::sqrt(px2 - pz4 / px2);
      u.SetXYZ(ux, uy, pz);
      return;
    }

    // General case
    double a = px2 + py2;
    double b = 2.0 * px * pz2;
    double c = pz4 - py2 * py2 - px2 * py2;

    double delta = b * b - 4.0 * a * c;

    if (delta < 0 || a == 0) {
      LOGP(warn, "Invalid input in getPerpendicularAxis: delta = {}, a = {}", delta, a);
      u.SetXYZ(0, 0, 0);
      return;
    }

    double ux = (-b + sign * std::sqrt(delta)) / (2.0 * a);
    double uy = (-pz2 - px * ux) / py;
    u.SetXYZ(ux, uy, pz);
  }

  double getDeltaPhi(double a1, double a2)
  {
    double deltaPhi(0);
    double phi1 = TVector2::Phi_0_2pi(a1);
    double phi2 = TVector2::Phi_0_2pi(a2);
    double diff = std::fabs(phi1 - phi2);

    if (diff <= PI)
      deltaPhi = diff;
    if (diff > PI)
      deltaPhi = TwoPI - diff;

    return deltaPhi;
  }

  // ITS hit
  template <typename TrackIts>
  bool hasITSHit(const TrackIts& track, int layer)
  {
    int ibit = layer - 1;
    return (track.itsClusterMap() & (1 << ibit));
  }

  // single-track selection for particles inside jets
  template <typename JetTrack>
  bool passedTrackSelectionForJetReconstruction(const JetTrack& track)
  {

    const int minTpcCr = 70;
    const double minCrFindable = 0.8;
    const double maxChi2Tpc = 4.0;
    const double maxChi2Its = 36.0;
    const double maxPseudorapidity = 0.8;
    const double minPtTrack = 0.1;
    const double dcaxyMaxTrackPar0 = 0.0105;
    const double dcaxyMaxTrackPar1 = 0.035;
    const double dcaxyMaxTrackPar2 = 1.1;
    const double dcazMaxTrack = 2.0;

    if (!track.hasITS())
      return false;
    if ((!hasITSHit(track, 1)) && (!hasITSHit(track, 2)) && (!hasITSHit(track, 3)))
      return false;
    if (!track.hasTPC())
      return false;
    if (track.tpcNClsCrossedRows() < minTpcCr)
      return false;
    if ((static_cast<double>(track.tpcNClsCrossedRows()) / static_cast<double>(track.tpcNClsFindable())) < minCrFindable)
      return false;
    if (track.tpcChi2NCl() > maxChi2Tpc)
      return false;
    if (track.itsChi2NCl() > maxChi2Its)
      return false;
    if (track.eta() < -maxPseudorapidity || track.eta() > maxPseudorapidity)
      return false;
    if (track.pt() < minPtTrack)
      return false;
    if (std::fabs(track.dcaXY()) > (dcaxyMaxTrackPar0 + dcaxyMaxTrackPar1 / std::pow(track.pt(), dcaxyMaxTrackPar2)))
      return false;
    if (std::fabs(track.dcaZ()) > dcazMaxTrack)
      return false;
    return true;
  }

  // single-track selection
  template <typename AntinucleusTrack>
  bool passedTrackSelection(const AntinucleusTrack& track)
  {
    if (requirePvContributor && !(track.isPVContributor()))
      return false;
    if (!track.hasITS())
      return false;
    if (track.itsNCls() < minItsNclusters)
      return false;
    if (!track.hasTPC())
      return false;
    if (track.tpcNClsCrossedRows() < minTpcNcrossedRows)
      return false;
    if ((static_cast<double>(track.tpcNClsCrossedRows()) / static_cast<double>(track.tpcNClsFindable())) < minTpcNcrossedRowsOverFindable)
      return false;
    if (track.tpcChi2NCl() > maxChiSquareTpc)
      return false;
    if (track.itsChi2NCl() > maxChiSquareIts)
      return false;
    if (track.eta() < minEta || track.eta() > maxEta)
      return false;
    if (track.pt() < minPt)
      return false;

    return true;
  }

  template <typename AntiprotonTrack>
  bool isHighPurityAntiproton(const AntiprotonTrack& track)
  {
    // variables
    double nsigmaTPCPr = track.tpcNSigmaPr();
    double nsigmaTOFPr = track.tofNSigmaPr();
    double pt = track.pt();
    double ptThreshold = 0.5;
    double nsigmaMaxPr = 2.0;

    if (pt < ptThreshold && std::fabs(nsigmaTPCPr) < nsigmaMaxPr)
      return true;
    if (pt >= ptThreshold && std::fabs(nsigmaTPCPr) < nsigmaMaxPr && track.hasTOF() && std::fabs(nsigmaTOFPr) < nsigmaMaxPr)
      return true;
    return false;
  }

  double getCorrectedPt(double ptRec, TH2* responseMatrix)
  {
    if (!responseMatrix) {
      LOGP(error, "Response matrix is null. Returning uncorrected pt.");
      return ptRec;
    }

    int binX = responseMatrix->GetXaxis()->FindBin(ptRec);
    if (binX < 1 || binX > responseMatrix->GetNbinsX()) {
        LOGP(error, "Bin index out of range: binX = {}", binX);
        return ptRec; // Return uncorrected pt if bin index is invalid
    }
    std::unique_ptr<TH1D> proj(responseMatrix->ProjectionY("proj", binX, binX));

    // add a protection in case the projection is empty
    if (proj->GetEntries() == 0) {
      return ptRec;
    }

    double deltaPt = proj->GetRandom();
    double ptGen = ptRec + deltaPt;

    return ptGen;
  }

  void getPtUnfoldingHistogram(o2::framework::Service<o2::ccdb::BasicCCDBManager> const& ccdbObj, TString filepath, TString histoNamePtUnfolding)
  {
    TList* l = ccdbObj->get<TList>(filepath.Data());
    if (!l) {
      LOGP(error, "Could not open the file {}", Form("%s", filepath.Data()));
      return;
    }
    TObject* obj = l->FindObject(Form("%s", histoNamePtUnfolding.Data()));
    if (!obj || !obj->InheritsFrom(TH2F::Class())) {
      LOGP(error, "Could not find a valid TH2F histogram {}", Form("%s", histoNamePtUnfolding.Data()));
      return;
    }
    responseMatrix = static_cast<TH2F*>(obj);
    LOGP(info, "Opened histogram {}", Form("%s", histoNamePtUnfolding.Data()));
  }

  void getReweightingHistograms(o2::framework::Service<o2::ccdb::BasicCCDBManager> const& ccdbObj, TString filepath, TString histname_antip_jet, TString histname_antip_ue)
  {
    TList* l = ccdbObj->get<TList>(filepath.Data());
    if (!l) {
      LOGP(error, "Could not open the file {}", Form("%s", filepath.Data()));
      return;
    }
    twoDweightsAntipJet = static_cast<TH2F*>(l->FindObject(Form("%s_antiproton", histname_antip_jet.Data())));
    if (!twoDweightsAntipJet) {
      LOGP(error, "Could not open histogram {}", Form("%s_antiproton", histname_antip_jet.Data()));
      return;
    }
    twoDweightsAntipUe = static_cast<TH2F*>(l->FindObject(Form("%s_antiproton", histname_antip_ue.Data())));
    if (!twoDweightsAntipUe) {
      LOGP(error, "Could not open histogram {}", Form("%s_antiproton", histname_antip_ue.Data()));
      return;
    }
    LOGP(info, "Opened histogram {}", Form("%s_antiproton", histname_antip_jet.Data()));
    LOGP(info, "Opened histogram {}", Form("%s_antiproton", histname_antip_ue.Data()));
  }

  bool shouldRejectEvent()
  {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 99);
    int randomNumber = dis(gen);
    if (randomNumber > rejectionPercentage) {
      return false; // accept event
    }
    return true; // reject event
  }

  // Process Data
  void processData(SelectedCollisions::iterator const& collision, FullNucleiTracks const& tracks)
  {
    if (rejectEvents) {
      // event counter: before event rejection
      registryData.fill(HIST("number_of_rejected_events"), 0.5);

      if (shouldRejectEvent())
        return;

      // event counter: after event rejection
      registryData.fill(HIST("number_of_rejected_events"), 1.5);
    }

    // event counter: before event selection
    registryData.fill(HIST("number_of_events_data"), 0.5);

    // event selection
    if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
      return;

    // event counter: after event selection
    registryData.fill(HIST("number_of_events_data"), 1.5);

    // loop over reconstructed tracks
    int id(-1);
    std::vector<fastjet::PseudoJet> fjParticles;
    for (auto const& track : tracks) {
      id++;
      if (!passedTrackSelectionForJetReconstruction(track))
        continue;

      // 4-momentum representation of a particle
      fastjet::PseudoJet fourMomentum(track.px(), track.py(), track.pz(), track.energy(MassPionCharged));
      fourMomentum.set_user_index(id);
      fjParticles.emplace_back(fourMomentum);
    }

    // reject empty events
    if (fjParticles.size() < 1)
      return;
    registryData.fill(HIST("number_of_events_data"), 2.5);

    // cluster particles using the anti-kt algorithm
    fastjet::JetDefinition jetDef(fastjet::antikt_algorithm, rJet);
    fastjet::AreaDefinition areaDef(fastjet::active_area, fastjet::GhostedAreaSpec(1.0)); // active_area_explicit_ghosts
    fastjet::ClusterSequenceArea cs(fjParticles, jetDef, areaDef);
    std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
    auto [rhoPerp, rhoMPerp] = backgroundSub.estimateRhoPerpCone(fjParticles, jets);

    // loop over reconstructed jets
    bool isAtLeastOneJetSelected = false;
    for (const auto& jet : jets) {

      // jet must be fully contained in the acceptance
      if ((std::fabs(jet.eta()) + rJet) > (maxEta - deltaEtaEdge))
        continue;

      // jet pt must be larger than threshold
      auto jetForSub = jet;
      fastjet::PseudoJet jetMinusBkg = backgroundSub.doRhoAreaSub(jetForSub, rhoPerp, rhoMPerp);
      if (getCorrectedPt(jetMinusBkg.pt(), responseMatrix) < minJetPt)
        continue;
      isAtLeastOneJetSelected = true;

      // perpendicular cone
      double coneRadius = std::sqrt(jet.area() / PI);
      TVector3 jetAxis(jet.px(), jet.py(), jet.pz()); // before or after subtraction of perpendicular cone?
      TVector3 ueAxis1(0, 0, 0);
      TVector3 ueAxis2(0, 0, 0);
      getPerpendicularAxis(jetAxis, ueAxis1, +1);
      getPerpendicularAxis(jetAxis, ueAxis2, -1);

      // get jet constituents
      std::vector<fastjet::PseudoJet> jetConstituents = jet.constituents();
      o2::aod::ITSResponse itsResponse;

      // loop over jet constituents
      for (const auto& particle : jetConstituents) {

        // get corresponding track and apply track selection criteria
        auto const& track = tracks.iteratorAt(particle.user_index());
        if (!passedTrackSelection(track))
          continue;

        // variables
        double nsigmaTPCPr = track.tpcNSigmaPr();
        double nsigmaTOFPr = track.tofNSigmaPr();
        double nsigmaTPCDe = track.tpcNSigmaDe();
        double nsigmaTOFDe = track.tofNSigmaDe();
        double nsigmaTPCHe = track.tpcNSigmaHe();
        double pt = track.pt();
        double dcaxy = track.dcaXY();
        double dcaz = track.dcaZ();

        // fill DCA distribution for antiprotons
        if (track.sign() < 0 && isHighPurityAntiproton(track) && std::fabs(dcaz) < maxDcaz) {
          registryData.fill(HIST("antiproton_dca_jet"), pt, dcaxy);
        }

        // DCA selections
        if (std::fabs(dcaxy) > maxDcaxy || std::fabs(dcaz) > maxDcaz)
          continue;

        // particle identification using the ITS cluster size
        bool passedItsPidProt(false), passedItsPidDeut(false), passedItsPidHel(false);
        if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
          passedItsPidProt = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) < nSigmaItsMax) {
          passedItsPidDeut = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) < nSigmaItsMax) {
          passedItsPidHel = true;
        }
        if (!applyItsPid) {
          passedItsPidProt = true;
          passedItsPidDeut = true;
          passedItsPidHel = true;
        }
        if (pt > ptMaxItsPidProt)
          passedItsPidProt = true;
        if (pt > ptMaxItsPidDeut)
          passedItsPidDeut = true;
        if ((2.0 * pt) > ptMaxItsPidHel)
          passedItsPidHel = true;

        // antimatter
        if (track.sign() < 0) {
          if (passedItsPidProt) {
            registryData.fill(HIST("antiproton_jet_tpc"), pt, nsigmaTPCPr);
            if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc && track.hasTOF())
              registryData.fill(HIST("antiproton_jet_tof"), pt, nsigmaTOFPr);
          }
          if (passedItsPidDeut) {
            registryData.fill(HIST("antideuteron_jet_tpc"), pt, nsigmaTPCDe);
            if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc && track.hasTOF())
              registryData.fill(HIST("antideuteron_jet_tof"), pt, nsigmaTOFDe);
          }
          if (passedItsPidHel) {
            registryData.fill(HIST("antihelium3_jet_tpc"), 2.0 * pt, nsigmaTPCHe);
          }
        }

        // matter
        if (track.sign() > 0) {
          if (passedItsPidDeut && nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc && track.hasTOF())
            registryData.fill(HIST("deuteron_jet_tof"), pt, nsigmaTOFDe);
          if (passedItsPidHel) {
            registryData.fill(HIST("helium3_jet_tpc"), 2.0 * pt, nsigmaTPCHe);
          }
        }
      }

      // underlying event
      for (auto const& track : tracks) {

        // get corresponding track and apply track selection criteria
        if (!passedTrackSelection(track))
          continue;

        double deltaEtaUe1 = track.eta() - ueAxis1.Eta();
        double deltaPhiUe1 = getDeltaPhi(track.phi(), ueAxis1.Phi());
        double deltaRUe1 = std::sqrt(deltaEtaUe1 * deltaEtaUe1 + deltaPhiUe1 * deltaPhiUe1);
        double deltaEtaUe2 = track.eta() - ueAxis2.Eta();
        double deltaPhiUe2 = getDeltaPhi(track.phi(), ueAxis2.Phi());
        double deltaRUe2 = std::sqrt(deltaEtaUe2 * deltaEtaUe2 + deltaPhiUe2 * deltaPhiUe2);
        if (deltaRUe1 > coneRadius && deltaRUe2 > coneRadius)
          continue;

        // variables
        double nsigmaTPCPr = track.tpcNSigmaPr();
        double nsigmaTOFPr = track.tofNSigmaPr();
        double nsigmaTPCDe = track.tpcNSigmaDe();
        double nsigmaTOFDe = track.tofNSigmaDe();
        double nsigmaTPCHe = track.tpcNSigmaHe();
        double pt = track.pt();
        double dcaxy = track.dcaXY();
        double dcaz = track.dcaZ();

        // fill DCA distribution for antiprotons
        if (track.sign() < 0 && isHighPurityAntiproton(track) && std::fabs(dcaz) < maxDcaz) {
          registryData.fill(HIST("antiproton_dca_ue"), pt, dcaxy);
        }

        // DCA selections
        if (std::fabs(dcaxy) > maxDcaxy || std::fabs(dcaz) > maxDcaz)
          continue;

        // particle identification using the ITS cluster size
        bool passedItsPidProt(false), passedItsPidDeut(false), passedItsPidHel(false);
        if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
          passedItsPidProt = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) < nSigmaItsMax) {
          passedItsPidDeut = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) < nSigmaItsMax) {
          passedItsPidHel = true;
        }
        if (!applyItsPid) {
          passedItsPidProt = true;
          passedItsPidDeut = true;
          passedItsPidHel = true;
        }
        if (pt > ptMaxItsPidProt)
          passedItsPidProt = true;
        if (pt > ptMaxItsPidDeut)
          passedItsPidDeut = true;
        if ((2.0 * pt) > ptMaxItsPidHel)
          passedItsPidHel = true;

        // antimatter
        if (track.sign() < 0) {
          if (passedItsPidProt) {
            registryData.fill(HIST("antiproton_ue_tpc"), pt, nsigmaTPCPr);
            if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc && track.hasTOF())
              registryData.fill(HIST("antiproton_ue_tof"), pt, nsigmaTOFPr);
          }
          if (passedItsPidDeut) {
            registryData.fill(HIST("antideuteron_ue_tpc"), pt, nsigmaTPCDe);
            if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc && track.hasTOF())
              registryData.fill(HIST("antideuteron_ue_tof"), pt, nsigmaTOFDe);
          }
          if (passedItsPidHel) {
            registryData.fill(HIST("antihelium3_ue_tpc"), 2.0 * pt, nsigmaTPCHe);
          }
        }

        // matter
        if (track.sign() > 0) {
          if (passedItsPidDeut && nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc && track.hasTOF())
            registryData.fill(HIST("deuteron_ue_tof"), pt, nsigmaTOFDe);
          // helium3
          if (passedItsPidHel) {
            registryData.fill(HIST("helium3_ue_tpc"), 2.0 * pt, nsigmaTPCHe);
          }
        }
      }
    }
    if (isAtLeastOneJetSelected) {
      registryData.fill(HIST("number_of_events_data"), 3.5);
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processData, "Process Data", true);

  // Process QC
  void processQC(SelectedCollisions::iterator const& collision, FullNucleiTracks const& tracks)
  {
    // event selection
    if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
      return;

    // loop over reconstructed tracks
    std::vector<fastjet::PseudoJet> fjParticles;
    for (auto const& track : tracks) {
      if (!passedTrackSelectionForJetReconstruction(track))
        continue;

      // 4-momentum representation of a particle
      fastjet::PseudoJet fourMomentum(track.px(), track.py(), track.pz(), track.energy(MassPionCharged));
      fjParticles.emplace_back(fourMomentum);
    }

    // reject empty events
    if (fjParticles.size() < 1)
      return;

    // cluster particles using the anti-kt algorithm
    fastjet::JetDefinition jetDef(fastjet::antikt_algorithm, rJet);
    fastjet::AreaDefinition areaDef(fastjet::active_area, fastjet::GhostedAreaSpec(1.0)); // active_area_explicit_ghosts
    fastjet::ClusterSequenceArea cs(fjParticles, jetDef, areaDef);
    std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
    auto [rhoPerp, rhoMPerp] = backgroundSub.estimateRhoPerpCone(fjParticles, jets);

    // loop over reconstructed jets
    int njetsInAcc(0);
    int njetsHighPt(0);
    for (const auto& jet : jets) {

      // jet must be fully contained in the acceptance
      if ((std::fabs(jet.eta()) + rJet) > (maxEta - deltaEtaEdge))
        continue;
      njetsInAcc++;
      registryQC.fill(HIST("sumPtJetCone"), jet.pt());
      double ptJetBeforeSub = jet.pt();

      // jet pt must be larger than threshold
      auto jetForSub = jet;
      fastjet::PseudoJet jetMinusBkg = backgroundSub.doRhoAreaSub(jetForSub, rhoPerp, rhoMPerp);
      double ptJetAfterSub = jetForSub.pt();
      registryQC.fill(HIST("jetPtDifference"), ptJetAfterSub - ptJetBeforeSub);

      if (getCorrectedPt(jetMinusBkg.pt(), responseMatrix) < minJetPt)
        continue;
      njetsHighPt++;
      registryQC.fill(HIST("sumPtJet"), jet.pt());

      // jet properties and perpendicular cone
      std::vector<fastjet::PseudoJet> jetConstituents = jet.constituents();
      TVector3 jetAxis(jet.px(), jet.py(), jet.pz());
      double coneRadius = std::sqrt(jet.area() / PI);
      TVector3 ueAxis1(0, 0, 0);
      TVector3 ueAxis2(0, 0, 0);
      getPerpendicularAxis(jetAxis, ueAxis1, +1);
      getPerpendicularAxis(jetAxis, ueAxis2, -1);

      registryQC.fill(HIST("jetEffectiveArea"), jet.area() / (PI * rJet * rJet));
      registryQC.fill(HIST("NchJetCone"), static_cast<int>(jetConstituents.size()));

      // loop over jet constituents
      for (const auto& particle : jetConstituents) {

        double deltaEta = particle.eta() - jetAxis.Eta();
        double deltaPhi = getDeltaPhi(particle.phi(), jetAxis.Phi());
        registryQC.fill(HIST("deltaEta_deltaPhi_jet"), deltaEta, deltaPhi);
        registryQC.fill(HIST("eta_phi_jet"), particle.eta(), particle.phi());
      }

      // loop over particles in perpendicular cones
      double nParticlesPerp(0);
      double ptPerp(0);
      for (auto const& track : tracks) {

        if (!passedTrackSelectionForJetReconstruction(track))
          continue;

        double deltaEtaUe1 = track.eta() - ueAxis1.Eta();
        double deltaPhiUe1 = getDeltaPhi(track.phi(), ueAxis1.Phi());
        double deltaRUe1 = std::sqrt(deltaEtaUe1 * deltaEtaUe1 + deltaPhiUe1 * deltaPhiUe1);
        double deltaEtaUe2 = track.eta() - ueAxis2.Eta();
        double deltaPhiUe2 = getDeltaPhi(track.phi(), ueAxis2.Phi());
        double deltaRUe2 = std::sqrt(deltaEtaUe2 * deltaEtaUe2 + deltaPhiUe2 * deltaPhiUe2);
        if (deltaRUe1 > coneRadius && deltaRUe2 > coneRadius)
          continue;

        ptPerp = ptPerp + track.pt();
        nParticlesPerp++;
        registryQC.fill(HIST("deltaEta_deltaPhi_ue"), deltaEtaUe1, deltaPhiUe1);
        registryQC.fill(HIST("deltaEta_deltaPhi_ue"), deltaEtaUe2, deltaPhiUe2);
        registryQC.fill(HIST("eta_phi_ue"), track.eta(), track.phi());
      }
      registryQC.fill(HIST("NchUE"), 0.5 * nParticlesPerp);
      registryQC.fill(HIST("NchJet"), static_cast<double>(jetConstituents.size()) - 0.5 * nParticlesPerp);
      registryQC.fill(HIST("sumPtUE"), 0.5 * ptPerp);
    }
    registryQC.fill(HIST("nJetsFound"), static_cast<int>(jets.size()));
    registryQC.fill(HIST("nJetsInAcceptance"), njetsInAcc);
    registryQC.fill(HIST("nJetsSelectedHighPt"), njetsHighPt);
  }
  PROCESS_SWITCH(AntinucleiInJets, processQC, "Process QC", false);

  void processEfficiency(SimCollisions const& collisions, MCTracks const& mcTracks, aod::McParticles const& mcParticles)
  {
    for (const auto& collision : collisions) {

      // event counter before event selection
      registryMC.fill(HIST("number_of_events_mc"), 0.5);

      // event selection
      if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
        continue;

      // event counter after event selection
      registryMC.fill(HIST("number_of_events_mc"), 1.5);

      // generated
      for (const auto& particle : mcParticles) {

        if (!particle.isPhysicalPrimary())
          continue;

        if (particle.pdgCode() == kProtonBar) {
          registryMC.fill(HIST("antiproton_eta_pt_pythia"), particle.pt(), particle.eta());
        }

        if (particle.eta() < minEta || particle.eta() > maxEta)
          continue;

        switch (particle.pdgCode()) {
          case kProtonBar:
            registryMC.fill(HIST("antiproton_incl_gen"), particle.pt());
            break;
          case o2::constants::physics::Pdg::kDeuteron:
            registryMC.fill(HIST("deuteron_incl_gen"), particle.pt());
            break;
          case -o2::constants::physics::Pdg::kDeuteron:
            registryMC.fill(HIST("antideuteron_incl_gen"), particle.pt());
            break;
          case o2::constants::physics::Pdg::kHelium3:
            registryMC.fill(HIST("helium3_incl_gen"), particle.pt());
            break;
          case -o2::constants::physics::Pdg::kHelium3:
            registryMC.fill(HIST("antihelium3_incl_gen"), particle.pt());
            break;
        }
      }

      // ITS pid using cluster size
      o2::aod::ITSResponse itsResponse;

      // Reconstructed Tracks
      for (auto const& track : mcTracks) {

        // Track Selection
        if (!passedTrackSelection(track))
          continue;
        if (std::fabs(track.dcaXY()) > maxDcaxy)
          continue;
        if (std::fabs(track.dcaZ()) > maxDcaz)
          continue;

        // Get MC Particle
        if (!track.has_mcParticle())
          continue;
        const auto particle = track.mcParticle();

        // Variables
        double nsigmaTPCPr = track.tpcNSigmaPr();
        double nsigmaTOFPr = track.tofNSigmaPr();
        double nsigmaTPCDe = track.tpcNSigmaDe();
        double nsigmaTOFDe = track.tofNSigmaDe();
        double nsigmaTPCHe = track.tpcNSigmaHe();

        // particle identification using the ITS cluster size
        bool passedItsPidProt(false), passedItsPidDeut(false), passedItsPidHel(false);
        if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
          passedItsPidProt = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) < nSigmaItsMax) {
          passedItsPidDeut = true;
        }
        if (itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Helium3>(track) < nSigmaItsMax) {
          passedItsPidHel = true;
        }
        if (!applyItsPid) {
          passedItsPidProt = true;
          passedItsPidDeut = true;
          passedItsPidHel = true;
        }
        if (track.pt() > ptMaxItsPidProt)
          passedItsPidProt = true;
        if (track.pt() > ptMaxItsPidDeut)
          passedItsPidDeut = true;
        if ((2.0 * track.pt()) > ptMaxItsPidHel)
          passedItsPidHel = true;

        if (particle.pdgCode() == kProtonBar)
          registryMC.fill(HIST("antiproton_incl_all"), track.pt());

        if (!particle.isPhysicalPrimary())
          continue;

        if (particle.pdgCode() == kProtonBar)
          registryMC.fill(HIST("antiproton_incl_prim"), track.pt());

        // antiprotons
        if (particle.pdgCode() == kProtonBar && passedItsPidProt) {
          if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc) {
            registryMC.fill(HIST("antiproton_incl_rec_tpc"), track.pt());
            if (track.hasTOF() && nsigmaTOFPr > minNsigmaTof && nsigmaTOFPr < maxNsigmaTof)
              registryMC.fill(HIST("antiproton_incl_rec_tof"), track.pt());
          }
        }

        // antideuterons
        if (particle.pdgCode() == -o2::constants::physics::Pdg::kDeuteron && passedItsPidDeut) {
          if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc) {
            registryMC.fill(HIST("antideuteron_incl_rec_tpc"), track.pt());
            if (track.hasTOF() && nsigmaTOFDe > minNsigmaTof && nsigmaTOFDe < maxNsigmaTof)
              registryMC.fill(HIST("antideuteron_incl_rec_tof"), track.pt());
          }
        }

        // deuterons
        if (particle.pdgCode() == o2::constants::physics::Pdg::kDeuteron && passedItsPidDeut) {
          if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc) {
            registryMC.fill(HIST("deuteron_incl_rec_tpc"), track.pt());
            if (track.hasTOF() && nsigmaTOFDe > minNsigmaTof && nsigmaTOFDe < maxNsigmaTof)
              registryMC.fill(HIST("deuteron_incl_rec_tof"), track.pt());
          }
        }

        // antihelium3
        if (particle.pdgCode() == -o2::constants::physics::Pdg::kHelium3 && passedItsPidHel) {
          if (nsigmaTPCHe > minNsigmaTpc && nsigmaTPCHe < maxNsigmaTpc) {
            registryMC.fill(HIST("antihelium3_incl_rec_tpc"), 2.0 * track.pt());
          }
        }

        // helium3
        if (particle.pdgCode() == o2::constants::physics::Pdg::kHelium3 && passedItsPidHel) {
          if (nsigmaTPCHe > minNsigmaTpc && nsigmaTPCHe < maxNsigmaTpc) {
            registryMC.fill(HIST("helium3_incl_rec_tpc"), 2.0 * track.pt());
          }
        }
      }
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processEfficiency, "process efficiency", false);

  void processJetsMCgen(SimCollisions const& collisions, aod::McParticles const& mcParticles)
  {
    for (const auto& collision : collisions) {

      // event selection
      if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
        continue;

      std::vector<fastjet::PseudoJet> fjParticles;
      for (const auto& particle : mcParticles) {

        if (!particle.isPhysicalPrimary())
          continue;
        double minPtParticle = 0.1;
        if (particle.eta() < minEta || particle.eta() > maxEta || particle.pt() < minPtParticle)
          continue;

        double energy = std::sqrt(particle.p() * particle.p() + MassPionCharged * MassPionCharged);
        fastjet::PseudoJet fourMomentum(particle.px(), particle.py(), particle.pz(), energy);
        fourMomentum.set_user_index(particle.pdgCode());
        fjParticles.emplace_back(fourMomentum);
      }
      // reject empty events
      if (fjParticles.size() < 1)
        continue;

      // cluster particles using the anti-kt algorithm
      fastjet::JetDefinition jetDef(fastjet::antikt_algorithm, rJet);
      fastjet::AreaDefinition areaDef(fastjet::active_area, fastjet::GhostedAreaSpec(1.0)); // active_area_explicit_ghosts
      fastjet::ClusterSequenceArea cs(fjParticles, jetDef, areaDef);
      std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
      auto [rhoPerp, rhoMPerp] = backgroundSub.estimateRhoPerpCone(fjParticles, jets);

      // loop over jets
      for (const auto& jet : jets) {

        // jet must be fully contained in the acceptance
        if ((std::fabs(jet.eta()) + rJet) > (maxEta - deltaEtaEdge))
          continue;

        // jet pt must be larger than threshold
        auto jetForSub = jet;
        fastjet::PseudoJet jetMinusBkg = backgroundSub.doRhoAreaSub(jetForSub, rhoPerp, rhoMPerp);
        if (jetMinusBkg.pt() < minJetPt)
          continue;

        // jet properties and perpendicular cone
        std::vector<fastjet::PseudoJet> jetConstituents = jet.constituents();
        TVector3 jetAxis(jet.px(), jet.py(), jet.pz());
        double coneRadius = std::sqrt(jet.area() / PI);
        TVector3 ueAxis1(0, 0, 0);
        TVector3 ueAxis2(0, 0, 0);
        getPerpendicularAxis(jetAxis, ueAxis1, +1);
        getPerpendicularAxis(jetAxis, ueAxis2, -1);

        // loop over jet constituents
        for (const auto& particle : jetConstituents) {

          if (particle.user_index() != kProtonBar)
            continue;
          registryMC.fill(HIST("antiproton_jet_gen"), particle.pt());
          registryMC.fill(HIST("antiproton_eta_pt_jet"), particle.pt(), particle.eta());
        }

        // loop over underlying-event
        for (const auto& particle : mcParticles) {

          if (!particle.isPhysicalPrimary())
            continue;
          double minPtParticle = 0.1;
          if (particle.eta() < minEta || particle.eta() > maxEta || particle.pt() < minPtParticle)
            continue;

          double deltaEtaUe1 = particle.eta() - ueAxis1.Eta();
          double deltaPhiUe1 = getDeltaPhi(particle.phi(), ueAxis1.Phi());
          double deltaRUe1 = std::sqrt(deltaEtaUe1 * deltaEtaUe1 + deltaPhiUe1 * deltaPhiUe1);
          double deltaEtaUe2 = particle.eta() - ueAxis2.Eta();
          double deltaPhiUe2 = getDeltaPhi(particle.phi(), ueAxis2.Phi());
          double deltaRUe2 = std::sqrt(deltaEtaUe2 * deltaEtaUe2 + deltaPhiUe2 * deltaPhiUe2);
          if (deltaRUe1 > coneRadius && deltaRUe2 > coneRadius)
            continue;

          if (particle.pdgCode() != kProtonBar)
            continue;

          registryMC.fill(HIST("antiproton_ue_gen"), particle.pt());
          registryMC.fill(HIST("antiproton_eta_pt_ue"), particle.pt(), particle.eta());
        }
      }
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processJetsMCgen, "process jets mc gen", false);

  void processJetsMCrec(SimCollisions const& collisions, MCTracks const& mcTracks, McParticles const&)
  {
    for (const auto& collision : collisions) {

      // event selection
      if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
        return;

      // loop over reconstructed tracks
      int id(-1);
      std::vector<fastjet::PseudoJet> fjParticles;
      for (auto const& track : mcTracks) {
        id++;
        if (!passedTrackSelectionForJetReconstruction(track))
          continue;

        // 4-momentum representations of a particle
        fastjet::PseudoJet fourMomentum(track.px(), track.py(), track.pz(), track.energy(MassPionCharged));
        fourMomentum.set_user_index(id);
        fjParticles.emplace_back(fourMomentum);
      }
      // reject empty events
      if (fjParticles.size() < 1)
        continue;

      // cluster particles using the anti-kt algorithm
      fastjet::JetDefinition jetDef(fastjet::antikt_algorithm, rJet);
      fastjet::AreaDefinition areaDef(fastjet::active_area, fastjet::GhostedAreaSpec(1.0));
      fastjet::ClusterSequenceArea cs(fjParticles, jetDef, areaDef);
      std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
      auto [rhoPerp, rhoMPerp] = backgroundSub.estimateRhoPerpCone(fjParticles, jets);

      // loop over reconstructed jets
      for (const auto& jet : jets) {

        // get jet constituents
        std::vector<fastjet::PseudoJet> jetConstituents = jet.constituents();

        // calculate generated jet pt
        double jetPtGen(0);
        for (const auto& particle : jetConstituents) {

          // get corresponding track
          auto const& track = mcTracks.iteratorAt(particle.user_index());
          if (!track.has_mcParticle())
            continue;
          const auto mcparticle = track.mcParticle();
          jetPtGen = jetPtGen + mcparticle.pt();
        }

        // jet must be fully contained in the acceptance
        if ((std::fabs(jet.eta()) + rJet) > (maxEta - deltaEtaEdge))
          continue;

        // fill detector response matrix
        registryMC.fill(HIST("detectorResponseMatrix"), jet.pt(), jetPtGen - jet.pt()); // maybe it should be filled after bkg sub

        // jet pt must be larger than threshold
        auto jetForSub = jet;
        fastjet::PseudoJet jetMinusBkg = backgroundSub.doRhoAreaSub(jetForSub, rhoPerp, rhoMPerp);
        if (getCorrectedPt(jetMinusBkg.pt(), responseMatrix) < minJetPt)
          continue;

        // perpendicular cone
        double coneRadius = std::sqrt(jet.area() / PI);
        TVector3 jetAxis(jet.px(), jet.py(), jet.pz());
        TVector3 ueAxis1(0, 0, 0);
        TVector3 ueAxis2(0, 0, 0);
        getPerpendicularAxis(jetAxis, ueAxis1, +1);
        getPerpendicularAxis(jetAxis, ueAxis2, -1);

        o2::aod::ITSResponse itsResponse; // to be implemented

        // loop over jet constituents
        for (const auto& particle : jetConstituents) {

          // get corresponding track and apply track selection criteria
          auto const& track = mcTracks.iteratorAt(particle.user_index());
          if (!passedTrackSelection(track))
            continue;
          if (std::fabs(track.dcaXY()) > maxDcaxy || std::fabs(track.dcaZ()) > maxDcaz)
            continue;
          if (track.sign() > 0)
            continue;
          if (!track.has_mcParticle())
            continue;
          const auto mcparticle = track.mcParticle();
          if (mcparticle.pdgCode() != kProtonBar)
            continue;

          // variables
          double nsigmaTPCPr = track.tpcNSigmaPr();
          double nsigmaTOFPr = track.tofNSigmaPr();

          registryMC.fill(HIST("antiproton_jet_all"), track.pt());

          if (!mcparticle.isPhysicalPrimary())
            continue;

          registryMC.fill(HIST("antiproton_jet_prim"), track.pt());

          // particle identification using the ITS cluster size
          bool passedItsPidProt(false);
          if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
            passedItsPidProt = true;
          }
          if (!applyItsPid)
            passedItsPidProt = true;
          if (track.pt() > ptMaxItsPidProt)
            passedItsPidProt = true;

          if (passedItsPidProt) {
            registryMC.fill(HIST("antiproton_jet_rec_tpc"), track.pt(), nsigmaTPCPr);
            if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc && track.hasTOF())
              registryMC.fill(HIST("antiproton_jet_rec_tof"), track.pt(), nsigmaTOFPr);
          }
        }

        // underlying event
        for (auto const& track : mcTracks) {

          // get corresponding track and apply track selection criteria
          if (!passedTrackSelection(track))
            continue;
          if (std::fabs(track.dcaXY()) > maxDcaxy || std::fabs(track.dcaZ()) > maxDcaz)
            continue;
          if (track.sign() > 0)
            continue;

          if (!track.has_mcParticle())
            continue;
          const auto mcparticle = track.mcParticle();
          if (mcparticle.pdgCode() != kProtonBar)
            continue;

          double deltaEtaUe1 = track.eta() - ueAxis1.Eta();
          double deltaPhiUe1 = getDeltaPhi(track.phi(), ueAxis1.Phi());
          double deltaRUe1 = std::sqrt(deltaEtaUe1 * deltaEtaUe1 + deltaPhiUe1 * deltaPhiUe1);
          double deltaEtaUe2 = track.eta() - ueAxis2.Eta();
          double deltaPhiUe2 = getDeltaPhi(track.phi(), ueAxis2.Phi());
          double deltaRUe2 = std::sqrt(deltaEtaUe2 * deltaEtaUe2 + deltaPhiUe2 * deltaPhiUe2);
          if (deltaRUe1 > coneRadius && deltaRUe2 > coneRadius)
            continue;

          // variables
          double nsigmaTPCPr = track.tpcNSigmaPr();
          double nsigmaTOFPr = track.tofNSigmaPr();

          registryMC.fill(HIST("antiproton_ue_all"), track.pt());
          if (!mcparticle.isPhysicalPrimary())
            continue;
          registryMC.fill(HIST("antiproton_ue_prim"), track.pt());

          // particle identification using the ITS cluster size
          bool passedItsPidProt(false);
          if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
            passedItsPidProt = true;
          }
          if (!applyItsPid)
            passedItsPidProt = true;
          if (track.pt() > ptMaxItsPidProt)
            passedItsPidProt = true;

          if (passedItsPidProt) {
            if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc) {
              registryMC.fill(HIST("antiproton_ue_rec_tpc"), track.pt());
              if (track.hasTOF() && nsigmaTOFPr > minNsigmaTof && nsigmaTOFPr < maxNsigmaTof)
                registryMC.fill(HIST("antiproton_ue_rec_tof"), track.pt());
            }
          }
        }
      }
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processJetsMCrec, "process jets MC rec", false);

  // Process Systematics
  void processSystematicsData(SelectedCollisions::iterator const& collision, FullNucleiTracks const& tracks)
  {
    if (rejectEvents) {
      // event counter: before event rejection
      registryData.fill(HIST("number_of_rejected_events_syst"), 0.5);

      if (shouldRejectEvent())
        return;

      // event counter: after event rejection
      registryData.fill(HIST("number_of_rejected_events_syst"), 1.5);
    }

    const int nSystematics = 10;
    int itsNclustersSyst[nSystematics] = {5, 6, 5, 4, 5, 3, 5, 6, 3, 4};
    float tpcNcrossedRowsSyst[nSystematics] = {100, 85, 80, 110, 95, 90, 105, 95, 100, 105};
    float dcaxySyst[nSystematics] = {0.05, 0.07, 0.10, 0.03, 0.06, 0.15, 0.08, 0.04, 0.09, 0.10};
    float dcazSyst[nSystematics] = {0.1, 0.15, 0.3, 0.075, 0.12, 0.18, 0.2, 0.1, 0.15, 0.2};

    // event selection
    if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
      return;

    // loop over reconstructed tracks
    int id(-1);
    std::vector<fastjet::PseudoJet> fjParticles;
    for (auto const& track : tracks) {
      id++;
      if (!passedTrackSelectionForJetReconstruction(track))
        continue;

      // 4-momentum representation of a particle
      fastjet::PseudoJet fourMomentum(track.px(), track.py(), track.pz(), track.energy(MassPionCharged));
      fourMomentum.set_user_index(id);
      fjParticles.emplace_back(fourMomentum);
    }

    // reject empty events
    if (fjParticles.size() < 1)
      return;

    // cluster particles using the anti-kt algorithm
    fastjet::JetDefinition jetDef(fastjet::antikt_algorithm, rJet);
    fastjet::AreaDefinition areaDef(fastjet::active_area, fastjet::GhostedAreaSpec(1.0)); // active_area_explicit_ghosts
    fastjet::ClusterSequenceArea cs(fjParticles, jetDef, areaDef);
    std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
    auto [rhoPerp, rhoMPerp] = backgroundSub.estimateRhoPerpCone(fjParticles, jets);

    // loop over reconstructed jets
    for (const auto& jet : jets) {

      // jet must be fully contained in the acceptance
      if ((std::fabs(jet.eta()) + rJet) > (maxEta - deltaEtaEdge))
        continue;

      // jet pt must be larger than threshold
      auto jetForSub = jet;
      fastjet::PseudoJet jetMinusBkg = backgroundSub.doRhoAreaSub(jetForSub, rhoPerp, rhoMPerp);
      if (getCorrectedPt(jetMinusBkg.pt(), responseMatrix) < minJetPt)
        continue;

      // get jet constituents
      std::vector<fastjet::PseudoJet> jetConstituents = jet.constituents();
      o2::aod::ITSResponse itsResponse;

      // loop over jet constituents
      for (const auto& particle : jetConstituents) {
        for (int i = 0; i < nSystematics; i++) {
          // get corresponding track and apply track selection criteria
          auto const& track = tracks.iteratorAt(particle.user_index());

          // variables
          double nsigmaTPCPr = track.tpcNSigmaPr();
          double nsigmaTOFPr = track.tofNSigmaPr();
          double nsigmaTPCDe = track.tpcNSigmaDe();
          double nsigmaTOFDe = track.tofNSigmaDe();
          double pt = track.pt();
          double dcaxy = track.dcaXY();
          double dcaz = track.dcaZ();

          if (requirePvContributor && !(track.isPVContributor()))
            continue;
          if (!track.hasITS())
            continue;
          if (track.itsNCls() < itsNclustersSyst[i])
            continue;
          if (!track.hasTPC())
            continue;
          if (track.tpcNClsCrossedRows() < tpcNcrossedRowsSyst[i])
            continue;
          if ((static_cast<double>(track.tpcNClsCrossedRows()) / static_cast<double>(track.tpcNClsFindable())) < minTpcNcrossedRowsOverFindable)
            continue;
          if (track.tpcChi2NCl() > maxChiSquareTpc)
            continue;
          if (track.itsChi2NCl() > maxChiSquareIts)
            continue;
          if (track.eta() < minEta || track.eta() > maxEta)
            continue;
          if (track.pt() < minPt)
            continue;
          if (std::fabs(dcaxy) > dcaxySyst[i])
            continue;
          if (std::fabs(dcaz) > dcazSyst[i])
            continue;

          bool passedItsPidProt(false), passedItsPidDeut(false);
          if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
            passedItsPidProt = true;
          }
          if (itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) < nSigmaItsMax) {
            passedItsPidDeut = true;
          }
          if (!applyItsPid) {
            passedItsPidProt = true;
            passedItsPidDeut = true;
          }
          if (pt > ptMaxItsPidProt)
            passedItsPidProt = true;
          if (pt > ptMaxItsPidDeut)
            passedItsPidDeut = true;

          // antimatter
          if (track.sign() < 0) {
            if (passedItsPidProt) {
              registryData.fill(HIST("antiproton_tpc_syst"), pt, nsigmaTPCPr, i);
              if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc && track.hasTOF())
                registryData.fill(HIST("antiproton_tof_syst"), pt, nsigmaTOFPr, i);
            }
            if (passedItsPidDeut) {
              registryData.fill(HIST("antideuteron_tpc_syst"), pt, nsigmaTPCDe, i);
              if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc && track.hasTOF())
                registryData.fill(HIST("antideuteron_tof_syst"), pt, nsigmaTOFDe, i);
            }
          }
        }
      }
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processSystematicsData, "Process Systematics", false);

  void processSystematicsEfficiency(SimCollisions const& collisions, MCTracks const& mcTracks, aod::McParticles const& mcParticles)
  {
    const int nSystematics = 10;
    int itsNclustersSyst[nSystematics] = {5, 6, 5, 4, 5, 3, 5, 6, 3, 4};
    float tpcNcrossedRowsSyst[nSystematics] = {100, 85, 80, 110, 95, 90, 105, 95, 100, 105};
    float dcaxySyst[nSystematics] = {0.05, 0.07, 0.10, 0.03, 0.06, 0.15, 0.08, 0.04, 0.09, 0.10};
    float dcazSyst[nSystematics] = {0.1, 0.15, 0.3, 0.075, 0.12, 0.18, 0.2, 0.1, 0.15, 0.2};

    for (const auto& collision : collisions) {

      if (!collision.sel8() || std::fabs(collision.posZ()) > zVtx)
        continue;

      // generated
      for (const auto& particle : mcParticles) {

        if (!particle.isPhysicalPrimary())
          continue;

        if (particle.eta() < minEta || particle.eta() > maxEta)
          continue;

        switch (particle.pdgCode()) {
          case kProtonBar:
            registryMC.fill(HIST("antiproton_incl_gen_syst"), particle.pt());
            break;
          case -o2::constants::physics::Pdg::kDeuteron:
            registryMC.fill(HIST("antideuteron_incl_gen_syst"), particle.pt());
            break;
        }
      }

      // ITS pid using cluster size
      o2::aod::ITSResponse itsResponse;

      // Reconstructed Tracks
      for (auto const& track : mcTracks) {

        // Get MC Particle
        if (!track.has_mcParticle())
          continue;
        const auto particle = track.mcParticle();

        // Variables
        double nsigmaTPCPr = track.tpcNSigmaPr();
        double nsigmaTOFPr = track.tofNSigmaPr();
        double nsigmaTPCDe = track.tpcNSigmaDe();
        double nsigmaTOFDe = track.tofNSigmaDe();
        double dcaxy = track.dcaXY();
        double dcaz = track.dcaZ();

        for (int i = 0; i < nSystematics; i++) {

          // Track Selection
          if (requirePvContributor && !(track.isPVContributor()))
            continue;
          if (!track.hasITS())
            continue;
          if (track.itsNCls() < itsNclustersSyst[i])
            continue;
          if (!track.hasTPC())
            continue;
          if (track.tpcNClsCrossedRows() < tpcNcrossedRowsSyst[i])
            continue;
          if ((static_cast<double>(track.tpcNClsCrossedRows()) / static_cast<double>(track.tpcNClsFindable())) < minTpcNcrossedRowsOverFindable)
            continue;
          if (track.tpcChi2NCl() > maxChiSquareTpc)
            continue;
          if (track.itsChi2NCl() > maxChiSquareIts)
            continue;
          if (track.eta() < minEta || track.eta() > maxEta)
            continue;
          if (track.pt() < minPt)
            continue;
          if (std::fabs(dcaxy) > dcaxySyst[i])
            continue;
          if (std::fabs(dcaz) > dcazSyst[i])
            continue;

          // particle identification using the ITS cluster size
          bool passedItsPidProt(false), passedItsPidDeut(false);
          if (itsResponse.nSigmaITS<o2::track::PID::Proton>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Proton>(track) < nSigmaItsMax) {
            passedItsPidProt = true;
          }
          if (itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) > nSigmaItsMin && itsResponse.nSigmaITS<o2::track::PID::Deuteron>(track) < nSigmaItsMax) {
            passedItsPidDeut = true;
          }
          if (!applyItsPid) {
            passedItsPidProt = true;
            passedItsPidDeut = true;
          }
          if (track.pt() > ptMaxItsPidProt)
            passedItsPidProt = true;
          if (track.pt() > ptMaxItsPidDeut)
            passedItsPidDeut = true;
          if (!particle.isPhysicalPrimary())
            continue;

          if (particle.pdgCode() == kProtonBar)
            registryMC.fill(HIST("antiproton_incl_prim_syst"), track.pt(), i);

          // antiprotons
          if (particle.pdgCode() == kProtonBar && passedItsPidProt) {
            if (nsigmaTPCPr > minNsigmaTpc && nsigmaTPCPr < maxNsigmaTpc) {
              registryMC.fill(HIST("antiproton_incl_rec_tpc_syst"), track.pt(), i);
              if (track.hasTOF() && nsigmaTOFPr > minNsigmaTof && nsigmaTOFPr < maxNsigmaTof)
                registryMC.fill(HIST("antiproton_incl_rec_tof_syst"), track.pt(), i);
            }
          }

          // antideuterons
          if (particle.pdgCode() == -o2::constants::physics::Pdg::kDeuteron && passedItsPidDeut) {
            if (nsigmaTPCDe > minNsigmaTpc && nsigmaTPCDe < maxNsigmaTpc) {
              registryMC.fill(HIST("antideuteron_incl_rec_tpc_syst"), track.pt(), i);
              if (track.hasTOF() && nsigmaTOFDe > minNsigmaTof && nsigmaTOFDe < maxNsigmaTof)
                registryMC.fill(HIST("antideuteron_incl_rec_tof_syst"), track.pt(), i);
            }
          }
        }
      }
    }
  }
  PROCESS_SWITCH(AntinucleiInJets, processSystematicsEfficiency, "process efficiency for systematics", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{adaptAnalysisTask<AntinucleiInJets>(cfgc)};
}
