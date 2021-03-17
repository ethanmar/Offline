//
// KinKal fit module using the LoopHelix parameterset
//
// Original author D. Brown (LBNL) 11/18/20
//

// framework
#include "fhiclcpp/ParameterSet.h"
#include "fhiclcpp/types/Atom.h"
#include "fhiclcpp/types/Sequence.h"
#include "fhiclcpp/types/Table.h"
#include "fhiclcpp/types/Tuple.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art_root_io/TFileService.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/Handle.h"
// conditions
#include "GlobalConstantsService/inc/GlobalConstantsHandle.hh"
#include "ProditionsService/inc/ProditionsHandle.hh"
#include "TrackerConditions/inc/StrawResponse.hh"
#include "BFieldGeom/inc/BFieldManager.hh"
#include "HepPDT/ParticleData.hh"
#include "GlobalConstantsService/inc/ParticleDataTable.hh"
// utiliites
#include "GeometryService/inc/GeomHandle.hh"
#include "TrackerGeom/inc/Tracker.hh"
#include "GeometryService/inc/GeometryService.hh"
#include "GeneralUtilities/inc/Angles.hh"
#include "TrkReco/inc/TrkUtilities.hh"
#include "CalorimeterGeom/inc/Calorimeter.hh"
// data
#include "DataProducts/inc/PDGCode.hh"
#include "DataProducts/inc/Helicity.hh"
#include "RecoDataProducts/inc/ComboHit.hh"
#include "RecoDataProducts/inc/StrawHitFlag.hh"
#include "RecoDataProducts/inc/KalSeed.hh"
#include "RecoDataProducts/inc/HelixSeed.hh"
#include "RecoDataProducts/inc/CaloCluster.hh"
#include "RecoDataProducts/inc/KKLoopHelix.hh"
// KinKal
#include "KinKal/Fit/Track.hh"
#include "KinKal/Fit/Config.hh"
#include "KinKal/Trajectory/LoopHelix.hh"
#include "KinKal/Trajectory/ParticleTrajectory.hh"
#include "KinKal/Trajectory/PiecewiseClosestApproach.hh"
#include "KinKal/Detector/StrawMaterial.hh"
#include "KinKal/Detector/StrawXing.hh"
#include "KinKal/MatEnv/MatDBInfo.hh"
#include "KinKal/General/Parameters.hh"
#include "KinKal/Trajectory/Line.hh"
// Mu2eKinKal
#include "Mu2eKinKal/inc/KKFileFinder.hh"
#include "Mu2eKinKal/inc/KKStrawHit.hh"
//#include "Mu2eKinKal/inc/KKPanelHit.hh"
#include "Mu2eKinKal/inc/KKBField.hh"
// root
#include "TH1F.h"
#include "TTree.h"
// C++
#include <iostream>
#include <fstream>
#include <string>
#include <functional>
#include <vector>
#include <memory>
using namespace std;
//using namespace KinKal;
namespace mu2e {
  using KTRAJ= KinKal::LoopHelix;
  using PKTRAJ = KinKal::ParticleTrajectory<KTRAJ>;
//  using TSH = KKStrawHit<KTRAJ>;
//  using TPH = KKPanelHit<KTRAJ>;
  using KKTRK = KinKal::Track<KTRAJ>;
  using MEAS = KinKal::Hit<KTRAJ>;
  using MEASPTR = std::shared_ptr<MEAS>;
  using MEASCOL = std::vector<MEASPTR>;
  using KKSTRAWHIT = KKStrawHit<KTRAJ>;
  using STRAWXING = KinKal::StrawXing<KTRAJ>;
  using EXING = KinKal::ElementXing<KTRAJ>;
  using EXINGPTR = std::shared_ptr<EXING>;
  using EXINGCOL = std::vector<EXINGPTR>;
  using KKEFF = KinKal::Effect<KTRAJ>;
  using KKHIT = KinKal::HitConstraint<KTRAJ>;
  using KKMAT = KinKal::Material<KTRAJ>;
  using KKBF = KinKal::BFieldEffect<KTRAJ>;
  using KinKal::Line;
  using PTCA = KinKal::PiecewiseClosestApproach<KTRAJ,Line>;
  using TCA = KinKal::ClosestApproach<KTRAJ,Line>;
  using MatEnv::MatDBInfo;
  using KKConfig = KinKal::Config;
  using KinKal::DVEC;
  using KinKal::Parameters;
  using KinKal::VEC3;
  using KinKal::TimeRange;
  using KinKal::MetaIterConfig;
  using KinKal::CAHint;
  using KinKal::StrawMaterial;
  using KinKal::WireHitState;
  using KinKal::DMAT;
  using KinKal::Status;
  using HPtr = art::Ptr<HelixSeed>;
  class LoopHelixFit : public art::EDProducer {
    using Name    = fhicl::Name;
    using Comment = fhicl::Comment;

