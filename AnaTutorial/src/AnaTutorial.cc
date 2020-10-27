#include "AnaTutorial.h"

/// Cluster/Calorimeter includes
#include <calobase/RawCluster.h>
#include <calobase/RawClusterContainer.h>
#include <calobase/RawClusterUtility.h>
#include <calobase/RawTower.h>
#include <calobase/RawTowerContainer.h>
#include <calobase/RawTowerGeom.h>
#include <calobase/RawTowerGeomContainer.h>
#include <calotrigger/CaloTriggerInfo.h>

/// Jet includes
#include <g4jets/Jet.h>
#include <g4jets/JetMap.h>

/// Tracking includes
#include <g4vertex/GlobalVertex.h>
#include <g4vertex/GlobalVertexMap.h>
#include <trackbase_historic/SvtxTrack.h>
#include <trackbase_historic/SvtxTrackMap.h>
#include <trackbase_historic/SvtxVertex.h>
#include <trackbase_historic/SvtxVertexMap.h>

/// Truth evaluation includes
#include <g4eval/JetEvalStack.h>
#include <g4eval/SvtxEvalStack.h>

/// HEPMC truth includes
#include <HepMC/GenEvent.h>
#include <HepMC/GenVertex.h>
#include <phhepmc/PHHepMCGenEvent.h>
#include <phhepmc/PHHepMCGenEventMap.h>

/// Fun4All includes
#include <fun4all/Fun4AllHistoManager.h>
#include <fun4all/Fun4AllReturnCodes.h>
#include <g4main/PHG4Hit.h>
#include <g4main/PHG4Particle.h>
#include <g4main/PHG4TruthInfoContainer.h>
#include <phool/PHCompositeNode.h>
#include <phool/getClass.h>

/// ROOT includes
#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TMath.h>
#include <TNtuple.h>
#include <TTree.h>

/// C++ includes
#include <cassert>
#include <sstream>
#include <string>

using namespace std;

/**
 * This class demonstrates the construction and use of an analysis module 
 * within the sPHENIX Fun4All framework. It is intended to show how to 
 * obtain physics objects from the analysis tree, and then write them out
 * to a ROOT tree and file for further offline analysis.  
 */

/**
 * Constructor of module
 */
AnaTutorial::AnaTutorial(const std::string &name, const std::string &filename)
  : SubsysReco(name)
  , m_outfilename(filename)
  , m_hm(nullptr)
  , m_minjetpt(5.0)
  , m_mincluspt(0.25)
  , m_analyzeTracks(true)
  , m_analyzeClusters(true)
  , m_analyzeJets(true)
  , m_analyzeTruth(false)
{
  /// Initialize variables and trees so we don't accidentally access 
  /// memory that was never allocated
  initializeVariables();
  initializeTrees();
}

/**
 * Destructor of module
 */
AnaTutorial::~AnaTutorial()
{
  delete m_hm;
  delete m_hepmctree;
  delete m_truthjettree;
  delete m_recojettree;
  delete m_tracktree;
}

/**
 * Initialize the module and prepare looping over events
 */
int AnaTutorial::Init(PHCompositeNode *topNode)
{
  if (Verbosity() > 5)
  {
    cout << "Beginning Init in AnaTutorial" << endl;
  }
 
  m_hm = new Fun4AllHistoManager(Name());
  // create and register your histos (all types) here
  // TH1 *h1 = new TH1F("h1",....)
  // hm->registerHisto(h1);
  m_outfile = new TFile(m_outfilename.c_str(), "RECREATE");

  m_phi_h = new TH1D("phi_h", ";Counts;#phi [rad]", 50, -6, 6);
  m_hm->registerHisto(m_phi_h);
  m_eta_phi_h = new TH2F("phi_eta_h", ";#eta;#phi [rad]", 10, -1, 1, 50, -6, 6);
  m_hm->registerHisto(m_eta_phi_h);

  return 0;
}

/**
 * Main workhorse function where each event is looped over and 
 * data from each event is collected from the node tree for analysis
 */
