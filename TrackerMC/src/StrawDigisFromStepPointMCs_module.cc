
// It also builds the truth match map
//
// $Id: StrawDigisFromStepPointMCs_module.cc,v 1.39 2014/08/29 19:49:23 brownd Exp $
// $Author: brownd $
// $Date: 2014/08/29 19:49:23 $
//
// Original author David Brown, LBNL
//
// framework
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "GeometryService/inc/GeomHandle.hh"
#include "art/Framework/Core/EDProducer.h"
#include "GeometryService/inc/DetectorSystem.hh"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "SeedService/inc/SeedService.hh"
#include "cetlib_except/exception.h"
// conditions
#include "ConditionsService/inc/ConditionsHandle.hh"
#include "ConditionsService/inc/AcceleratorParams.hh"
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TTrackerGeom/inc/TTracker.hh"
#include "ConfigTools/inc/ConfigFileLookupPolicy.hh"
#include "TrackerConditions/inc/StrawElectronics.hh"
#include "TrackerConditions/inc/StrawPhysics.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "GeometryService/inc/DetectorSystem.hh"
#include "BFieldGeom/inc/BFieldManager.hh"
#include "BTrk/BField/BField.hh"
#include "GlobalConstantsService/inc/GlobalConstantsHandle.hh"
#include "GlobalConstantsService/inc/ParticleDataTable.hh"
// utiliities
#include "GeneralUtilities/inc/TwoLinePCA.hh"
#include "Mu2eUtilities/inc/SimParticleTimeOffset.hh"
#include "TrackerConditions/inc/DeadStrawList.hh"
#include "TrackerConditions/inc/Types.hh"
// data
#include "RecoDataProducts/inc/StrawDigiCollection.hh"
#include "MCDataProducts/inc/StepPointMCCollection.hh"
#include "MCDataProducts/inc/PtrStepPointMCVectorCollection.hh"
#include "MCDataProducts/inc/StrawDigiMCCollection.hh"
// MC structures
#include "TrackerMC/inc/StrawClusterSequencePair.hh"
#include "TrackerMC/inc/StrawWaveform.hh"
#include "TrackerMC/inc/IonCluster.hh"
//CLHEP
#include "CLHEP/Random/RandGaussQ.h"
#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Random/RandExponential.h"
#include "CLHEP/Random/RandPoisson.h"
#include "CLHEP/Vector/LorentzVector.h"
// root
#include "TMath.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TGraph.h"
#include "TMarker.h"
#include "TTree.h"
// C++
#include <map>
#include <algorithm>
#include <array>
using namespace std;
using CLHEP::Hep3Vector;
namespace mu2e {
  namespace TrackerMC {
    using namespace TrkTypes;

    struct WireCharge { // charge at the wire after drift
      double _charge; // charge at the wire, in units of pC
      double _time; // relative time at the wire, relative to ionzation time (ns)
      double _dd;  // transverse distance drifted to the wrie
      double _wpos; // position long the wire, WRT the wire center, signed by the wire direction
    };

    struct WireEndCharge { // charge at one end of the wire after propagation
      double _charge; // charge at the wire, in units of pC
      double _time; // time at the wire end, relative to the time the charge reached the wire (ns)
      double _wdist; // propagation distance from the point of collection to the end
    };

    class StrawDigisFromStepPointMCs : public art::EDProducer {

      public:

	typedef map<StrawIndex,StrawClusterSequencePair> StrawClusterMap;  // clusts by straw
	typedef vector<art::Ptr<StepPointMC> > StrawSPMCPV; // vector of associated StepPointMCs for a single straw/particle
	typedef list<WFX> WFXList;
	typedef WFXList::const_iterator WFXP[2];
	typedef std::array<StrawWaveform,2> SWFP;

	explicit StrawDigisFromStepPointMCs(fhicl::ParameterSet const& pset);
	// Accept compiler written d'tor.

	virtual void beginJob();
	virtual void beginRun( art::Run& run );
	virtual void produce( art::Event& e);

      private:

	// Diagnostics level.
	int _debug, _diag, _printLevel;
	unsigned _maxhist;
	bool  _xtalkhist;
	unsigned _minnxinghist;
	double _tstep, _nfall;
	// Limit on number of events for which there will be full printout.
	int _maxFullPrint;
	// Name of the tracker StepPoint collection
	string _trackerStepPoints;

	// Parameters
	bool   _addXtalk; // should we add cross talk hits?
	double _ctMinCharge; // minimum charge to add cross talk (for performance issues)
	bool   _addNoise; // should we add noise hits?
	double _preampxtalk, _postampxtalk; // x-talk parameters; these should come from conditions, FIXME!!
	double _bgcut; // cut dividing 'min-ion' particles from highly-ionizing
	double _minstepE; // minimum step energy to simulate
	string _g4ModuleLabel;  // Nameg of the module that made these hits.
	double _mbtime; // period of 1 microbunch
	double _mbbuffer; // buffer on that for ghost clusts (for waveform)
	double _steptimebuf; // buffer for MC step point times
	// models of straw response to stimuli
	ConditionsHandle<StrawPhysics> _strawphys;
	ConditionsHandle<StrawElectronics> _strawele;
	SimParticleTimeOffset _toff;
	TrkTypes::Path _diagpath; // electronics path for waveform diagnostics
	// Random number distributions
	art::RandomNumberGenerator::base_engine_t& _engine;
	CLHEP::RandGaussQ _randgauss;
	CLHEP::RandFlat _randflat;
	CLHEP::RandExponential _randexp;
	CLHEP::RandPoisson _randP;
	// A category for the error logger.
	const string _messageCategory;
	// Give some informationation messages only on the first event.
	bool _firstEvent;
	// record the BField directionat the tracker center
	Hep3Vector _bdir;
	// minimum pt (perp to bfield) to assume straight trajectory in a starw
	double _ptmin, _ptfac;
	// max # clusters for modeling non-minion steps
	unsigned _maxnclu;
	bool _sort; // sort cluster sizes before filling energy
	// List of dead straws as a parameter set; needed at beginRun time.
	fhicl::ParameterSet _deadStraws;
	DeadStrawList _strawStatus;
	// diagnostics
	TTree* _swdiag;
	Int_t _swplane, _swpanel, _swlayer, _swstraw, _ndigi;
	Float_t _hqsum[2], _vmax[2], _tvmax[2], _sesum[2];
	Int_t _wmcpdg[2], _wmcproc[2], _nxing[2], _nclu[2];
	Int_t _nsteppoint[2], _npart[2];
	Float_t _mce[2], _slen[2], _sedep[2];
	Float_t _tmin[2], _tmax[2], _txing[2], _xddist[2], _xwdist[2], _xpdist[2];
	TTree* _sddiag;
	Int_t _sdplane, _sdpanel, _sdlayer, _sdstraw;
	Int_t _ncludd[2], _iclust[2];
	Int_t _nstep;
	Float_t _ectime[2], _cdist[2];
	Float_t _xtime[2], _tctime[2], _charge[2], _ddist[2];
	Float_t _wdist[2], _vstart[2], _vcross[2];
	Float_t _mctime, _mcenergy, _mctrigenergy, _mcthreshenergy;
	Int_t _mcthreshpdg, _mcthreshproc, _mcnstep;
	Float_t _mcdca;
	Int_t _dmcpdg, _dmcproc, _dmcgen;
	Float_t _dmcmom;
	Bool_t _xtalk;
	vector<unsigned> _adc;
	Int_t _tdc[2], _tot[2];
	TTree* _sdiag;
	Float_t _steplen, _stepE, _qsum, _esum, _eesum, _qe, _partP;
	Int_t _nclusd, _netot, _partPDG;
	vector<IonCluster> _clusters;
	