    struct ModuleSettings {
      fhicl::Sequence<art::InputTag> helixSeedCollections         {Name("HelixSeedCollections"),     Comment("Helix seed fit collections to be processed ") };
      fhicl::Atom<art::InputTag>     comboHitCollection     {Name("ComboHitCollection"),     Comment("Single Straw ComboHit collection ") };
      fhicl::Atom<art::InputTag>     strawHitFlagCollection {Name("StrawHitFlagCollection"), Comment("StrawHitFlag collection ") };
      fhicl::Sequence<std::string> helixFlags { Name("HelixFlags"), Comment("Flags required to be present to convert a helix seed to a KinKal track") };
      fhicl::Atom<int> printLevel { Name("PrintLevel"), Comment("Diagnostic printout Level"), 0 };
      fhicl::Atom<bool> saveAll { Name("SaveAllFits"), Comment("Save all fits, whether they suceed or not"),false };
      fhicl::Sequence<float> zsave { Name("ZSavePositions"), Comment("Z positions to sample and save the fit result helices")};
    };

    struct PatRecSettings {
      fhicl::Sequence<std::string> addHitFlags { Name("AddHitFlags"), Comment("Flags required to be present to add a hit"), std::vector<std::string>() };
      fhicl::Sequence<std::string> rejectHitFlags { Name("RejectHitFlags"), Comment("Flags required not to be present to add a hit"), std::vector<std::string>() };
      fhicl::Atom<float> maxAddDOCA { Name("MaxAddDOCA"), Comment("Max DOCA to add a hit (mm)"), 2.75 };
      fhicl::Atom<float> maxAddDt { Name("MaxAddDt"), Comment("Max Detla time to add a hit (ns)"), 3.0 };
      fhicl::Atom<float> maxAddChi { Name("MaxAddChi"), Comment("Max Chi to add a hit"), 4.0 };
      fhicl::Atom<float> maxAddDeltaU { Name("MaxAddDeltaU"), Comment("Max Delta-U to add a hit (mm)"), 10.0 };
    };

    struct MaterialSettings {
      fhicl::Atom<std::string> isotopes { Name("isotopes"), Comment("Filename for istotopes information")};
      fhicl::Atom<std::string> elements { Name("elements"), Comment("Filename for elements information") };
      fhicl::Atom<std::string> materials { Name("materials"), Comment("Filename for materials information") };
      fhicl::Atom<std::string> strawGasMaterialName{ Name("strawGasMaterialName"), Comment("strawGasMaterialName") };
      fhicl::Atom<std::string> strawWallMaterialName{ Name("strawWallMaterialName"), Comment("strawWallMaterialName") };
      fhicl::Atom<std::string> strawWireMaterialName{ Name("strawWireMaterialName"), Comment("strawWireMaterialName") };
      fhicl::Atom<double> dahlLynchScatteringFraction{ Name("dahlLynchScatteringFraction"), Comment("dahlLynchScatteringFraction") };
    };

