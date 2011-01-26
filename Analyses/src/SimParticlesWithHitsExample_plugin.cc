//
// Plugin to show how to use the SimParticlesWithHits class.
//
// $Id: SimParticlesWithHitsExample_plugin.cc,v 1.2 2011/01/26 23:10:18 kutschke Exp $
// $Author: kutschke $
// $Date: 2011/01/26 23:10:18 $
//
// Original author Rob Kutschke.
//

// C++ includes.
#include <iostream>
#include <string>

// Framework includes.
#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

// Mu2e includes.
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TrackerGeom/inc/Tracker.hh"
#include "Mu2eUtilities/inc/SimParticlesWithHits.hh"

using namespace std;

namespace mu2e {

  class SimParticlesWithHitsExample : public edm::EDAnalyzer {
  public:
    explicit SimParticlesWithHitsExample(edm::ParameterSet const& pset):
      _g4ModuleLabel(pset.getParameter<std::string>("g4ModuleLabel")),
      _hitMakerModuleLabel(pset.getParameter<std::string>("hitMakerModuleLabel")),
      _trackerStepPoints(pset.getParameter<std::string>("trackerStepPoints")),
      _minEnergyDep(pset.getParameter<double>("minEnergyDep")),
      _minHits(pset.getParameter<uint32_t>("minHits")){
    }
    virtual ~SimParticlesWithHitsExample() { }

    void analyze( edm::Event const& e, edm::EventSetup const&);

  private:

    // Label of the modules that created the data products.
    std::string _g4ModuleLabel;
    std::string _hitMakerModuleLabel;

    // Name of the tracker StepPoint collection
    std::string _trackerStepPoints;

    // Cuts used inside SimParticleWithHits:
    //  - drop hits with too little energy deposited.
    //  - drop SimParticles with too few hits.
    double _minEnergyDep;
    size_t _minHits;

  };

  void
  SimParticlesWithHitsExample::analyze(edm::Event const& evt, edm::EventSetup const&) {

    const Tracker& tracker = getTrackerOrThrow();
    
    // Construct an object that ties together all of the simulated particle and hit info.
    SimParticlesWithHits sims( evt,
                               _g4ModuleLabel, 
                               _hitMakerModuleLabel,
                               _trackerStepPoints,
                               _minEnergyDep,
                               _minHits );

    typedef SimParticlesWithHits::map_type map_type;
    //    int n(0);
    for ( map_type::const_iterator i=sims.begin();
          i != sims.end(); ++i ){

      // All information about this SimParticle
      SimParticleInfo const& simInfo = i->second;

      // Information about StrawHits that belong on this SimParticle.
      vector<StrawHitMCInfo> const& infos = simInfo.strawHitInfos();

      cout << "SimParticle: "
           << " Event: " << evt.id().event()
           << " Track: " << i->first 
           << " PdgId: " << simInfo.simParticle().pdgId() 
           << " |p|: "   << simInfo.simParticle().startMomentum().vect().mag()
           << " Hits: "  << infos.size()
           << endl;

      // Loop over all StrawsHits to which this SimParticle contributed.
      for ( size_t j=0; j<infos.size(); ++j){
        StrawHitMCInfo const& info = infos.at(j);
        StrawHit const& hit      = info.hit();
        Straw const& straw       = tracker.getStraw(hit.strawIndex());
        cout << "     Straw Hit: "
             << info.index()             << " "
             << hit.strawIndex().asInt() << " "
             << hit.time()               << " "
             << info.isShared()          << " "
             << straw.getMidPoint().z()  << " "
             << info.time()              << " | StepPointMCs: ";

        // Loop over all StepPointMC's that contribute to this StrawHit.
        std::vector<StepPointMC const *> const& steps = info.steps();
        for ( size_t k=0; k<steps.size(); ++k){
          StepPointMC const& step = *(steps[k]);
          cout << " (" << step.time()  << "," << step.momentum().mag() << "," << step.trackId() << ")";
        }
        cout << endl;

      }

    }

  } // end of ::analyze.
  
}

using mu2e::SimParticlesWithHitsExample;
DEFINE_FWK_MODULE(SimParticlesWithHitsExample);