	//    vector<TGraph*> _waveforms;
	vector<TH1F*> _waveforms;
	//  helper functions
	void fillClusterMap(art::Event const& event, StrawClusterMap & hmap);
	void addStep(art::Ptr<StepPointMC> const& spmcptr, Straw const& straw, StrawClusterSequencePair& shsp);
	void divideStep(StepPointMC const& step, vector<IonCluster>& clusters);
	void driftCluster(Straw const& straw, IonCluster const& cluster, WireCharge& wireq);
	void propagateCharge(Straw const& straw, WireCharge const& wireq, StrawEnd end, WireEndCharge& weq);
	double microbunchTime(double globaltime) const;
	void addGhosts(StrawCluster const& clust,StrawClusterSequence& shs);
	void addNoise(StrawClusterMap& hmap);
	void findThresholdCrossings(SWFP const& swfp, WFXList& xings);
	void createDigis(StrawClusterSequencePair const& hsp,
	    XTalk const& xtalk,
	    StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	    PtrStepPointMCVectorCollection* mcptrs );
	void fillDigis(WFXList const& xings,SWFP const& swfp , StrawIndex index,
	    StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	    PtrStepPointMCVectorCollection* mcptrs );
	void createDigi(WFXP const& xpair, SWFP const& wf, StrawIndex index, StrawDigiCollection* digis);
	void findCrossTalkStraws(Straw const& straw,vector<XTalk>& xtalk);
	void fillClusterNe(std::vector<unsigned>& me);
	void fillClusterPositions(Straw const& straw, StepPointMC const& step, std::vector<Hep3Vector>& cpos);
	void fillClusterMinion(StepPointMC const& step, std::vector<unsigned>& me, std::vector<double>& cen);
	  // diagnostic functions
	void waveformHist(SWFP const& wf, WFXList const& xings);
	void waveformDiag(SWFP const& wf, WFXList const& xings);
	void digiDiag(SWFP const& wf, WFXP const& xpair, StrawDigi const& digi,StrawDigiMC const& mcdigi);
    };

    StrawDigisFromStepPointMCs::StrawDigisFromStepPointMCs(fhicl::ParameterSet const& pset) :
      // diagnostic parameters
      _debug(pset.get<int>("debugLevel",0)),
      _diag(pset.get<int>("diagLevel",0)),
      _printLevel(pset.get<int>("printLevel",0)),
      _maxhist(pset.get<unsigned>("MaxHist",100)),
      _xtalkhist(pset.get<bool>("CrossTalkHist",false)),
      _minnxinghist(pset.get<int>("MinNXingHist",1)), // minimum # of crossings to histogram waveform
      _tstep(pset.get<double>("WaveformStep",0.1)), // ns
      _nfall(pset.get<double>("WaveformTail",5.0)),  // # of decay lambda past last signal to record waveform
      // Parameters
      _maxFullPrint(pset.get<int>("maxFullPrint",2)),
      _trackerStepPoints(pset.get<string>("trackerStepPoints","tracker")),
      _addXtalk(pset.get<bool>("addCrossTalk",false)),
      _ctMinCharge(pset.get<double>("xtalkMinimumCharge",0)),
      _addNoise(pset.get<bool>("addNoise",false)),
      _preampxtalk(pset.get<double>("preAmplificationCrossTalk",0.0)),
      _postampxtalk(pset.get<double>("postAmplificationCrossTalk",0.02)), // dimensionless relative coupling
      _bgcut(pset.get<double>("BetaGammaCut",0.5)), // treat particles with beta-gamma above this as minimum-ionizing 
      _minstepE(pset.get<double>("minstepE",2.0e-6)), // minimum step energy depostion to turn into a straw signal (MeV)
      _g4ModuleLabel(pset.get<string>("g4ModuleLabel")),
      _steptimebuf(pset.get<double>("StepPointMCTimeBuffer",100.0)), // nsec
      _toff(pset.get<fhicl::ParameterSet>("TimeOffsets", fhicl::ParameterSet())),
      _diagpath(static_cast<TrkTypes::Path>(pset.get<int>("WaveformDiagPath",TrkTypes::thresh))),
      // Random number distributions
      _engine(createEngine( art::ServiceHandle<SeedService>()->getSeed())),
      _randgauss( _engine ),
      _randflat( _engine ),
      _randexp( _engine),
      _randP( _engine),
      _messageCategory("HITS"),
      _firstEvent(true),      // Control some information messages.
      _ptfac(pset.get<double>("PtFactor", 2.0)), // factor for defining curling in a straw
      _maxnclu(pset.get<unsigned>("MaxNClusters", 10)), // max # of clusters for low-PT steps
      _sort(pset.get<bool>("SortClusterEnergy",false)), // 
      _deadStraws(pset.get<fhicl::ParameterSet>("deadStrawList", fhicl::ParameterSet())),
      _strawStatus(pset.get<fhicl::ParameterSet>("deadStrawList", fhicl::ParameterSet()))
    {
      // Tell the framework what we make.
      produces<StrawDigiCollection>();
      produces<PtrStepPointMCVectorCollection>();
      produces<StrawDigiMCCollection>();
    }