    struct FitSettings {
      fhicl::Atom<int> fitParticle {  Name("FitParticle"), Comment("Particle type to fit: e-, e+, mu-, ..."), PDGCode::e_minus};
      fhicl::Atom<int> fitDirection { Name("FitDirection"), Comment("Particle direction to fit, either upstream or downstream"), TrkFitDirection::downstream };
      fhicl::Atom<int> maxniter { Name("MaxNIter"), Comment("Maximum number of algebraic iteration steps in each fit meta-iteration"), 10 };
      fhicl::Atom<float> dwt { Name("Deweight"), Comment("Deweighting factor when initializing the track end parameters"), 1.0e6 };
      fhicl::Atom<float> tBuffer { Name("TimeBuffer"), Comment("Time buffer for final fit (ns)"), 0.2 };
      fhicl::Atom<float> btol { Name("BCorrTolerance"), Comment("Tolerance on BField correction accuracy (mm)"), 0.01 };
      fhicl::Sequence<float> seederrors { Name("SeedErrors"), Comment("Initial value of seed parameter errors (rms, various units)") };
      fhicl::Atom<int> bfieldCorr { Name("BFieldCorrection"), Comment("BField correction algorithm") };
      fhicl::Atom<int> minndof { Name("MinNDOF"), Comment("Minimum number of Degrees of Freedom to conitnue fitting"), 5  };
      fhicl::Atom<int> printLevel { Name("PrintLevel"), Comment("Internal fit print level"),0};
      fhicl::Atom<int> nullHitDimension { Name("NullHitDimension"), Comment("Null hit constrain dimension"), 2 }; 
      fhicl::Atom<float> tPOCAPrec { Name("TPOCAPrecision"), Comment("Precision for TPOCA calculation (ns)"), 1e-5 };
      fhicl::Atom<bool> addMaterial { Name("AddMaterial"), Comment("Add material effects to the fit"), true }; 
    };

    using MetaIterationSettings = fhicl::Sequence<fhicl::Tuple<float,float,float>>;
    using StrawHitUpdateSettings = fhicl::Sequence<fhicl::Tuple<float,float,unsigned,unsigned>>;
    struct ModuleConfig {
      fhicl::Table<ModuleSettings> modsettings { Name("ModuleSettings") };
      fhicl::Table<PatRecSettings> patrecsettings { Name("PatRecSettings") };
      fhicl::Table<FitSettings> fitsettings { Name("FitSettings") };
      fhicl::Table<MaterialSettings> matsettings { Name("MaterialSettings") };
      MetaIterationSettings mconfig { Name("MetaIterationSettings"), Comment("MetaIteration sequence configuration parameters, format: \n"
      " 'Temperature (dimensionless)', Delta chisquared/DOF for convergence', 'Delta chisquared/DOF for divergence'") };
      StrawHitUpdateSettings shuconfig { Name("StrawHitUpdateSettings"), Comment("Setting sequence for updating StrawHits, format: \n"
      " 'MinDoca', 'MaxDoca', First Meta-iteration', 'Last Meta-iteration'") };
    };
    using ModuleParams = art::EDProducer::Table<ModuleConfig>;

    public:
    explicit LoopHelixFit(const ModuleParams& config);
    virtual ~LoopHelixFit();
    void beginRun(art::Run& aRun) override;
    void produce(art::Event& event) override;
    private:
    // utility functions
    KalSeed createSeed(KKTRK const& ktrk,HPtr const& hptr) const;
    double zTime(PKTRAJ const& trak, double zpos) const; // find the time the trajectory crosses the plane perp to z at the given z position
    // data payload
    std::vector<art::ProductToken<HelixSeedCollection>> hseedCols_;
    art::ProductToken<ComboHitCollection> chcol_T_;
    art::ProductToken<StrawHitFlagCollection> shfcol_T_;
    TrkFitFlag goodhelix_;
    bool saveall_;
    std::vector<float> zsave_;
    TrkFitDirection tdir_;
    PDGCode::type tpart_;
    ProditionsHandle<StrawResponse> strawResponse_h_;
    ProditionsHandle<Tracker> alignedTracker_h_;
    int print_;
    float maxDoca_, maxDt_, maxChi_, maxDU_, tbuff_, tpocaprec_;
    WireHitState::Dimension nulldim_; 
    bool addmat_;
    KKFileFinder filefinder_;
    std::string wallmatname_, gasmatname_, wirematname_;
    std::unique_ptr<StrawMaterial> smat_; // straw material
    KKConfig kkconfig_; // KinKal fit configuration
    DMAT seedcov_; // seed covariance matrix
  };