int AnaTutorial::process_event(PHCompositeNode *topNode)
{
  if (Verbosity() > 5)
  {
    cout << "Beginning process_event in AnaTutorial" << endl;
  }
  /// Get the truth information
  if (m_analyzeTruth)
  {
    getHEPMCTruth(topNode);
    getPHG4Truth(topNode);
  }

  /// Get the tracks
  if (m_analyzeTracks)
  {
    getTracks(topNode);
  }
  /// Get the truth and reconstructed jets
  if (m_analyzeJets)
  {
    getTruthJets(topNode);
    getReconstructedJets(topNode);
  }

  /// Get calorimeter information
  if (m_analyzeClusters)
  {
    getEMCalClusters(topNode);
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

/**
 * End the module and finish any data collection. Clean up any remaining
 * loose ends
 */
int AnaTutorial::End(PHCompositeNode *topNode)
{
  if (Verbosity() > 1)
  {
    cout << "Ending AnaTutorial analysis package" << endl;
  }
  /// Change to the outfile
  m_outfile->cd();

  /// If we analyzed the tracks, write the tree out
  if (m_analyzeTracks)
    m_tracktree->Write();

  /// If we analyzed the jets, write them out
  if (m_analyzeJets)
  {
    m_truthjettree->Write();
    m_recojettree->Write();
  }

  /// If we analyzed the truth particles, write them out
  if (m_analyzeTruth)
  {
    m_hepmctree->Write();
    m_truthtree->Write();
  }

  /// If we analyzed the clusters, write them out
  if (m_analyzeClusters)
  {
    m_clustertree->Write();
  }

  /// Write out any other histograms
  m_phi_h->Write();
  m_eta_phi_h->Write();

  /// Write and close the outfile
  m_outfile->Write();
  m_outfile->Close();

  delete m_outfile;

  /// Let the histogram manager deal with dumping the histogram memory
  m_hm->dumpHistos(m_outfilename, "UPDATE");

  if (Verbosity() > 1)
  {
    cout << "Finished AnaTutorial analysis package" << endl;
  }

  return 0;
}

/**
 * This method gets all of the HEPMC truth particles from the node tree
 * and stores them in a ROOT TTree. The HEPMC truth particles are what, 
 * for example, directly comes out of PYTHIA and thus gives you all of
 * the associated parton information
 */
void AnaTutorial::getHEPMCTruth(PHCompositeNode *topNode)
{
  /// Get the node from the node tree
  PHHepMCGenEventMap *hepmceventmap = findNode::getClass<PHHepMCGenEventMap>(topNode, "PHHepMCGenEventMap");

  /// If the node was not properly put on the tree, return
  if (!hepmceventmap)
  {
    cout << PHWHERE
         << "HEPMC event map node is missing, can't collected HEPMC truth particles"
         << endl;
    return;
  }

  /// Could have some print statements for debugging with verbosity
  if (Verbosity() > 1)
  {
    cout << "Getting HEPMC truth particles " << endl;
  }

  /// You can iterate over the number of events in a hepmc event
  /// for pile up events where you have multiple hard scatterings per bunch crossing
  for (PHHepMCGenEventMap::ConstIter eventIter = hepmceventmap->begin();
       eventIter != hepmceventmap->end();
       ++eventIter)
  {
    /// Get the event
    PHHepMCGenEvent *hepmcevent = eventIter->second;

    if (hepmcevent)
    {
      /// Get the event characteristics, inherited from HepMC classes
      HepMC::GenEvent *truthevent = hepmcevent->getEvent();
      if (!truthevent)
      {
        cout << PHWHERE
             << "no evt pointer under phhepmvgeneventmap found "
             << endl;
        return;
      }

      /// Get the parton info
      HepMC::PdfInfo *pdfinfo = truthevent->pdf_info();

      /// Get the parton info as determined from HEPMC
      m_partid1 = pdfinfo->id1();
      m_partid2 = pdfinfo->id2();
      m_x1 = pdfinfo->x1();
      m_x2 = pdfinfo->x2();

      /// Are there multiple partonic intercations in a p+p event
      m_mpi = truthevent->mpi();

      /// Get the PYTHIA signal process id identifying the 2-to-2 hard process
      m_process_id = truthevent->signal_process_id();

      if (Verbosity() > 2)
      {
        cout << " Iterating over an event" << endl;
      }
      /// Loop over all the truth particles and get their information
      for (HepMC::GenEvent::particle_const_iterator iter = truthevent->particles_begin();
           iter != truthevent->particles_end();
           ++iter)
      {
        /// Get each pythia particle characteristics
        m_truthenergy = (*iter)->momentum().e();
        m_truthpid = (*iter)->pdg_id();

        m_trutheta = (*iter)->momentum().pseudoRapidity();
        m_truthphi = (*iter)->momentum().phi();
        m_truthpx = (*iter)->momentum().px();
        m_truthpy = (*iter)->momentum().py();
        m_truthpz = (*iter)->momentum().pz();
        m_truthpt = sqrt(m_truthpx * m_truthpx + m_truthpy * m_truthpy);

        /// Fill the truth tree
        m_hepmctree->Fill();
        m_numparticlesinevent++;
      }
    }
  }
}

/**
 * This function collects the truth PHG4 stable particles that
 * are produced from PYTHIA, or some other event generator. These
 * are the stable particles, e.g. there are not any (for example)
 * partons here.
 */
void AnaTutorial::getPHG4Truth(PHCompositeNode *topNode)
{
  /// G4 truth particle node
  PHG4TruthInfoContainer *truthinfo = findNode::getClass<PHG4TruthInfoContainer>(topNode, "G4TruthInfo");

  if (!truthinfo)
  {
    cout << PHWHERE
         << "PHG4TruthInfoContainer node is missing, can't collect G4 truth particles"
         << endl;
    return;
  }

  /// Get the primary particle range
  PHG4TruthInfoContainer::Range range = truthinfo->GetPrimaryParticleRange();

  /// Loop over the G4 truth (stable) particles
  for (PHG4TruthInfoContainer::ConstIterator iter = range.first;
       iter != range.second;
       ++iter)
  {
    /// Get this truth particle
    const PHG4Particle *truth = iter->second;

    /// Get this particles momentum, etc.
    m_truthpx = truth->get_px();
    m_truthpy = truth->get_py();
    m_truthpz = truth->get_pz();
    m_truthp = sqrt(m_truthpx * m_truthpx + m_truthpy * m_truthpy + m_truthpz * m_truthpz);
    m_truthenergy = truth->get_e();

    m_truthpt = sqrt(m_truthpx * m_truthpx + m_truthpy * m_truthpy);

    m_truthphi = atan(m_truthpy / m_truthpx);

    m_trutheta = atanh(m_truthpz / m_truthenergy);
    /// Check for nans
    if (m_trutheta != m_trutheta)
      m_trutheta = -99;
    m_truthpid = truth->get_pid();

    /// Fill the g4 truth tree
    m_truthtree->Fill();
  }
}

/**
 * This method gets the tracks as reconstructed from the tracker. It also
 * compares the reconstructed track to its truth track counterpart as determined
 * by the 
 */
void AnaTutorial::getTracks(PHCompositeNode *topNode)
{
  /// SVTX tracks node
  SvtxTrackMap *trackmap = findNode::getClass<SvtxTrackMap>(topNode, "SvtxTrackMap");

  if (!trackmap)
  {
    cout << PHWHERE
         << "SvtxTrackMap node is missing, can't collect tracks"
         << endl;
    return;
  }

  /// EvalStack for truth track matching
  SvtxEvalStack *svtxevalstack = new SvtxEvalStack(topNode);
  svtxevalstack->next_event(topNode);

  /// Get the track evaluator
  SvtxTrackEval *trackeval = svtxevalstack->get_track_eval();

  /// Get the range for primary tracks
  PHG4TruthInfoContainer *truthinfo = findNode::getClass<PHG4TruthInfoContainer>(topNode, "G4TruthInfo");

  if (Verbosity() > 1)
  {
    cout << "Get the SVTX tracks" << endl;
  }
  for (SvtxTrackMap::Iter iter = trackmap->begin();
       iter != trackmap->end();
       ++iter)
  {
    SvtxTrack *track = iter->second;

    /// Get the reconstructed track info
    m_tr_px = track->get_px();
    m_tr_py = track->get_py();
    m_tr_pz = track->get_pz();
    m_tr_p = sqrt(m_tr_px * m_tr_px + m_tr_py * m_tr_py + m_tr_pz * m_tr_pz);

    m_tr_pt = sqrt(m_tr_px * m_tr_px + m_tr_py * m_tr_py);

    // Make some cuts on the track to clean up sample
    if (m_tr_pt < 0.5)
      continue;

    m_tr_phi = track->get_phi();
    m_tr_eta = track->get_eta();

    m_charge = track->get_charge();
    m_chisq = track->get_chisq();
    m_ndf = track->get_ndf();
    m_dca = track->get_dca();
    m_tr_x = track->get_x();
    m_tr_y = track->get_y();
    m_tr_z = track->get_z();

    /// Get truth track info that matches this reconstructed track
    PHG4Particle *truthtrack = trackeval->max_truth_particle_by_nclusters(track);
    m_truth_is_primary = truthinfo->is_primary(truthtrack);

    m_truthtrackpx = truthtrack->get_px();
    m_truthtrackpy = truthtrack->get_py();
    m_truthtrackpz = truthtrack->get_pz();
    m_truthtrackp = sqrt(m_truthtrackpx * m_truthtrackpx + m_truthtrackpy * m_truthtrackpy + m_truthtrackpz * m_truthtrackpz);

    m_truthtracke = truthtrack->get_e();

    m_truthtrackpt = sqrt(m_truthtrackpx * m_truthtrackpx + m_truthtrackpy * m_truthtrackpy);
    m_truthtrackphi = atan(m_truthtrackpy / m_truthtrackpx);
    m_truthtracketa = atanh(m_truthtrackpz / m_truthtrackp);
    m_truthtrackpid = truthtrack->get_pid();

    m_tracktree->Fill();
  }
}

/**
 * Method that gets the truth jets and stores them in a tree
 */
void AnaTutorial::getTruthJets(PHCompositeNode *topNode)
{
  if (Verbosity() > 1)
  {
    cout << "get the truth jets" << endl;
  }

  /// Get the truth jet node
  JetMap *truth_jets = findNode::getClass<JetMap>(topNode, "AntiKt_Truth_r04");

  if (!truth_jets)
  {
    cout << PHWHERE
         << "Truth jet node is missing, can't collect truth jets"
         << endl;
    return;
  }

  /// Iterate over the truth jets
  for (JetMap::Iter iter = truth_jets->begin();
       iter != truth_jets->end();
       ++iter)
  {
    const Jet *jet = iter->second;

    m_truthjetpt = jet->get_pt();

    /// Only collect truthjets above the _minjetpt cut
    if (m_truthjetpt < m_minjetpt)
      continue;

    m_truthjeteta = jet->get_eta();
    m_truthjetpx = jet->get_px();
    m_truthjetpy = jet->get_py();
    m_truthjetpz = jet->get_pz();
    m_truthjetphi = jet->get_phi();
    m_truthjetp = jet->get_p();
    m_truthjetenergy = jet->get_e();

    /// Fill the truthjet tree
    m_truthjettree->Fill();
  }
}

/**
 * Get the reconstructed jets and store them in a tree
 */
void AnaTutorial::getReconstructedJets(PHCompositeNode *topNode)
{
  /// Get the reconstructed tower jets
  JetMap *reco_jets = findNode::getClass<JetMap>(topNode, "AntiKt_Tower_r04");
  /// Get the truth jets
  JetMap *truth_jets = findNode::getClass<JetMap>(topNode, "AntiKt_Truth_r04");

  if (!reco_jets)
  {
    cout << PHWHERE
         << "Reconstructed jet node is missing, can't collect reconstructed jets"
         << endl;
    return;
  }

  if (Verbosity() > 1)
  {
    cout << "Get all Reco Jets" << endl;
  }

  /// Iterate over the reconstructed jets
  for (JetMap::Iter recoIter = reco_jets->begin();
       recoIter != reco_jets->end();
       ++recoIter)
  {
    const Jet *recoJet = recoIter->second;
    m_recojetpt = recoJet->get_pt();
    if (m_recojetpt < m_minjetpt)
      continue;

    m_recojeteta = recoJet->get_eta();

    // Get reco jet characteristics
    m_recojetid = recoJet->get_id();
    m_recojetpx = recoJet->get_px();
    m_recojetpy = recoJet->get_py();
    m_recojetpz = recoJet->get_pz();
    m_recojetphi = recoJet->get_phi();
    m_recojetp = recoJet->get_p();
    m_recojetenergy = recoJet->get_e();

    if (Verbosity() > 1)
    {
      cout << "matching by distance jet" << endl;
    }

    /// Set the matched truth jet characteristics to 0
    m_truthjetid = 0;
    m_truthjetp = 0;
    m_truthjetphi = 0;
    m_truthjeteta = 0;
    m_truthjetpt = 0;
    m_truthjetenergy = 0;
    m_truthjetpx = 0;
    m_truthjetpy = 0;
    m_truthjetpz = 0;

    /// Check to make sure the truth jet node is available
    if (truth_jets)
    {
      /// Match the reconstructed jet to the closest truth jet in delta R space
      /// Iterate over the truth jets
      float closestjet = 9999;
      for (JetMap::Iter truthIter = truth_jets->begin();
           truthIter != truth_jets->end();
           ++truthIter)
      {
        const Jet *truthJet = truthIter->second;

        float thisjetpt = truthJet->get_pt();
        if (thisjetpt < m_minjetpt)
          continue;

        float thisjeteta = truthJet->get_eta();
        float thisjetphi = truthJet->get_phi();

        float dphi = m_recojetphi - thisjetphi;
        if (dphi > 3. * TMath::Pi() / 2.)
          dphi -= TMath::Pi() * 2.;
        if (dphi < -1. * TMath::Pi() / 2.)
          dphi += TMath::Pi() * 2.;

        float deta = m_recojeteta - thisjeteta;
        /// Determine the distance in eta phi space between the reconstructed
        /// and truth jets
        m_dR = sqrt(pow(dphi, 2.) + pow(deta, 2.));

        /// If this truth jet is closer than the previous truth jet, it is
        /// closer and thus should be considered the truth jet
        if (m_dR < reco_jets->get_par() && m_dR < closestjet)
        {
          m_truthjetid = -9999;
          m_truthjetp = truthJet->get_p();
          m_truthjetphi = truthJet->get_phi();
          m_truthjeteta = truthJet->get_eta();
          m_truthjetpt = truthJet->get_pt();
          m_truthjetenergy = truthJet->get_e();
          m_truthjetpx = truthJet->get_px();
          m_truthjetpy = truthJet->get_py();
          m_truthjetpz = truthJet->get_pz();
          closestjet = m_dR;
        }
      }
    }
    m_recojettree->Fill();
  }
}

/**
 * This method gets clusters from the EMCal and stores them in a tree. It
 * also demonstrates how to get trigger emulator information. Clusters from
 * other containers can be obtained in a similar way (e.g. clusters from
 * the IHCal, etc.)
 */
void AnaTutorial::getEMCalClusters(PHCompositeNode *topNode)
{
  /// Get the raw cluster container
  /// Note: other cluster containers exist as well. Check out the node tree when
  /// you run a simulation
  RawClusterContainer *clusters = findNode::getClass<RawClusterContainer>(topNode, "CLUSTER_CEMC");

  if (!clusters)
  {
    cout << PHWHERE
         << "EMCal cluster node is missing, can't collect EMCal clusters"
         << endl;
    return;
  }

  /// Get the global vertex to determine the appropriate pseudorapidity of the clusters
  GlobalVertexMap *vertexmap = findNode::getClass<GlobalVertexMap>(topNode, "GlobalVertexMap");
  if (!vertexmap)
  {
    cout << "AnaTutorial::getEmcalClusters - Fatal Error - GlobalVertexMap node is missing. Please turn on the do_global flag in the main macro in order to reconstruct the global vertex." << endl;
    assert(vertexmap);  // force quit

    return;
  }

  if (vertexmap->empty())
  {
    cout << "AnaTutorial::getEmcalClusters - Fatal Error - GlobalVertexMap node is empty. Please turn on the do_global flag in the main macro in order to reconstruct the global vertex." << endl;
    return;
  }

  GlobalVertex *vtx = vertexmap->begin()->second;
  if (vtx == nullptr)
    return;

  /// Trigger emulator
  CaloTriggerInfo *trigger = findNode::getClass<CaloTriggerInfo>(topNode, "CaloTriggerInfo");
  /// Can obtain some trigger information if desired
  m_E_4x4 = trigger->get_best_EMCal_4x4_E();

  RawClusterContainer::ConstRange begin_end = clusters->getClusters();
  RawClusterContainer::ConstIterator clusIter;

  /// Loop over the EMCal clusters
  for (clusIter = begin_end.first;
       clusIter != begin_end.second;
       ++clusIter)
  {
    /// Get this cluster
    const RawCluster *cluster = clusIter->second;

    /// Get cluster characteristics
    /// This helper class determines the photon characteristics
    /// depending on the vertex position
    /// This is important for e.g. eta determination and E_T determination
    CLHEP::Hep3Vector vertex(vtx->get_x(), vtx->get_y(), vtx->get_z());
    CLHEP::Hep3Vector E_vec_cluster = RawClusterUtility::GetECoreVec(*cluster, vertex);
    m_clusenergy = E_vec_cluster.mag();
    m_cluseta = E_vec_cluster.pseudoRapidity();
    m_clustheta = E_vec_cluster.getTheta();
    m_cluspt = E_vec_cluster.perp();
    m_clusphi = E_vec_cluster.getPhi();

    if (m_cluspt < m_mincluspt)
      continue;

    m_cluspx = m_cluspt * cos(m_clusphi);
    m_cluspy = m_cluspt * sin(m_clusphi);
    m_cluspz = sqrt(m_clusenergy * m_clusenergy - m_cluspx * m_cluspx - m_cluspy * m_cluspy);

    //fill the cluster tree with all emcal clusters
    m_clustertree->Fill();
  }
}

/**
 * This function puts all of the tree branch assignments in one place so as to not
 * clutter up the AnaTutorial::Init function.
 */
void AnaTutorial::initializeTrees()
{
  m_recojettree = new TTree("jettree", "A tree with reconstructed jets");
  m_recojettree->Branch("m_recojetpt", &m_recojetpt, "m_recojetpt/D");
  m_recojettree->Branch("m_recojetid", &m_recojetid, "m_recojetid/I");
  m_recojettree->Branch("m_recojetpx", &m_recojetpx, "m_recojetpx/D");
  m_recojettree->Branch("m_recojetpy", &m_recojetpy, "m_recojetpy/D");
  m_recojettree->Branch("m_recojetpz", &m_recojetpz, "m_recojetpz/D");
  m_recojettree->Branch("m_recojetphi", &m_recojetphi, "m_recojetphi/D");
  m_recojettree->Branch("m_recojeteta", &m_recojeteta, "m_recojeteta/D");
  m_recojettree->Branch("m_recojetenergy", &m_recojetenergy, "m_recojetenergy/D");
  m_recojettree->Branch("m_truthjetid", &m_truthjetid, "m_truthjetid/I");
  m_recojettree->Branch("m_truthjetp", &m_truthjetp, "m_truthjetp/D");
  m_recojettree->Branch("m_truthjetphi", &m_truthjetphi, "m_truthjetphi/D");
  m_recojettree->Branch("m_truthjeteta", &m_truthjeteta, "m_truthjeteta/D");
  m_recojettree->Branch("m_truthjetpt", &m_truthjetpt, "m_truthjetpt/D");
  m_recojettree->Branch("m_truthjetenergy", &m_truthjetenergy, "m_truthjetenergy/D");
  m_recojettree->Branch("m_truthjetpx", &m_truthjetpx, "m_truthjetpx/D");
  m_recojettree->Branch("m_truthjetpy", &m_truthjetpy, "m_truthjyetpy/D");
  m_recojettree->Branch("m_truthjetpz", &m_truthjetpz, "m_truthjetpz/D");
  m_recojettree->Branch("m_dR", &m_dR, "m_dR/D");

  m_truthjettree = new TTree("truthjettree", "A tree with truth jets");
  m_truthjettree->Branch("m_truthjetid", &m_truthjetid, "m_truthjetid/I");
  m_truthjettree->Branch("m_truthjetp", &m_truthjetp, "m_truthjetp/D");
  m_truthjettree->Branch("m_truthjetphi", &m_truthjetphi, "m_truthjetphi/D");
  m_truthjettree->Branch("m_truthjeteta", &m_truthjeteta, "m_truthjeteta/D");
  m_truthjettree->Branch("m_truthjetpt", &m_truthjetpt, "m_truthjetpt/D");
  m_truthjettree->Branch("m_truthjetenergy", &m_truthjetenergy, "m_truthjetenergy/D");
  m_truthjettree->Branch("m_truthjetpx", &m_truthjetpx, "m_truthjetpx/D");
  m_truthjettree->Branch("m_truthjetpy", &m_truthjetpy, "m_truthjetpy/D");
  m_truthjettree->Branch("m_truthjetpz", &m_truthjetpz, "m_truthjetpz/D");

  m_tracktree = new TTree("tracktree", "A tree with svtx tracks");
  m_tracktree->Branch("m_tr_px", &m_tr_px, "m_tr_px/D");
  m_tracktree->Branch("m_tr_py", &m_tr_py, "m_tr_py/D");
  m_tracktree->Branch("m_tr_pz", &m_tr_pz, "m_tr_pz/D");
  m_tracktree->Branch("m_tr_p", &m_tr_p, "m_tr_p/D");
  m_tracktree->Branch("m_tr_pt", &m_tr_pt, "m_tr_pt/D");
  m_tracktree->Branch("m_tr_phi", &m_tr_phi, "m_tr_phi/D");
  m_tracktree->Branch("m_tr_eta", &m_tr_eta, "m_tr_eta/D");
  m_tracktree->Branch("m_charge", &m_charge, "m_charge/I");
  m_tracktree->Branch("m_chisq", &m_chisq, "m_chisq/D");
  m_tracktree->Branch("m_ndf", &m_ndf, "m_ndf/I");
  m_tracktree->Branch("m_dca", &m_dca, "m_dca/D");
  m_tracktree->Branch("m_tr_x", &m_tr_x, "m_tr_x/D");
  m_tracktree->Branch("m_tr_y", &m_tr_y, "m_tr_y/D");
  m_tracktree->Branch("m_tr_z", &m_tr_z, "m_tr_z/D");
  m_tracktree->Branch("m_truth_is_primary", &m_truth_is_primary, "m_truth_is_primary/I");
  m_tracktree->Branch("m_truthtrackpx", &m_truthtrackpx, "m_truthtrackpx/D");
  m_tracktree->Branch("m_truthtrackpy", &m_truthtrackpy, "m_truthtrackpy/D");
  m_tracktree->Branch("m_truthtrackpz", &m_truthtrackpz, "m_truthtrackpz/D");
  m_tracktree->Branch("m_truthtrackp", &m_truthtrackp, "m_truthtrackp/D");
  m_tracktree->Branch("m_truthtracke", &m_truthtracke, "m_truthtracke/D");
  m_tracktree->Branch("m_truthtrackpt", &m_truthtrackpt, "m_truthtrackpt/D");
  m_tracktree->Branch("m_truthtrackphi", &m_truthtrackphi, "m_truthtrackphi/D");
  m_tracktree->Branch("m_truthtracketa", &m_truthtracketa, "m_truthtracketa/D");
  m_tracktree->Branch("m_truthtrackpid", &m_truthtrackpid, "m_truthtrackpid/I");

  m_hepmctree = new TTree("hepmctree", "A tree with hepmc truth particles");
  m_hepmctree->Branch("m_partid1", &m_partid1, "m_partid1/I");
  m_hepmctree->Branch("m_partid2", &m_partid2, "m_partid2/I");
  m_hepmctree->Branch("m_x1", &m_x1, "m_x1/D");
  m_hepmctree->Branch("m_x2", &m_x2, "m_x2/D");
  m_hepmctree->Branch("m_mpi", &m_mpi, "m_mpi/I");
  m_hepmctree->Branch("m_process_id", &m_process_id, "m_process_id/I");
  m_hepmctree->Branch("m_truthenergy", &m_truthenergy, "m_truthenergy/D");
  m_hepmctree->Branch("m_trutheta", &m_trutheta, "m_trutheta/D");
  m_hepmctree->Branch("m_truthphi", &m_truthphi, "m_truthphi/D");
  m_hepmctree->Branch("m_truthpx", &m_truthpx, "m_truthpx/D");
  m_hepmctree->Branch("m_truthpy", &m_truthpy, "m_truthpy/D");
  m_hepmctree->Branch("m_truthpz", &m_truthpz, "m_truthpz/D");
  m_hepmctree->Branch("m_truthpt", &m_truthpt, "m_truthpt/D");
  m_hepmctree->Branch("m_numparticlesinevent", &m_numparticlesinevent, "m_numparticlesinevent/I");
  m_hepmctree->Branch("m_truthpid", &m_truthpid, "m_truthpid/I");

  m_truthtree = new TTree("truthg4tree", "A tree with truth g4 particles");
  m_truthtree->Branch("m_truthenergy", &m_truthenergy, "m_truthenergy/D");
  m_truthtree->Branch("m_truthp", &m_truthp, "m_truthp/D");
  m_truthtree->Branch("m_truthpx", &m_truthpx, "m_truthpx/D");
  m_truthtree->Branch("m_truthpy", &m_truthpy, "m_truthpy/D");
  m_truthtree->Branch("m_truthpz", &m_truthpz, "m_truthpz/D");
  m_truthtree->Branch("m_truthpt", &m_truthpt, "m_truthpt/D");
  m_truthtree->Branch("m_truthphi", &m_truthphi, "m_truthphi/D");
  m_truthtree->Branch("m_trutheta", &m_trutheta, "m_trutheta/D");
  m_truthtree->Branch("m_truthpid", &m_truthpid, "m_truthpid/I");

  m_clustertree = new TTree("clustertree", "A tree with emcal clusters");
  m_clustertree->Branch("m_clusenergy", &m_clusenergy, "m_clusenergy/D");
  m_clustertree->Branch("m_cluseta", &m_cluseta, "m_cluseta/D");
  m_clustertree->Branch("m_clustheta", &m_clustheta, "m_clustheta/D");
  m_clustertree->Branch("m_cluspt", &m_cluspt, "m_cluspt/D");
  m_clustertree->Branch("m_clusphi", &m_clusphi, "m_clusphi/D");
  m_clustertree->Branch("m_cluspx", &m_cluspx, "m_cluspx/D");
  m_clustertree->Branch("m_cluspy", &m_cluspy, "m_cluspy/D");
  m_clustertree->Branch("m_cluspz", &m_cluspz, "m_cluspz/D");
  m_clustertree->Branch("m_E_4x4", &m_E_4x4, "m_E_4x4/D");
}

/**
 * This function initializes all of the member variables in this class so that there
 * are no variables that might not be set before e.g. writing them to the output
 * trees. 
 */
void AnaTutorial::initializeVariables()
{
  m_outfile = new TFile();
  m_phi_h = new TH1F();
  m_eta_phi_h = new TH2F();

  m_partid1 = -99;
  m_partid2 = -99;
  m_x1 = -99;
  m_x2 = -99;
  m_mpi = -99;
  m_process_id = -99;
  m_truthenergy = -99;
  m_trutheta = -99;
  m_truthphi = -99;
  m_truthp = -99;
  m_truthpx = -99;
  m_truthpy = -99;
  m_truthpz = -99;
  m_truthpt = -99;
  m_numparticlesinevent = -99;
  m_truthpid = -99;

  m_tr_px = -99;
  m_tr_py = -99;
  m_tr_pz = -99;
  m_tr_p = -99;
  m_tr_pt = -99;
  m_tr_phi = -99;
  m_tr_eta = -99;
  m_charge = -99;
  m_chisq = -99;
  m_ndf = -99;
  m_dca = -99;
  m_tr_x = -99;
  m_tr_y = -99;
  m_tr_z = -99;
  m_truth_is_primary = -99;
  m_truthtrackpx = -99;
  m_truthtrackpy = -99;
  m_truthtrackpz = -99;
  m_truthtrackp = -99;
  m_truthtracke = -99;
  m_truthtrackpt = -99;
  m_truthtrackphi = -99;
  m_truthtracketa = -99;
  m_truthtrackpid = -99;

  m_recojetpt = -99;
  m_recojetid = -99;
  m_recojetpx = -99;
  m_recojetpy = -99;
  m_recojetpz = -99;
  m_recojetphi = -99;
  m_recojetp = -99;
  m_recojetenergy = -99;
  m_recojeteta = -99;
  m_truthjetid = -99;
  m_truthjetp = -99;
  m_truthjetphi = -99;
  m_truthjeteta = -99;
  m_truthjetpt = -99;
  m_truthjetenergy = -99;
  m_truthjetpx = -99;
  m_truthjetpy = -99;
  m_truthjetpz = -99;
  m_dR = -99;
}