    void StrawDigisFromStepPointMCs::beginJob(){
      
      if(_diag > 0){

	art::ServiceHandle<art::TFileService> tfs;
	_sdiag =tfs->make<TTree>("sdiag","Step diagnostics");
	_sdiag->Branch("steplen",&_steplen,"steplen/F");
	_sdiag->Branch("stepE",&_stepE,"stepE/F");
	_sdiag->Branch("partP",&_partP,"partP/F");
	_sdiag->Branch("qsum",&_qsum,"qsum/F");
	_sdiag->Branch("esum",&_esum,"esum/F");
	_sdiag->Branch("eesum",&_eesum,"eesum/F");
	_sdiag->Branch("qe",&_qe,"qe/F");
	_sdiag->Branch("nclust",&_nclusd,"nclust/I");
	_sdiag->Branch("netot",&_netot,"netot/I");
	_sdiag->Branch("partPDG",&_partPDG,"partPDG/I");
	_sdiag->Branch("clusters",&_clusters);

   	_swdiag =tfs->make<TTree>("swdiag","StrawWaveform diagnostics");
	_swdiag->Branch("plane",&_swplane,"plane/I");
	_swdiag->Branch("panel",&_swpanel,"panel/I");
	_swdiag->Branch("layer",&_swlayer,"layer/I");
	_swdiag->Branch("straw",&_swstraw,"straw/I");
	_swdiag->Branch("ndigi",&_ndigi,"ndigi/I");
	_swdiag->Branch("hqsum",&_hqsum,"hqsumcal/F:hqsumhv/F");
	_swdiag->Branch("vmax",&_vmax,"vmaxcal/F:vmaxhv/F");
	_swdiag->Branch("tvmax",&_tvmax,"tvmaxcal/F:tvmaxhv/F");
	_swdiag->Branch("mcpdg",&_wmcpdg,"mcpdgcal/I:mcpdghv/I");
	_swdiag->Branch("mcproc",&_wmcproc,"mcproccal/I:mcprochv/I");
	_swdiag->Branch("mce",&_mce,"mcecal/F:mcehv/F");
	_swdiag->Branch("slen",&_slen,"slencal/F:slenhv/F");
	_swdiag->Branch("sedep",&_sedep,"sedepcal/F:sedephv/F");
	_swdiag->Branch("nxing",&_nxing,"nxingcal/I:nxinghv/I");
	_swdiag->Branch("nclust",&_nclu,"nclucal/I:ncluhv/I");
	_swdiag->Branch("nstep",&_nsteppoint,"nscal/I:nshv/I");
	_swdiag->Branch("sesum",&_sesum,"sesumcal/F:sesumhv/F");
	_swdiag->Branch("npart",&_npart,"npart/I");
	_swdiag->Branch("tmin",&_tmin,"tmincal/F:tminhv/F");
	_swdiag->Branch("tmax",&_tmax,"tmaxcal/F:tmaxhv/F");
	_swdiag->Branch("txing",&_txing,"txcal/F:txhv/F");
	_swdiag->Branch("xddist",&_xddist,"xdcal/F:xdhv/F");
	_swdiag->Branch("xwdist",&_xwdist,"xwdcal/F:xwdhv/F");
	_swdiag->Branch("xpdist",&_xpdist,"xpdcal/F:xpdhv/F");
  

	if(_diag > 1){
	  _sddiag =tfs->make<TTree>("sddiag","StrawDigi diagnostics");
	  _sddiag->Branch("plane",&_sdplane,"plane/I");
	  _sddiag->Branch("panel",&_sdpanel,"panel/I");
	  _sddiag->Branch("layer",&_sdlayer,"layer/I");
	  _sddiag->Branch("straw",&_sdstraw,"straw/I");
	  _sddiag->Branch("nstep",&_nstep,"nstep/I");
	  _sddiag->Branch("xtime",&_xtime,"xtimecal/F:xtimehv/F");
	  _sddiag->Branch("tctime",&_tctime,"tctimecal/F:tctimehv/F");
	  _sddiag->Branch("ectime",&_ectime,"ectimecal/F:ectimehv/F");
	  _sddiag->Branch("charge",&_charge,"chargecal/F:chargehv/F");
	  _sddiag->Branch("wdist",&_wdist,"wdistcal/F:wdisthv/F");
	  _sddiag->Branch("cdist",&_cdist,"cdistcal/F:cdisthv/F");
	  _sddiag->Branch("vstart",&_vstart,"vstartcal/F:vstarthv/F");
	  _sddiag->Branch("vcross",&_vcross,"vcrosscal/F:vcrosshv/F");
	  _sddiag->Branch("ddist",&_ddist,"ddistcal/F:ddisthv/F");
	  _sddiag->Branch("nclust",&_ncludd,"nclustcal/I:nclusthv/I");
	  _sddiag->Branch("iclust",&_iclust,"iclustcal/I:iclusthv/I");
	  _sddiag->Branch("tdc",&_tdc,"tdccal/I:tdchv/I");
          _sddiag->Branch("tot",&_tot,"totcal/I:tothv/I");
	  _sddiag->Branch("adc",&_adc);
	  _sddiag->Branch("mctime",&_mctime,"mctime/F");
	  _sddiag->Branch("mcenergy",&_mcenergy,"mcenergy/F");
	  _sddiag->Branch("mctrigenergy",&_mctrigenergy,"mctrigenergy/F");
	  _sddiag->Branch("mcthreshenergy",&_mcthreshenergy,"mcthreshenergy/F");
	  _sddiag->Branch("mcthreshpdg",&_mcthreshpdg,"mcthreshpdg/I");
	  _sddiag->Branch("mcthreshproc",&_mcthreshproc,"mcthreshproc/I");
	  _sddiag->Branch("mcnstep",&_mcnstep,"mcnstep/I");
	  _sddiag->Branch("mcdca",&_mcdca,"mcdca/F");
	  _sddiag->Branch("mcpdg",&_dmcpdg,"mcpdg/I");
	  _sddiag->Branch("mcproc",&_dmcproc,"mcproc/I");
	  _sddiag->Branch("mcgen",&_dmcgen,"mcgen/I");
	  _sddiag->Branch("mcmom",&_dmcmom,"mcmom/F");
	  _sddiag->Branch("xtalk",&_xtalk,"xtalk/B");
	}
      }
    }

    void StrawDigisFromStepPointMCs::beginRun( art::Run& run ){
      // set dead straws as listed
      _strawStatus.reset(_deadStraws);
      // get field at the center of the tracker
      GeomHandle<BFieldManager> bfmgr;
      GeomHandle<DetectorSystem> det;
      Hep3Vector vpoint_mu2e = det->toMu2e(Hep3Vector(0.0,0.0,0.0));
      Hep3Vector b0 = bfmgr->getBField(vpoint_mu2e);
      _bdir = b0.unit();
      // compute the transverse momentum for which a particle will curl up in a straw
      const Tracker& tracker = getTrackerOrThrow();
      const Straw& straw = tracker.getStraw(StrawId(0,0,0,0));
      double rstraw = straw.getRadius();
      _ptmin = _ptfac*BField::mmTeslaToMeVc*b0.mag()*rstraw;
      if ( _printLevel > 0 ) {
	_strawphys = ConditionsHandle<StrawPhysics>("ignored");
	_strawphys->print(cout);
      }
    }

    void StrawDigisFromStepPointMCs::produce(art::Event& event) {
      if ( _printLevel > 1 ) cout << "StrawDigisFromStepPointMCs: produce() begin; event " << event.id().event() << endl;
      static int ncalls(0);
      ++ncalls;
      // update conditions caches.
      ConditionsHandle<AcceleratorParams> accPar("ignored");
      _mbtime = accPar->deBuncherPeriod;
      _toff.updateMap(event);
      _strawele = ConditionsHandle<StrawElectronics>("ignored");
      _strawphys = ConditionsHandle<StrawPhysics>("ignored");
      const Tracker& tracker = getTrackerOrThrow();
      // make the microbunch buffer long enough to get the full waveform
      _mbbuffer = _strawele->nADCSamples()*_strawele->adcPeriod();
      // Containers to hold the output information.
      unique_ptr<StrawDigiCollection> digis(new StrawDigiCollection);
      unique_ptr<StrawDigiMCCollection> mcdigis(new StrawDigiMCCollection);
      unique_ptr<PtrStepPointMCVectorCollection> mcptrs(new PtrStepPointMCVectorCollection);
      // create the StrawCluster map
      StrawClusterMap hmap;
      // fill this from the event
      fillClusterMap(event,hmap);
      // add noise clusts
      if(_addNoise)addNoise(hmap);
      // loop over the clust sequences
      for(auto ihsp=hmap.begin();ihsp!= hmap.end();++ihsp){
	StrawClusterSequencePair const& hsp = ihsp->second;
	// create primary digis from this clust sequence
	XTalk self(hsp.strawIndex()); // this object represents the straws coupling to itself, ie 100%
	createDigis(hsp,self,digis.get(),mcdigis.get(),mcptrs.get());
	// if we're applying x-talk, look for nearby coupled straws
	if(_addXtalk) {
	  // only apply if the charge is above a threshold
	  double totalCharge = 0;
	  for(auto ih=hsp.clustSequence(TrkTypes::cal).clustList().begin();ih!= hsp.clustSequence(TrkTypes::cal).clustList().end();++ih){
	    totalCharge += ih->charge();
	  }
	  if( totalCharge > _ctMinCharge){
	    vector<XTalk> xtalk;
	    Straw const& straw = tracker.getStraw(hsp.strawIndex());
	    findCrossTalkStraws(straw,xtalk);
	    for(auto ixtalk=xtalk.begin();ixtalk!=xtalk.end();++ixtalk){
	      createDigis(hsp,*ixtalk,digis.get(),mcdigis.get(),mcptrs.get());
	    }
	  }
	}
      }
      // store the digis in the event
      event.put(move(digis));
      // store MC truth match
      event.put(move(mcdigis));
      event.put(move(mcptrs));
      if ( _printLevel > 1 ) cout << "StrawDigisFromStepPointMCs: produce() end" << endl;
      // Done with the first event; disable some messages.
      _firstEvent = false;
    } // end produce