  LoopHelixFit::LoopHelixFit(const ModuleParams& config) : art::EDProducer{config}, 
    chcol_T_(consumes<ComboHitCollection>(config().modsettings().comboHitCollection())),
    shfcol_T_(mayConsume<StrawHitFlagCollection>(config().modsettings().strawHitFlagCollection())),
    goodhelix_(config().modsettings().helixFlags()),
    saveall_(config().modsettings().saveAll()),
    zsave_(config().modsettings().zsave()),
    tdir_(static_cast<TrkFitDirection::FitDirection>(config().fitsettings().fitDirection())), tpart_(static_cast<PDGCode::type>(config().fitsettings().fitParticle())),
    print_(config().modsettings().printLevel()),
    maxDoca_(config().patrecsettings().maxAddDOCA()),
    maxDt_(config().patrecsettings().maxAddDt()),
    maxChi_(config().patrecsettings().maxAddChi()),
    maxDU_(config().patrecsettings().maxAddDeltaU()),
    tbuff_(config().fitsettings().tBuffer()),
    tpocaprec_(config().fitsettings().tPOCAPrec()),
    nulldim_(static_cast<WireHitState::Dimension>(config().fitsettings().nullHitDimension())),
    addmat_(config().fitsettings().addMaterial()),
    filefinder_(config().matsettings().elements(),config().matsettings().isotopes(),config().matsettings().materials()),
    wallmatname_(config().matsettings().strawWallMaterialName()),
    gasmatname_(config().matsettings().strawGasMaterialName()),
    wirematname_(config().matsettings().strawWireMaterialName())
  {
    // collection handling
    for(const auto& hseedtag : config().modsettings().helixSeedCollections()) { hseedCols_.emplace_back(consumes<HelixSeedCollection>(hseedtag)); }
    produces<KKLoopHelixCollection>();
    produces<KalSeedCollection>();
    // construct the fit configuration object.  This controls all the global and iteration-specific aspects of the fit
    kkconfig_.maxniter_ = config().fitsettings().maxniter();
    kkconfig_.dwt_ = config().fitsettings().dwt();
    kkconfig_.tbuff_ = config().fitsettings().tBuffer();
    kkconfig_.tol_ = config().fitsettings().btol();
    kkconfig_.minndof_ = config().fitsettings().minndof();
    kkconfig_.bfcorr_ = static_cast<KKConfig::BFCorr>(config().fitsettings().bfieldCorr());
    kkconfig_.plevel_ = static_cast<KKConfig::printLevel>(config().fitsettings().printLevel());
    // build the seed covariance
    auto const& seederrors = config().fitsettings().seederrors();
    if(seederrors.size() != KinKal::NParams()) 
      throw cet::exception("RECO")<<"mu2e::LoopHelixFit:Seed error configuration error"<< endl;
    for(size_t ipar=0;ipar < seederrors.size(); ++ipar){
      seedcov_[ipar][ipar] = seederrors[ipar]*seederrors[ipar];
    }
    // Now set the schedule for the meta-iterations
    unsigned nmiter(0);
    for(auto const& misetting : config().mconfig()) {
      MetaIterConfig mconfig;
      mconfig.temp_ = std::get<0>(misetting);
      mconfig.tprec_ = tpocaprec_;
      mconfig.convdchisq_ = std::get<1>(misetting);
      mconfig.divdchisq_ = std::get<2>(misetting);
      mconfig.miter_ = nmiter++;
      kkconfig_.schedule_.push_back(mconfig);
    }
    auto& schedule = kkconfig_.schedule();
    // simple hit updating
    for(auto const& shusetting : config().shuconfig() ) {
      KKStrawHitUpdater shupdater(std::get<0>(shusetting), std::get<1>(shusetting), static_cast<WireHitState::Dimension>(config().fitsettings().nullHitDimension()));
      unsigned minmeta = std::get<2>(shusetting);
      unsigned maxmeta = std::get<3>(shusetting);
      if(maxmeta < minmeta || kkconfig_.schedule_.size() < maxmeta)
	throw cet::exception("RECO")<<"mu2e::LoopHelixFit: Hit updater configuration error"<< endl;
      for(unsigned imeta=minmeta; imeta<=maxmeta; imeta++)
	schedule[imeta].updaters_.push_back(shupdater);
    }
  }

  LoopHelixFit::~LoopHelixFit(){}
  //-----------------------------------------------------------------------------
  void LoopHelixFit::beginRun(art::Run& run) {
    // initialize material access
    GeomHandle<Tracker> tracker_h;
    auto const& sprop = tracker_h->strawProperties();
    MatDBInfo matdbinfo(filefinder_);
    smat_ = std::make_unique<StrawMaterial>(
	sprop._strawOuterRadius, sprop._strawWallThickness, sprop._wireRadius,
	matdbinfo.findDetMaterial(wallmatname_),
	matdbinfo.findDetMaterial(gasmatname_),
	matdbinfo.findDetMaterial(wirematname_));
  }

