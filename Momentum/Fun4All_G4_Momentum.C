#pragma once
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 00, 0)
#include <fun4all/Fun4AllDstOutputManager.h>
#include <fun4all/Fun4AllDummyInputManager.h>
#include <fun4all/Fun4AllInputManager.h>
#include <fun4all/Fun4AllOutputManager.h>
#include <fun4all/Fun4AllServer.h>
#include <fun4all/SubsysReco.h>
#include <g4detectors/PHG4CylinderSubsystem.h>
#include <g4trackfastsim/PHG4TrackFastSim.h>
#include <g4trackfastsim/PHG4TrackFastSimEval.h>
#include <g4main/PHG4SimpleEventGenerator.h>
#include <g4main/PHG4TruthSubsystem.h>
#include <g4main/PHG4Reco.h>
#include <phool/recoConsts.h>
R__LOAD_LIBRARY(libfun4all.so)
R__LOAD_LIBRARY(libg4testbench.so)
R__LOAD_LIBRARY(libg4detectors.so)
R__LOAD_LIBRARY(libg4trackfastsim.so)
#endif

int Fun4All_G4_Momentum(const int nEvents = 1000, const char *outfile = NULL)
{
  gSystem->Load("libfun4all");
  gSystem->Load("libg4detectors.so");
  gSystem->Load("libg4testbench.so");
  gSystem->Load("libg4trackfastsim.so");

  const bool whether_to_sim_calorimeter = false;

  ///////////////////////////////////////////
  // Make the Server
  //////////////////////////////////////////
  Fun4AllServer *se = Fun4AllServer::instance();
  se->Verbosity(01);

  recoConsts *rc = recoConsts::instance();
  rc->set_IntFlag("RANDOMSEED", 12345); // if you want to use a fixed seed


  // toss low multiplicity dummy events
  PHG4SimpleEventGenerator *gen = new PHG4SimpleEventGenerator();
  gen->add_particles("e+", 1);  // mu+,e+,proton,pi+,Upsilon
  //gen->add_particles("pi+",100); // 100 pion option

    gen->set_vertex_distribution_function(PHG4SimpleEventGenerator::Uniform,
                                          PHG4SimpleEventGenerator::Uniform,
                                          PHG4SimpleEventGenerator::Uniform);
    gen->set_vertex_distribution_mean(0.0, 0.0, 0.0);
    gen->set_vertex_distribution_width(0.0, 0.0, 0.0);

  gen->set_vertex_size_function(PHG4SimpleEventGenerator::Uniform);
  gen->set_vertex_size_parameters(0.0, 0.0);
  gen->set_eta_range(-.05, .05);
  gen->set_phi_range(-1.0 * TMath::Pi(), 1.0 * TMath::Pi());
  gen->set_pt_range(4, 4);
  gen->Embed(2);
  gen->Verbosity(0);

  se->registerSubsystem(gen);

  
  

  PHG4Reco *g4Reco = new PHG4Reco();
  g4Reco->set_field(1.5);  // 1.5 T solenoidal field

  double si_thickness[6] = {0.32,  0.32, 0.032};
  double svxrad[6] = {10,40,80};
  double length[6] = {80., 80., 80.};  // -1 use eta coverage to determine length
  PHG4CylinderSubsystem *cyl;
  // here is our silicon:
  for (int ilayer = 0; ilayer <3; ilayer++)
  {
    cyl = new PHG4CylinderSubsystem("SVTX", ilayer);
    cyl->set_double_param("radius", svxrad[ilayer]);
    cyl->set_string_param("material", "G4_Si");
    cyl->set_double_param("thickness", si_thickness[ilayer]);
    cyl->SetActive();
    cyl->SuperDetector("SVTX");
    if (length[ilayer] > 0)
    {
      cyl->set_double_param("length", length[ilayer]);
    }
    g4Reco->registerSubsystem(cyl);
  }

  // Black hole swallows everything - prevent loopers from returning
  // to inner detectors
  cyl = new PHG4CylinderSubsystem("BlackHole", 0);
  cyl->set_double_param("radius", 90);        // 80 cm
  cyl->set_double_param("thickness", 0.1); // does not matter (but > 0)
  cyl->SetActive();
  cyl->BlackHole(); // eats everything
  g4Reco->registerSubsystem(cyl);

  PHG4TruthSubsystem *truth = new PHG4TruthSubsystem();
  g4Reco->registerSubsystem(truth);

  se->registerSubsystem(g4Reco);

  //---------------------------
  // fast pattern recognition and full Kalman filter
  // output evaluation file for truth track and reco tracks are PHG4TruthInfoContainer
  //---------------------------
  PHG4TrackFastSim *kalman = new PHG4TrackFastSim("PHG4TrackFastSim");
  kalman->set_use_vertex_in_fitting(false);
  kalman->set_sub_top_node_name("SVTX");
  kalman->set_trackmap_out_name("SvtxTrackMap");
  kalman->set_primary_assumption_pid(-11);

  //  add Si Trtacker
  kalman->add_phg4hits(
      "G4HIT_SVTX",                //      const std::string& phg4hitsNames,
      PHG4TrackFastSim::Cylinder,  //      const DETECTOR_TYPE phg4dettype,
      300e-4,                      //       radial-resolution [cm]
      30e-4,                       //        azimuthal-resolution [cm]
      1,                           //      z-resolution [cm]
      1,                           //      efficiency,
      0                            //      noise hits
  );
  se->registerSubsystem(kalman);

  PHG4TrackFastSimEval *fast_sim_eval = new PHG4TrackFastSimEval("FastTrackingEval");
  fast_sim_eval->set_filename("FastTrackingEval.root");
  se->registerSubsystem(fast_sim_eval);
  //---------------------------

  //---------------------------
  // output DST file for further offlien analysis
  //---------------------------
  if (outfile)
  {
    Fun4AllOutputManager *out = new Fun4AllDstOutputManager("DSTOUT", outfile);
    se->registerOutputManager(out);
  }
  Fun4AllInputManager *in = new Fun4AllDummyInputManager("JADE");
  se->registerInputManager(in);

  if (nEvents > 0)
  {
    se->run(nEvents);
    // finish job - close and save output files
    se->End();
    std::cout << "All done" << std::endl;

    // cleanup - delete the server and exit
    delete se;
    gSystem->Exit(0);
  }
  return 0;
}