    void StrawDigisFromStepPointMCs::createDigis(StrawClusterSequencePair const& hsp,
	XTalk const& xtalk,
	StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	PtrStepPointMCVectorCollection* mcptrs ) {
      // instantiate waveforms for both ends of this straw
      SWFP waveforms  ={ StrawWaveform(hsp.clustSequence(TrkTypes::cal),_strawele,xtalk),
	StrawWaveform(hsp.clustSequence(TrkTypes::hv),_strawele,xtalk) };
      // find the threshold crossing points for these waveforms
      WFXList xings;
	// find the threshold crossings
      findThresholdCrossings(waveforms,xings);
      // convert the crossing points into digis, and add them to the event data.  Require both ends to have threshold
      // crossings.  The logic here needs to be improved to require exactly 2 matching ends FIXME!!
      size_t nd = digis->size();
      if(xings.size() > 1){
	// fill digis from these crossings
	fillDigis(xings,waveforms,xtalk._dest,digis,mcdigis,mcptrs);
      } 
      // waveform diagnostics 
      if (_diag >1 && (
	waveforms[0].clusts().clustList().size() > 0 ||
	waveforms[0].clusts().clustList().size() > 0 ) ) {
	// waveform xing diagnostics
	_ndigi = digis->size()-nd;
	waveformDiag(waveforms,xings);
	// waveform histograms
	if(_diag > 2 )waveformHist(waveforms,xings);
      }
    }

    void StrawDigisFromStepPointMCs::fillClusterMap(art::Event const& event, StrawClusterMap & hmap){
	// get conditions
	const TTracker& tracker = static_cast<const TTracker&>(getTrackerOrThrow());
	// Get all of the tracker StepPointMC collections from the event:
	typedef vector< art::Handle<StepPointMCCollection> > HandleVector;
	// This selector will select only data products with the given instance name.
	art::ProductInstanceNameSelector selector(_trackerStepPoints);
	HandleVector stepsHandles;
	event.getMany( selector, stepsHandles);
	// Informational message on the first event.
	if ( _firstEvent ) {
	  mf::LogInfo log(_messageCategory);
	  log << "StrawDigisFromStepPointMCs::fillHitMap will use StepPointMCs from: \n";
	  for ( HandleVector::const_iterator i=stepsHandles.begin(), e=stepsHandles.end();
	      i != e; ++i ){
	    art::Provenance const& prov(*(i->provenance()));
	    log  << "   " << prov.branchName() << "\n";
	  }
	}
	if(stepsHandles.empty()){
	  throw cet::exception("SIM")<<"mu2e::StrawDigisFromStepPointMCs: No StepPointMC collections found for tracker" << endl;
	}
	// Loop over StepPointMC collections
	for ( HandleVector::const_iterator ispmcc=stepsHandles.begin(), espmcc=stepsHandles.end();ispmcc != espmcc; ++ispmcc ){
	  art::Handle<StepPointMCCollection> const& handle(*ispmcc);
	  StepPointMCCollection const& steps(*handle);
	  // Loop over the StepPointMCs in this collection
	  for (size_t ispmc =0; ispmc<steps.size();++ispmc){
	    // find straw index
	    StrawIndex const & strawind = steps[ispmc].strawIndex();
	    // Skip dead straws, and straws that don't exist
	    if(tracker.strawExists(strawind)) {
	      // lookup straw here, to avoid having to find the tracker for every step
	      Straw const& straw = tracker.getStraw(strawind);
	      // Skip steps that occur in the deadened region near the end of each wire,
	      // or in dead regions of the straw
	      double wpos = fabs((steps[ispmc].position()-straw.getMidPoint()).dot(straw.getDirection()));
	      if(wpos <  straw.getDetail().activeHalfLength() &&
		  _strawStatus.isAlive(strawind,wpos) &&
		  steps[ispmc].ionizingEdep() > _minstepE){
		// create ptr to MC truth, used for references
		art::Ptr<StepPointMC> spmcptr(handle,ispmc);
		// create a clust from this step, and add it to the clust map
		addStep(spmcptr,straw,hmap[strawind]);
	      }
	    }
	  }
	}
      }

    void StrawDigisFromStepPointMCs::addStep(art::Ptr<StepPointMC> const& spmcptr,
	Straw const& straw,
	StrawClusterSequencePair& shsp) {
      StepPointMC const& step = *spmcptr;
      StrawIndex const & strawind = step.strawIndex();
      // Subdivide the StepPointMC into ionization clusters
      _clusters.clear();
      divideStep(step,_clusters);
      // check
      if(_debug > 1){
	double ec(0.0);
	double ee(0.0);
	double eq(0.0);
	for (auto const& cluster : _clusters) {
	  ec += cluster._eion;
	  ee += _strawphys->ionizationEnergy(cluster._ne);
	  eq += _strawphys->ionizationEnergy(cluster._charge);
	}
	cout << "step with ionization edep = " << step.ionizingEdep()
	<< " creates " << _clusters.size() 
	<< " clusters with total cluster energy = " << ec 
	<< " electron count energy = " << ee
	<< " charge energy = " << eq << endl;
      }
      // get time offset for this step
      double tstep = _toff.timeWithOffsetsApplied(step);
      // test if this microbunch is worth simulating
      double mbtime = microbunchTime(tstep);
      if( mbtime > _strawele->flashEnd()-_steptimebuf
	  || mbtime <  _strawele->flashStart() ) {
	// drift these clusters to the wire, and record the charge at the wire
	for(auto iclu = _clusters.begin(); iclu != _clusters.end(); ++iclu){
	  WireCharge wireq;
	  driftCluster(straw,*iclu,wireq);
	  // propagate this charge to each end of the wire
	  for(size_t iend=0;iend<2;++iend){
	    StrawEnd end(static_cast<TrkTypes::End>(iend));
	    // compute the longitudinal propagation effects
	    WireEndCharge weq;
	    propagateCharge(straw,wireq,end,weq);
	    // compute the total time, modulo the microbunch
	    double gtime = tstep + wireq._time + weq._time;
	    double ctime = microbunchTime(gtime);
	    // create the clust
	    StrawCluster clust(StrawCluster::primary,strawind,end,ctime,weq._charge,wireq._dd,weq._wdist,
		spmcptr,CLHEP::HepLorentzVector(iclu->_pos,mbtime));
	    // add the clusts to the appropriate sequence.
	    shsp.clustSequence(end).insert(clust);
	    // if required, add a 'ghost' copy of this clust
	    addGhosts(clust,shsp.clustSequence(end));
	  }
	}
      }
    }