  void LoopHelixFit::produce(art::Event& event ) {
    // find current proditions
    auto const& ptable = GlobalConstantsHandle<ParticleDataTable>();
    auto const& strawresponse = strawResponse_h_.getPtr(event.id());
    auto const& tracker = alignedTracker_h_.getPtr(event.id()).get();
    GeomHandle<BFieldManager> bfmgr;
    GeomHandle<DetectorSystem> det;
    KKBField kkbf(*bfmgr,*det); // move this to beginrun TODO
    // initial WireHitState; this gets overwritten in updating.  Move this to beginrun TODO
    double rstraw = tracker->strawProperties().strawInnerRadius();
    static const double invsqrt12(1.0/sqrt(12.0));
    // initialize hits as null (no drift)  Move this to beginrun TODO
    WireHitState whstate(WireHitState::null, nulldim_, rstraw*invsqrt12,0.5*rstraw/strawresponse->driftConstantSpeed()); 
    // find input hits
    auto ch_H = event.getValidHandle<ComboHitCollection>(chcol_T_);
    auto const& chcol = *ch_H;;
    // create output
    unique_ptr<KKLoopHelixCollection> kktrkcol(new KKLoopHelixCollection );
    unique_ptr<KalSeedCollection> kkseedcol(new KalSeedCollection );
    // find the seed collections
    for (auto const& hseedtag : hseedCols_) {
      auto const& hseedcol_h = event.getValidHandle<HelixSeedCollection>(hseedtag);
      auto const& hseedcol = *hseedcol_h;
      // loop over the seeds
      for(size_t iseed=0; iseed < hseedcol.size(); ++iseed) {
	auto const& hseed = hseedcol[iseed];
  	auto hptr = HPtr(hseedcol_h,iseed);

	if(hseed.status().hasAllProperties(goodhelix_)){
	  // create a PKTRAJ from the helix fit result, to seed the KinKal fit.  First, translate the parameters
	  // Note the flips in case of backwards propagation
	  auto const& shelix = hseed.helix();
	  auto const& hhits = hseed.hits();
	  DVEC pars;
	  pars[KTRAJ::rad_] = shelix.radius(); // translation should depend on fit direction and charge FIXME!
	  pars[KTRAJ::lam_] = shelix.lambda();
	  pars[KTRAJ::cx_] = shelix.centerx();
	  pars[KTRAJ::cy_] = shelix.centery();
	  pars[KTRAJ::phi0_] = shelix.fz0()+M_PI_2; // convention difference for phi0: not sure if this is fixable. TODO
	  pars[KTRAJ::t0_] = hseed.t0().t0();
	  if(tdir_ == TrkFitDirection::upstream){
	    pars[KTRAJ::rad_] *= -1.0;
	    pars[KTRAJ::lam_] *= -1.0;
	  }
	  // create the initial trajectory
	  Parameters kkpars(pars,seedcov_);
	  // compute the magnetic field at the center.  We only want the z compontent, as the helix fit assumes B points along Z
	  float zcent = 0.5*(hhits.front().pos().Z()+hhits.back().pos().Z());
	  VEC3 center(shelix.centerx(), shelix.centery(),zcent);
	  auto bcent = kkbf.fieldVect(center);
	  VEC3 bnom(0.0,0.0,bcent.Z());
	  // now compute kinematics
	  double mass = ptable->particle(tpart_).ref().mass().value(); 
	  int charge = static_cast<int>(ptable->particle(tpart_).ref().charge());
	  // construct individual KKHit objects from each Helix hit
	  // first, we need to unwind the straw combination
	  std::vector<StrawHitIndex> strawHitIdxs;
	  float tmin = std::numeric_limits<float>::max();
	  float tmax = std::numeric_limits<float>::min();
	  for(size_t ihit = 0; ihit < hhits.size(); ++ihit ){
	    hhits.fillStrawHitIndices(event,ihit,strawHitIdxs);
	    auto const& hhit = hhits[ihit];
	    tmin = std::min(tmin,hhit.correctedTime());
	    tmax = std::max(tmax,hhit.correctedTime());
	  }
	  // buffer the time range
	  TimeRange trange(tmin - tbuff_, tmax + tbuff_);
	  //  construct the seed trajectory
	  KTRAJ seedtraj(kkpars, mass, charge, bnom, trange );
	  PKTRAJ pseedtraj(seedtraj);
	  if(print_ > 1){
	    std::cout << "Seed Helix parameters " << shelix << std::endl;
	    std::cout << "Seed Traj parameters  " << seedtraj << std::endl;
	    std::cout << "Seed Traj t=t0 direction" << seedtraj.direction(seedtraj.t0()) << std::endl
	              << "Seed Helix z-0 direction " << shelix.direction(0.0) << std::endl;
	    std::cout << "Seed Traj t=t0 position " << seedtraj.position3(seedtraj.t0()) << std::endl
	              << "Seed Helix z=0 position " << shelix.position(0.0) << std::endl;
	  }
	  MEASCOL thits; // polymorphic container of hits
	  EXINGCOL exings; // polymorphic container of detector element crossings 
	  // loop over the individual straw hits
	  for(auto strawidx : strawHitIdxs) {
	    const ComboHit& strawhit(chcol.at(strawidx));
	    if(strawhit.mask().level() != StrawIdMask::uniquestraw)
	      throw cet::exception("RECO")<<"mu2e::LoopHelixFit: ComboHit error"<< endl;
	    const Straw& straw = tracker->getStraw(strawhit.strawId());
	    // find the propagation speed for signals along this straw
	    double sprop = 2*strawresponse->halfPropV(strawhit.strawId(),strawhit.energyDep());
	    // construct a kinematic line trajectory from this straw. the measurement point is the signal end
	    auto p0 = straw.wireEnd(strawhit.driftEnd());
	    auto p1 = straw.wireEnd(StrawEnd(strawhit.driftEnd().otherEnd()));
	    auto propdir = (p0 - p1).unit(); // The signal propagates from the far end to the near
	    // clumsy conversion: make this more elegant TODO
	    VEC3 vp0(p0.x(),p0.y(),p0.z());
	    VEC3 vp1(p1.x(),p1.y(),p1.z());
	    Line wline(vp0,vp1,strawhit.time(),sprop);
	    // compute 'hint' to TPOCA.  correct the hit time using the time division
	    double psign = propdir.dot(straw.wireDirection());  // wire distance is WRT straw center, in the nominal wire direction
	    double htime = wline.t0() - (straw.halfLength()-psign*strawhit.wireDist())/wline.speed();
	    CAHint hint(seedtraj.ztime(vp0.Z()),htime);
	    // compute a preliminary PTCA between the seed trajectory and this straw.
	    PTCA ptca(pseedtraj, wline, hint, tpocaprec_);
	    // create the material crossing
	    if(addmat_){
	      exings.push_back(std::make_shared<STRAWXING>(ptca,*smat_));
	      if(print_ > 2)exings.back()->print(std::cout,1);
	    }
	    // create the hit
	    thits.push_back(std::make_shared<KKSTRAWHIT>(kkbf, ptca, whstate, strawhit, straw, *strawresponse));
	    if(print_ > 2)thits.back()->print(std::cout,2);
	  }
	  // add calorimeter cluster hit TODO
	  //	      art::Ptr<CaloCluster> ccPtr;
	  //	      if (kseed.caloCluster()){
	  //	      ccPtr = kseed.caloCluster(); // remember the Ptr for creating the TrkCaloHitSeed and KalSeed Ptr
	  //	      }
	  // create and fit the track.  
	  auto kktrk = make_unique<KKTRK>(kkconfig_,kkbf,seedtraj,thits,exings);
	  if(kktrk->fitStatus().usable() || saveall_){
	  // convert fits into KalSeeds for persistence	
	    kkseedcol->push_back(createSeed(*kktrk,hptr));
	    kktrkcol->push_back(kktrk.release()); // OwningPointerCollection should use unique_ptr FIXME!
	  }
	}
      }
    }

    // put the output products into the event
    event.put(move(kktrkcol));
    event.put(move(kkseedcol));
  }