    void StrawDigisFromStepPointMCs::divideStep(StepPointMC const& step, vector<IonCluster>& clusters) {
    // get particle charge
      double charge(0.0);
      GlobalConstantsHandle<ParticleDataTable> pdt;
      if(pdt->particle(step.simParticle()->pdgId()).isValid()){
	charge = pdt->particle(step.simParticle()->pdgId()).ref().charge();
      }
	// if the step length is small compared to the mean free path, or this is an
	// uncharged particle, put all the energy in a single cluster
      if (charge == 0.0 || step.stepLength() < _strawphys->meanFreePath()){
	double cen = step.ionizingEdep();
	double fne = cen/_strawphys->meanElectronEnergy();
	unsigned ne = std::max( static_cast<unsigned>(_randP(fne)),(unsigned)1);
	double qc = _strawphys->ionizationCharge(ne);
	IonCluster cluster(step.position(),qc,cen,ne);
	clusters.push_back(cluster);
      } else {
	// use beta-gamma to decide if this is a min-ion particle or not
	bool minion(false);
	if(pdt->particle(step.simParticle()->pdgId()).isValid()){
	  double mass = pdt->particle(step.simParticle()->pdgId()).ref().mass();
	  double mom = step.momentum().mag();
	  // approximate pt
	  double apt = step.momentum().perpPart(_bdir).mag();
	  double bg = mom/mass; // beta x gamma
	  minion = bg > _bgcut && apt > _ptmin;
	}
	// get tracker information
	const Tracker& tracker = getTrackerOrThrow();
	const Straw& straw = tracker.getStraw(step.strawIndex());
	// compute the number of clusters for this step from the mean free path
	double fnc = step.stepLength()/_strawphys->meanFreePath();
	// use a truncated Poisson distribution; this keeps both the mean and variance physical
	unsigned nc = std::max(static_cast<unsigned>(_randP.fire(fnc)),(unsigned)1);
	if(!minion)nc = std::min(nc,_maxnclu);
	// require clusters not exceed the energy sum required for single-electron clusters
	nc = std::min(nc,static_cast<unsigned>(floor(step.ionizingEdep()/_strawphys->ionizationEnergy((unsigned)1))));
	// generate random positions for the clusters
	std::vector<Hep3Vector> cpos(nc);
	fillClusterPositions(straw,step,cpos);
	// generate electron counts and energies for these clusters: minion model is more detailed 
	std::vector<unsigned> ne(nc);
	std::vector<double> cen(nc);
	if(minion){
	  fillClusterMinion(step,ne,cen);
	} else {
	  // get Poisson distribution of # of electrons for the average energy
	  double fne = step.ionizingEdep()/(nc*_strawphys->meanElectronEnergy()); // average # of electrons/cluster for non-minion clusters
	  for(unsigned ic=0;ic<nc;++ic){
	    ne[ic] = static_cast<unsigned>(std::max(_randP.fire(fne),(long)1));
	    cen[ic] = ne[ic]*_strawphys->meanElectronEnergy(); // average energy per electron, works for large numbers of electrons
	  }
	}
	// create the cluster objects
	for(unsigned ic=0;ic<nc;++ic){
	  double qc = _strawphys->ionizationCharge(ne[ic]);
	  IonCluster cluster(cpos[ic],qc,cen[ic],ne[ic]);
	  clusters.push_back(cluster);
	}
      }
      // diagnostics
      if(_diag > 0){
	_steplen = step.stepLength();
	_stepE = step.ionizingEdep();
	_partP = step.momentum().mag();
	_partPDG = step.simParticle()->pdgId();
	_nclusd = (int)clusters.size();
	_netot = 0;
	_qsum = _esum = _eesum = 0.0;
	for(auto iclust=clusters.begin();iclust != clusters.end();++iclust){
	  _netot += iclust->_ne;
	  _qsum += iclust->_charge;
	  _esum += iclust->_eion;
	  _eesum += _strawphys->meanElectronEnergy()*iclust->_ne;
	}
	_qe = _strawphys->ionizationEnergy(_qsum);
	_sdiag->Fill();
      }
    }

    void StrawDigisFromStepPointMCs::driftCluster(Straw const& straw,
	IonCluster const& cluster, WireCharge& wireq) {
      // Compute the vector from the cluster to the wire
      Hep3Vector cpos = cluster._pos-straw.getMidPoint();
      // drift distance perp to wire, and angle WRT magnetic field (for Lorentz effect)
      double dd = min(cpos.perp(straw.getDirection()),straw.getDetail().innerRadius());
      // for now ignore Lorentz effects FIXME!!!
      double dphi = 0.0;
      // sample the gain for this cluster 
      double gain = _strawphys->clusterGain(_randgauss, _randflat, cluster._ne);
      wireq._charge = cluster._charge*(gain);
      // compute drift time for this cluster
      double dt = _strawphys->driftDistanceToTime(dd,dphi);
      double dtsig = _strawphys->driftTimeSpread(dt);
      wireq._time = _randgauss.fire(dt,dtsig);
      wireq._dd = dd;
      // position along wire
      // need to add Lorentz effects, this should be in StrawPhysics, FIXME!!!
      wireq._wpos = cpos.dot(straw.getDirection());
    }

    void StrawDigisFromStepPointMCs::propagateCharge(Straw const& straw,
	WireCharge const& wireq, StrawEnd end, WireEndCharge& weq) {
      // compute distance to the appropriate end
      double wlen = straw.getDetail().halfLength(); // use the full length, not the active length
      // NB: the following assumes the straw direction points in increasing azimuth.  FIXME!
      if(end == TrkTypes::hv)
	weq._wdist = wlen - wireq._wpos;
      else
	weq._wdist = wlen + wireq._wpos;
      // split the charge
      weq._charge = 0.5*wireq._charge;
      weq._time = _strawphys->propagationTime(weq._wdist);
    }

    double StrawDigisFromStepPointMCs::microbunchTime(double globaltime) const {
      // fold time relative to MB frequency
      return fmod(globaltime,_mbtime);
    }

    void StrawDigisFromStepPointMCs::addGhosts(StrawCluster const& clust,StrawClusterSequence& shs) {
      // add enough buffer to cover both the flash blanking and the ADC waveform
      if(clust.time() < _strawele->flashStart()+_mbbuffer)
	shs.insert(StrawCluster(clust,_mbtime));
    }

    void StrawDigisFromStepPointMCs::findThresholdCrossings( SWFP const& swfp, WFXList& xings){
      //randomize the threshold to account for electronics noise; this includes parts that are coherent
      // for both ends (coming from the straw itself)
      // Keep track of crossings on each end to keep them in sequence
      std::vector<double> thresh;
      thresh.push_back(_randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh)));
      // loop over the ends of this straw
      for(size_t iend=0;iend<2;++iend){
	size_t icross(0);
	// start when the electronics becomes enabled:
	WFX wfx(swfp[iend],_strawele->flashEnd());
	// add incoherent noise. 
	double threshold = _randgauss.fire(thresh[icross],_strawele->thresholdNoise()); 
	// iterate sequentially over clusts inside the sequence.  Note we fold
	// the flash blanking to AFTER the end of the microbunch
	// keep these in time-order
	while( wfx._time < _mbtime+_strawele->flashStart() &&
	  swfp[iend].crossesThreshold(threshold,wfx) ){
	  auto iwfxl = xings.begin();
	  while(iwfxl != xings.end() && iwfxl->_time < wfx._time)
	    ++iwfxl;
	  xings.insert(iwfxl,wfx);
	  // insure a minimum time buffer between crossings
	  wfx._time += _strawele->deadTimeAnalog();
	  if(wfx._time >_mbtime+_strawele->flashStart())
	    break;
	  // skip to the next clust
	  ++(wfx._iclust);
	  // update threshold
	  ++icross;
	  if(icross >= thresh.size())
	    thresh.push_back(_randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh)));
	  threshold = _randgauss.fire(thresh[icross],_strawele->thresholdNoise()); 
	}
      }
    }

    void StrawDigisFromStepPointMCs::fillDigis(WFXList const& xings, SWFP const& wf,
	StrawIndex index,
	StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	PtrStepPointMCVectorCollection* mcptrs ){
      // loop over crossings
      auto first_wfx = xings.begin();
      while (first_wfx != xings.end()){
	auto second_wfx = first_wfx; ++second_wfx;
	bool found_coincidence = false;
	while (second_wfx != xings.end()){
	  // if we are past coincidence window, stop
	  if (!_strawele->combineEnds(first_wfx->_time,second_wfx->_time))
	    break;
	  // if these are crossings on different ends of the straw
	  if (first_wfx->_iclust->strawEnd() != second_wfx->_iclust->strawEnd())
	  {
	    found_coincidence = true;
	    WFXP xpair;
	    xpair[first_wfx->_iclust->strawEnd()] = first_wfx;
	    xpair[second_wfx->_iclust->strawEnd()] = second_wfx;
	    // create a digi from this pair
	    createDigi(xpair,wf,index,digis);
	    // fill associated MC truth matching. Only count the same step once
	    set<art::Ptr<StepPointMC> > xmcsp;
	    double wetime[2] = {-100.,-100.};
	    CLHEP::HepLorentzVector cpos[2];
	    art::Ptr<StepPointMC> stepMC[2];

	    for (size_t iend=0;iend<2;++iend){
	      StrawCluster const& sc = *(xpair[iend]->_iclust);
	      xmcsp.insert(sc.stepPointMC());
	      wetime[iend] = sc.time();
	      cpos[iend] = sc.clusterPosition();
	      stepMC[iend] = sc.stepPointMC();
	    }
	    // choose the minimum time from either end, as the ADC sums both
	    double ptime = 1.0e10;
	    for (size_t iend=0;iend<2;++iend){
	      if (wetime[iend] > 0)
		ptime = std::min(ptime,wetime[iend]);
	    }
	    // subtract a small buffer
	    ptime -= 0.01*_strawele->adcPeriod();
	    // pickup all StepPointMCs associated with clusts inside the time window of the ADC digitizations (after the threshold)
	    set<art::Ptr<StepPointMC> > spmcs;
	    for (auto ih=wf[0].clusts().clustList().begin();ih!=wf[0].clusts().clustList().end();++ih){
	      if (ih->time() >= ptime && ih->time() < ptime +
		  ( _strawele->nADCSamples()-_strawele->nADCPreSamples())*_strawele->adcPeriod())
		spmcs.insert(ih->stepPointMC());
	    }
	    vector<art::Ptr<StepPointMC> > stepMCs;
	    stepMCs.reserve(spmcs.size());
	    for(auto ispmc=spmcs.begin(); ispmc!= spmcs.end(); ++ispmc){
	      stepMCs.push_back(*ispmc);
	    }
	    PtrStepPointMCVector mcptr;
	    for(auto ixmcsp=xmcsp.begin();ixmcsp!=xmcsp.end();++ixmcsp)
	      mcptr.push_back(*ixmcsp);
	    mcptrs->push_back(mcptr);
	    mcdigis->push_back(StrawDigiMC(index,wetime,cpos,stepMC,stepMCs));
	    // diagnostics
	    if(_diag > 1)digiDiag(wf,xpair,digis->back(),mcdigis->back());
	    // we are done looking for a coincidence with this first wfx
	    break;
	  }

	  ++second_wfx;
	}

	// now find next crossing to start looking from for coincidence
	if (found_coincidence){
	  first_wfx = second_wfx;
	  ++first_wfx;
	  // move past deadtime for event readout
	  while (first_wfx != xings.end()){
	    if (first_wfx->_time > second_wfx->_time + _strawele->deadTimeDigital())
	      break;
	    ++first_wfx;
	  }
	}else{
	  // analog deadtime between non triggering crossings enforced when finding crossings
	  ++first_wfx;
	}
      }
    }

    void StrawDigisFromStepPointMCs::createDigi(WFXP const& xpair, SWFP const& waveform,
	StrawIndex index, StrawDigiCollection* digis){
      // storage for MC match can be more than 1 StepPointMCs
      set<art::Ptr<StepPointMC>> mcmatch;
      // initialize the float variables that we later digitize
      TDCTimes xtimes = {0.0,0.0}; 
      TrkTypes::TOTValues tot;
      // get the ADC sample times from the electroincs.  Use the cal side time to randomize
      // the phase, this doesn't really matter
      TrkTypes::ADCTimes adctimes;
      _strawele->adcTimes(xpair[TrkTypes::cal]->_time,adctimes);
      //  sums voltages from both waveforms for ADC
      ADCVoltages wf[2];
      // smear (coherently) both times for the TDC clock jitter
      double dt = _randgauss.fire(0.0,_strawele->clockJitter());
      // loop over the associated crossings
      for(size_t iend = 0;iend<2; ++iend){
	WFX const& wfx = *xpair[iend];
	// record the crossing time for this end, including clock jitter  These already include noise effects
	xtimes[iend] = wfx._time+dt;
	// record MC match if it isn't already recorded
	mcmatch.insert(wfx._iclust->stepPointMC());
	// randomize threshold.  This assumes the noise on each end is incoherent.  A better model
	// would have separate coherent + incoherent components, FIXME!
	double threshold = _randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh));
	// find TOT
	tot[iend] = waveform[iend].digitizeTOT(threshold,wfx._time + dt);
	// sample ADC
	waveform[iend].sampleADCWaveform(adctimes,wf[iend]);
      }
      // add ends and add noise
      ADCVoltages wfsum; wfsum.reserve(adctimes.size());
      for(unsigned isamp=0;isamp<adctimes.size();++isamp){
	wfsum.push_back(wf[0][isamp]+wf[1][isamp]+_randgauss.fire(0.0,_strawele->analogNoise(TrkTypes::adc)));
      }
      // digitize
      TrkTypes::ADCWaveform adc;
      _strawele->digitizeWaveform(wfsum,adc);
      TrkTypes::TDCValues tdc;
      _strawele->digitizeTimes(xtimes,tdc);
      // create the digi from this
      digis->push_back(StrawDigi(index,tdc,tot,adc));
    }

    // find straws which couple to the given one, and record them and their couplings in XTalk objects.
    // For now, this is just a fixed number for adjacent straws,
    // the couplings and straw identities should eventually come from a database, FIXME!!!
    void StrawDigisFromStepPointMCs::findCrossTalkStraws(Straw const& straw, vector<XTalk>& xtalk) {
      StrawIndex selfind = straw.index();
      xtalk.clear();
      // find straws sensitive to straw-to-straw cross talk
      vector<StrawIndex> const& strawNeighbors = straw.nearestNeighboursByIndex();
      // find straws sensitive to electronics cross talk
      vector<StrawIndex> const& preampNeighbors = straw.preampNeighboursByIndex();
      // convert these to cross-talk
      for(auto isind=strawNeighbors.begin();isind!=strawNeighbors.end();++isind){
	xtalk.push_back(XTalk(selfind,*isind,_preampxtalk,0));
      }
      for(auto isind=preampNeighbors.begin();isind!=preampNeighbors.end();++isind){
	xtalk.push_back(XTalk(selfind,*isind,0,_postampxtalk));
      }
    }

    // functions that need implementing:: FIXME!!!!!!
    void StrawDigisFromStepPointMCs::addNoise(StrawClusterMap& hmap){
      // create random noise clusts and add them to the sequences of random straws.
    }
    // diagnostic functions
    void StrawDigisFromStepPointMCs::waveformHist(SWFP const& wfs, WFXList const& xings) {
      // histogram individual waveforms
      static unsigned nhist(0);// maximum number of histograms per job!
      for(size_t iend=0;iend<2;++iend){
	if(nhist < _maxhist && xings.size() >= _minnxinghist &&
	    ( ((!_xtalkhist) && wfs[iend].xtalk().self()) || (_xtalkhist && !wfs[iend].xtalk().self()) ) ) {
	  ClusterList const& clist = wfs[iend].clusts().clustList();
	  double tstart = clist.begin()->time()-_tstep;
	  double tfall = _strawele->fallTime(_diagpath);
	  double tend = clist.rbegin()->time() + _nfall*tfall;
	  ADCTimes times;
	  ADCVoltages volts;
	  times.reserve(size_t(ceil(tend-tstart)/_tstep));
	  volts.reserve(size_t(ceil(tend-tstart)/_tstep));
	  double t = tstart;
	  while(t<tend){
	    times.push_back(t);
	    volts.push_back(wfs[iend].sampleWaveform(_diagpath,t));
	    t += _tstep;
	  }
	  ++nhist;
	  art::ServiceHandle<art::TFileService> tfs;
	  char name[60];
	  char title[100];
	  snprintf(name,60,"SWF%i_%i",wfs[iend].clusts().strawIndex().asInt(),nhist);
	  snprintf(title,100,"Electronic output for straw %i end %i path %i;time (nSec);Waveform (mVolts)",wfs[iend].clusts().strawIndex().asInt(),(int)iend,_diagpath);
	  TH1F* wfh = tfs->make<TH1F>(name,title,volts.size(),times.front(),times.back());
	  for(size_t ibin=0;ibin<times.size();++ibin)
	    wfh->SetBinContent(ibin+1,volts[ibin]);
	  TList* flist = wfh->GetListOfFunctions();
	  for(auto ixing=xings.begin();ixing!=xings.end();++ixing){
	    if(ixing->_iclust->strawEnd() == wfs[iend].strawEnd()){
	      TMarker* smark = new TMarker(ixing->_time,ixing->_vcross,8);
	      smark->SetMarkerColor(kGreen);
	      smark->SetMarkerSize(2);
	      flist->Add(smark);
	    }
	  }
	  _waveforms.push_back(wfh);
	}
      }
    }

    void StrawDigisFromStepPointMCs::waveformDiag(SWFP const& wfs, WFXList const& xings) {
      const Tracker& tracker = getTrackerOrThrow();
      const Straw& straw = tracker.getStraw( wfs[0].clusts().strawIndex() );
      _swplane = straw.id().getPlane();
      _swpanel = straw.id().getPanel();
      _swlayer = straw.id().getLayer();
      _swstraw = straw.id().getStraw();
      for(size_t iend=0;iend<2; ++iend){
	ClusterList const& clusts = wfs[iend].clusts().clustList();
	size_t nclust = clusts.size();
	set<art::Ptr<StepPointMC> > steps;
	set<art::Ptr<SimParticle> > parts;
	_nxing[iend] = 0;
	_txing[iend] = -100.0;
	_xddist[iend] = _xwdist[iend] = _xpdist[iend] = -1.0;
	for(auto ixing=xings.begin();ixing!=xings.end();++ixing){
	  if(ixing->_iclust->strawEnd() == iend) {
	    ++_nxing[iend];
	    _txing[iend] = min(_txing[iend],static_cast<float_t>(ixing->_time));
	    _xddist[iend] = ixing->_iclust->driftDistance();
	    _xwdist[iend] = ixing->_iclust->wireDistance();
	    // compute direction perpendicular to wire and momentum
	    art::Ptr<StepPointMC> const& spp = ixing->_iclust->stepPointMC();
	    if(!spp.isNull()){
	      Hep3Vector pdir = straw.getDirection().cross(spp->momentum()).unit();
	      // project the differences in position to get the perp distance
	      _xpdist[iend] = pdir.dot(spp->position()-straw.getMidPoint());
	    }
	  }
	}
	if(_nxing[iend] == 0){
	  // no xings: just take the 1st clust
	  if(nclust > 0 ){
	    _xddist[iend] = clusts.front().driftDistance();
	    _xwdist[iend] = clusts.front().wireDistance();
	    art::Ptr<StepPointMC> const& spp = clusts.front().stepPointMC();
	    if(!spp.isNull()){
	      Hep3Vector pdir = straw.getDirection().cross(spp->momentum()).unit();
	      // project the differences in position to get the perp distance
	      _xpdist[iend] = pdir.dot(spp->position()-straw.getMidPoint());
	    }
	  }
	}
	if(nclust > 0){
	  _tmin[iend] = clusts.begin()->time();
	  _tmax[iend] = clusts.rbegin()->time();
	} else {
	  _tmin[iend] = _mbtime+_mbbuffer;
	  _tmax[iend] = -100.0;
	}

	_hqsum[iend] = 0.0;
	_vmax[iend] = _tvmax[iend] = 0.0;
	_wmcpdg[iend] = _wmcproc[iend] = 0;
	for(auto iclu=clusts.begin();iclu!=clusts.end();++iclu){
	  if(iclu->stepPointMC().isNonnull()){
	    steps.insert(iclu->stepPointMC());
	    parts.insert(iclu->stepPointMC()->simParticle());
	    _hqsum[iend] += iclu->charge();
	    double ctime = iclu->time()+_strawele->maxResponseTime(_diagpath,iclu->wireDistance());
	    double vout = wfs[iend].sampleWaveform(_diagpath,ctime);
	    if(vout > _vmax[iend]){
	      _vmax[iend] = vout;
	      _tvmax[iend] = ctime;
	      _wmcpdg[iend] = iclu->stepPointMC()->simParticle()->pdgId();
	      _wmcproc[iend] = iclu->stepPointMC()->simParticle()->creationCode();
	      _mce[iend] = iclu->stepPointMC()->simParticle()->startMomentum().e();
	      _slen[iend] = iclu->stepPointMC()->stepLength();
	      _sedep[iend] = iclu->stepPointMC()->ionizingEdep();
	    }
	  }
	}
	_nsteppoint[iend] = steps.size();
	_npart[iend] = parts.size();
	_sesum[iend] = 0.0;
	for(auto istep=steps.begin();istep!=steps.end();++istep)
	  _sesum [iend]+= (*istep)->ionizingEdep();
      }
      _swdiag->Fill();
    }

    void StrawDigisFromStepPointMCs::digiDiag(SWFP const& wfs, WFXP const& xpair, StrawDigi const& digi,StrawDigiMC const& mcdigi) {
      const Tracker& tracker = getTrackerOrThrow();
      const Straw& straw = tracker.getStraw( digi.strawIndex() );
      _sdplane = straw.id().getPlane();
      _sdpanel = straw.id().getPanel();
      _sdlayer = straw.id().getLayer();
      _sdstraw = straw.id().getStraw();

      for(size_t iend=0;iend<2;++iend){
	_xtime[iend] = xpair[iend]->_time;
	_tctime[iend] = xpair[iend]->_iclust->time();
	_charge[iend] = xpair[iend]->_iclust->charge();
	_ddist[iend] = xpair[iend]->_iclust->driftDistance();
	_wdist[iend] = xpair[iend]->_iclust->wireDistance();
	_vstart[iend] = xpair[iend]->_vstart;
	_vcross[iend] = xpair[iend]->_vcross;
	_tdc[iend] = digi.TDC(xpair[iend]->_iclust->strawEnd());
	_tot[iend] = digi.TOT(xpair[iend]->_iclust->strawEnd());
	ClusterList const& clist = wfs[iend].clusts().clustList();
	auto ctrig = xpair[iend]->_iclust;
	_ncludd[iend] = clist.size();
	// find the earliest cluster from the same particle that triggered the crossing
	auto iclu = clist.begin();
	while( iclu != clist.end() && ctrig->stepPointMC()->simParticle() != iclu->stepPointMC()->simParticle() ){
	  ++iclu;
	}
	if(iclu != clist.end() ){
	  _ectime[iend] = iclu->time();
	  _cdist[iend] = iclu->driftDistance();
	  // count how many clusters till we get to the trigger cluster
	  size_t iclust(0);
	  while( iclu != clist.end() && iclu != ctrig){
	    ++iclu;
	    ++iclust;
	  }
	  _iclust[iend] = iclust;
	}
      }
      if(xpair[0]->_iclust->stepPointMC() == xpair[1]->_iclust->stepPointMC())
	_nstep = 1;
      else
	_nstep = 2;
      _adc.clear();
      for(auto iadc=digi.adcWaveform().begin();iadc!=digi.adcWaveform().end();++iadc){
	_adc.push_back(*iadc);
      }
      // mc truth information
      _dmcpdg = _dmcproc = _dmcgen = 0;
      _dmcmom = -1.0;
      _mctime = _mcenergy = _mctrigenergy = _mcthreshenergy = _mcdca = -1000.0;
      _mcthreshpdg = _mcthreshproc = _mcnstep = 0;
      art::Ptr<StepPointMC> const& spmc = xpair[0]->_iclust->stepPointMC();
      if(!spmc.isNull()){
	_mctime = _toff.timeWithOffsetsApplied(*spmc);
	// compute the doca for this step
	TwoLinePCA pca( straw.getMidPoint(), straw.getDirection(),
	    spmc->position(), spmc->momentum().unit() );
	_mcdca = pca.dca();
	if(!spmc->simParticle().isNull()){
	  _dmcpdg = spmc->simParticle()->pdgId();
	  _dmcproc = spmc->simParticle()->creationCode();
	  if(spmc->simParticle()->genParticle().isNonnull())
	    _dmcgen = spmc->simParticle()->genParticle()->generatorId().id();
	  _dmcmom = spmc->momentum().mag();
	}
      }
      _mcenergy = mcdigi.energySum();
      _mctrigenergy = mcdigi.triggerEnergySum(TrkTypes::cal);
      // sum the energy from the explicit trigger particle, and find it's releationship
      _mcthreshenergy = 0.0;
      _mcnstep = mcdigi.stepPointMCs().size();
      art::Ptr<StepPointMC> threshpart = mcdigi.stepPointMC(TrkTypes::cal);
      if(threshpart.isNull()) threshpart = mcdigi.stepPointMC(TrkTypes::hv);
      for(auto imcs = mcdigi.stepPointMCs().begin(); imcs!= mcdigi.stepPointMCs().end(); ++ imcs){
	// if the SimParticle for this step is the same as the one which fired the discrim, add the energy
	if( (*imcs)->simParticle() == threshpart->simParticle() )
	  _mcthreshenergy += (*imcs)->eDep();
      }
      _mcthreshpdg = threshpart->simParticle()->pdgId();
      _mcthreshproc = threshpart->simParticle()->creationCode();

      _xtalk = digi.strawIndex() != spmc->strawIndex();
      // fill the tree entry
      _sddiag->Fill();
    }

    void StrawDigisFromStepPointMCs::fillClusterPositions(Straw const& straw, StepPointMC const& step, std::vector<Hep3Vector>& cpos) {
      // basic info
      double charge(0.0);
      GlobalConstantsHandle<ParticleDataTable> pdt;
      if(pdt->particle(step.simParticle()->pdgId()).isValid()){
	charge = pdt->particle(step.simParticle()->pdgId()).ref().charge();
      }
      double r2 = straw.getDetail().innerRadius()*straw.getDetail().innerRadius();
      // decide how we step; straight or helix, depending on the Pt
      Hep3Vector const& mom = step.momentum();
      Hep3Vector mdir = mom.unit();
      // approximate pt
      double apt = step.momentum().perpPart(_bdir).mag();
      if( apt > _ptmin) { // use linear approximation
	// make sure this linear approximation doesn't extend past the physical straw
	Hep3Vector dperp = (step.position() -straw.getMidPoint()).perpPart(straw.getDirection());
	Hep3Vector mperp = mdir.perpPart(straw.getDirection());
	double dm = dperp.dot(mperp);
	double m2 = mperp.mag2();
	double dp2 = dperp.mag2();
	double smax = (-dm + sqrt(dm*dm - m2*(dp2 - r2)))/m2;
	double slen = std::min(smax,step.stepLength());
	// generate a vector of random lengths
	for(unsigned ic=0;ic < cpos.size();++ic){
	  // generate a random cluster position
	  cpos[ic] = step.position() +_randflat.fire(slen) *mdir;
	}
      } else {
	// Use a helix to model particles which curl on the scale of the straws
	GeomHandle<BFieldManager> bfmgr;
	GeomHandle<DetectorSystem> det;
	// find the local field vector at this step
	Hep3Vector vpoint_mu2e = det->toMu2e(step.position());
	Hep3Vector bf = bfmgr->getBField(vpoint_mu2e);
	// compute transverse radius of particle
	double rcurl = fabs(charge*(mom.perpPart(bf).mag())/BField::mmTeslaToMeVc*bf.mag());
	// basis using local Bfield direction
	Hep3Vector bfdir = bf.unit();
	Hep3Vector qmdir = (charge*mom).unit(); // charge-signed momentum direction
	Hep3Vector rdir = qmdir.cross(bfdir).unit(); // points along magnetic force, ie towards center
	Hep3Vector pdir = bfdir.cross(rdir).unit(); // perp to this and field
	// find the center of the helix at the start of this step
	Hep3Vector hcent = step.position() + rcurl*rdir;
	// find helix parameters.  By definition, z0 = phi0 = 0
	double omega = qmdir.dot(pdir)/(rcurl*qmdir.dot(bfdir));
	// compute how far this step goes along the field direction.  This includes sign information
	double zlen = step.stepLength()*mdir.dot(bfdir);
	// loop until we've found enough valid samples, or have given up trying
	unsigned iclu(0);
	unsigned ntries(0);
	unsigned nclus = cpos.size();
	while(iclu < nclus && ntries < 10*nclus){
	  double zclu = _randflat.fire(zlen);
	  double phi = zclu*omega;
	  // build cluster position from these
	  Hep3Vector cp = hcent + rcurl*(-rdir*cos(phi) + pdir*sin(phi)) + zclu*bfdir;
	  // test
	  double rd2 = (cp-straw.getMidPoint()).perpPart(straw.getDirection()).mag2();
	  if(rd2 - r2 < 1.0e-3){
	    cpos[iclu] = cp;
	    ++iclu;
	  } else if (_debug > 0) {
	    cout << "cluster outside straw: helix " <<  sqrt(rd2) << endl;
	  }
	  ++ntries;
	}
	if(iclu != nclus){
	  // failed to find valid steps. put any remining energy at the step
	  if(_debug > 0)cout << "mu2e::StrawDigisFromStepPointMCs: Couldn't create enough clusters : "<< iclu << " wanted " << nclus << endl;
	  while(iclu < nclus){
	    cpos[iclu] = step.position();
	    ++iclu;
	  }
	}
      }
    }

    void StrawDigisFromStepPointMCs::fillClusterMinion(StepPointMC const& step, std::vector<unsigned>& ne, std::vector<double>& cen) {
      // Loop until we've assigned energy + electrons to every cluster 
      unsigned mc(0);
      double esum(0.0);
      double etot = step.ionizingEdep();
      unsigned nc = ne.size();
      while(mc < nc){
	std::vector<unsigned> me(nc);
	// fill an array of random# of electrons according to the measured distribution.  These are returned sorted lowest-highest.
	fillClusterNe(me);
	for(auto ie : me) {
	  // maximum energy for this cluster requires at least 1 electron for the rest of the cluster
	  double emax = etot - esum - (nc -mc -1)*_strawphys->ionizationEnergy((unsigned)1);
	  double eele = _strawphys->ionizationEnergy(ie);
	  if( eele < emax){
	    ne[mc] = ie;
	    cen[mc] = eele;
	    ++mc;
	    esum += eele;
	  } else {
	    break;
	  }
	}
      }
      // distribute any residual energy randomly to these clusters.  This models delta rays
      unsigned ns;
      do{
	unsigned me = _strawphys->nePerIon(_randflat.fire());
	double emax = etot - esum;
	double eele = _strawphys->ionizationEnergy(me);
	if(eele < emax){
	  // choose a random cluster to assign this energy to
	  unsigned mc = std::min(nc-1,static_cast<unsigned>(floor(_randflat.fire(nc))));
	  ne[mc] += me;
	  cen[mc] += eele;
	  esum += eele;
	}
	// maximum energy for this cluster requires at least 1 electron for the rest of the cluster
	ns = static_cast<unsigned>(floor((etot-esum)/_strawphys->ionizationEnergy((unsigned)1)));
      } while(ns > 0);
    }

    void StrawDigisFromStepPointMCs::fillClusterNe(std::vector<unsigned>& me) {
      for(size_t ie=0;ie < me.size(); ++ie){
	me[ie] = _strawphys->nePerIon(_randflat.fire());
      }
      if(_sort)std::sort(me.begin(),me.end());
    }

  } // end namespace trackermc
} // end namespace mu2e

using mu2e::TrackerMC::StrawDigisFromStepPointMCs;
DEFINE_ART_MODULE(StrawDigisFromStepPointMCs);