  KalSeed LoopHelixFit::createSeed(KKTRK const& kktrk,HPtr const& hptr) const {
    TrkFitFlag fflag(hptr->status());
    fflag.merge(TrkFitFlag::KKLoopHelix);
    if(kktrk.fitStatus().usable()) fflag.merge(TrkFitFlag::kalmanOK);
    if(kktrk.fitStatus().status_ == Status::converged) fflag.merge(TrkFitFlag::kalmanConverged);
    // explicit T0 is needed for backwards-compatibility; sample from the appropriate trajectory piece
    auto const& fittraj = kktrk.fitTraj();
    double tz0 = zTime(fittraj,0.0);
    auto const& t0piece = fittraj.nearestPiece(tz0);
    HitT0 t0(t0piece.paramVal(KTRAJ::t0_), sqrt(t0piece.paramVar(KTRAJ::t0_))); 
    // create the shell for the output.  Note the (obsolete) flight length is given as t0
    KalSeed fseed(tpart_,tdir_,t0,t0.t0(),fflag);
    fseed._helix = hptr;
    auto const& fstatus = kktrk.fitStatus();
    fseed._chisq = fstatus.chisq_.chisq();
    fseed._fitcon = fstatus.chisq_.probability();
    // loop over individual effects
    for(auto const& eff: kktrk.effects()) {
      const KKHIT* kkhit = dynamic_cast<const KKHIT*>(eff.get());
      if(kkhit != 0){
	const KKSTRAWHIT* strawhit = dynamic_cast<const KKSTRAWHIT*>(kkhit->hit().get());
	if(strawhit != 0) {
	  auto const& chit = strawhit->hit();
	  StrawHitFlag hflag = chit.flag();
	  if(strawhit->active())hflag.merge(StrawHitFlag::active);
	  auto const& ca = strawhit->closestApproach();
	  TrkStrawHitSeed seedhit(static_cast<StrawHitIndex>(0), // index is obsolete, as are drift radius and drift radius error
	      HitT0(ca.sensorToca(),sqrt(ca.tocaVar())),
	      static_cast<float>(ca.particleToca()), static_cast<float>(ca.sensorToca()),
	      static_cast<float>(-1.0), static_cast<float>(strawhit->time()),
	      static_cast<float>(ca.doca()), strawhit->hitState().lrambig_,static_cast<float>(-1.0), hflag, chit);
	  fseed._hits.push_back(seedhit);
	} else {
	// see if this is a TrkCaloHit
	  //const TrkCaloHit* tch = TrkUtilities::findTrkCaloHit(kktrk); TODO
//    if(tch != 0){
//      TrkUtilities::fillCaloHitSeed(tch,fseed._chit);
//      // set the Ptr using the helix: this could be more direct FIXME!
//      fseed._chit._cluster = ccPtr;
//      // create a helix segment at the TrkCaloHit
//      KalSegment kseg;
//      // sample the momentum at this flight.  This belongs in a separate utility FIXME
//      BbrVectorErr momerr = kktrk.momentumErr(tch->fltLen());
//      double locflt(0.0);
//      const HelixTraj* htraj = dynamic_cast<const HelixTraj*>(kktrk.localTrajectory(tch->fltLen(),locflt));
//      TrkUtilities::fillSegment(*htraj,momerr,locflt-tch->fltLen(),kseg);
//      fseed._segments.push_back(kseg);
//    }
	}
      }
//      const KKMAT* kkmat = dynamic_cast<const KKMAT*>(eff.get());
//TrkUtilities::fillStraws(ktrk,fseed._straws); TODO!!
      const KKBF* kkbf = dynamic_cast<const KKBF*>(eff.get());
      if(kkbf != 0)fseed._nbend++;
    }
    // sample the fit at the requested z positions.
    for(auto zpos : zsave_ ) {
      // compute the time the trajectory crosses this plane
      double tz = zTime(fittraj,zpos);
      // find the explicit trajectory piece at this time
      auto const& zpiece = fittraj.nearestPiece(tz);
      // construct and add the segment
      fseed._segments.emplace_back(zpiece,tz);
    }
    return fseed;
  }

  double LoopHelixFit::zTime(PKTRAJ const& ptraj, double zpos) const {
    auto bpos = ptraj.position3(ptraj.range().begin());
    auto epos = ptraj.position3(ptraj.range().end());
    // assume linear transit to get an initial estimate
    double tz = ptraj.range().begin() + ptraj.range().range()*(zpos-bpos.Z())/(epos.Z()-bpos.Z());
    size_t zindex = ptraj.nearestIndex(tz);
    size_t oldindex;
    do {
      oldindex = zindex;
      auto const& traj = ptraj.piece(zindex);
      bpos = traj.position3(traj.range().begin());
      epos = traj.position3(traj.range().end());
      tz = traj.range().begin() + traj.range().range()*(zpos-bpos.Z())/(epos.Z()-bpos.Z());
      zindex = ptraj.nearestIndex(tz);
    } while(zindex != oldindex);
    return tz;
  }


}
// mu2e

DEFINE_ART_MODULE(mu2e::LoopHelixFit);
