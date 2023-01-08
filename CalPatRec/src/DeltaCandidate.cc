//
#include "Offline/CalPatRec/inc/DeltaCandidate.hh"

namespace mu2e {
//-----------------------------------------------------------------------------
    DeltaCandidate::DeltaCandidate() :
      fSx(0), fSy(0), fSnx2(0), fSnxny(0), fSny2(0),fSnxnr(0),fSnynr(0),
      fSt(0), fSz(0), fSt2(0), fStz(0), fSz2(0)
    {
      fIndex  = -1;
      for(int s=0; s<kNStations; ++s) {
        dxy    [s] = -1;
        seed   [s] = NULL;
        fT0Min [s] = -1.e10;
        fT0Max [s] =  1.e10;
      }
      fFirstStation = 999;
      fLastStation  =  -1;
      fMcPart       = NULL;
      fNHits        = 0;
      fNStrawHits   = 0;
      fNHitsCE      = 0;
      fNSeeds       = 0;
      fSumEDep      = 0;
    }

    DeltaCandidate::DeltaCandidate(int Index, DeltaSeed* Seed, int Station) :
      fSx(0), fSy(0), fSnx2(0), fSnxny(0), fSny2(0),fSnxnr(0),fSnynr(0)
    {
      fIndex  = Index;
      for(int s=0; s<kNStations; ++s) {
        dxy    [s] = -1;
        seed   [s] = NULL;
        fT0Min [s] = -1.e10;
        fT0Max [s] =  1.e10;
      }
      fFirstStation = 999;
      fLastStation  =  -1;
      fMcPart       = NULL;
      fNHits        = 0;
      fNStrawHits   = 0;
      fNHitsCE      = 0;
      fNSeeds       = 0;
      fSumEDep      = 0;

      if (Seed) AddSeed(Seed,Station);
    }

//-----------------------------------------------------------------------------
// first seed added has at least one stereo, so this is safe
//-----------------------------------------------------------------------------
    void DeltaCandidate::AddSeed(DeltaSeed* Seed, int Station) {
      seed[Station]         = Seed;
      fNSeeds              += 1;

      if (fFirstStation > Station) fFirstStation = Station;
      if (fLastStation  < Station) fLastStation  = Station;
//-----------------------------------------------------------------------------
// ** FIXME recalculate the center of gravity - dont' need to be exact here
// seeds with SFace(1) < 0 don't have their center of gravity defined - their hits
// have been picked up individually, the intersection doesn't matter
//-----------------------------------------------------------------------------
      fNHits         += Seed->NHits();
      fNStrawHits    += Seed->NStrawHits();
      fSumEDep       += Seed->SumEDep();

      fSx            += Seed->fSx;
      fSy            += Seed->fSy;
      fSnx2          += Seed->fSnx2;
      fSnxny         += Seed->fSnxny;
      fSny2          += Seed->fSny2;
      fSnxnr         += Seed->fSnxnr;
      fSnynr         += Seed->fSnynr;

      double x_mean, y_mean, nxny_mean, nx2_mean, ny2_mean, nxnr_mean, nynr_mean;

      x_mean    = fSx   /fNHits;
      y_mean    = fSy   /fNHits;
      nxny_mean = fSnxny/fNHits;
      nx2_mean  = fSnx2 /fNHits;
      ny2_mean  = fSny2 /fNHits;
      nxnr_mean = fSnxnr/fNHits;
      nynr_mean = fSnynr/fNHits;

      double d  = (1-nx2_mean)*(1-ny2_mean)-nxny_mean*nxny_mean;

      double xc = ((x_mean-nxnr_mean)*(1-ny2_mean)+(y_mean-nynr_mean)*nxny_mean)/d;
      double yc = ((y_mean-nynr_mean)*(1-nx2_mean)+(x_mean-nxnr_mean)*nxny_mean)/d;

      CofM.SetX(xc);
      CofM.SetY(yc);
                                        // and recalculate phi
      phi             = CofM.phi();
//-----------------------------------------------------------------------------
// time
//-----------------------------------------------------------------------------
      double t = Seed->TMean();
      double z = DeltaFinder_stationZ[Station];
      fSt     += t;
      fSt2    += t*t;
      fStz    += t*z;
      fSz     += z;
      fSz2    += z*z;
                                        // and update the time...
      double tm, zm, tzm, t2m, z2m;

      tm  = fSt /fNSeeds;
      zm  = fSz /fNSeeds;
      tzm = fStz/fNSeeds;
      t2m = fSt2/fNSeeds;
      z2m = fSz2/fNSeeds;
                                        // 'combo-hit'-based way, FIXME
      fDtDz  = (tzm-tm*zm)/(z2m-zm*zm);
      fT0    = tm-fDtDz*zm;
      fSigT0 = sqrt((t2m-tm*tm)/(fNSeeds-0.9999));
//-----------------------------------------------------------------------------
// update t0min and t0max
// FIXME - need more reasonable limits
//-----------------------------------------------------------------------------
      float t0min     = Seed->MinHitTime();
      float t0max     = Seed->MaxHitTime();
      float t0        = (t0min+t0max)/2;
      float dt        = (t0max-t0min)/2;

      if (dt < 20) dt = 20;
      fT0Min[Station] = t0-dt;
      fT0Max[Station] = t0+dt;
//-----------------------------------------------------------------------------
// finally, set seed DeltaIndex
//-----------------------------------------------------------------------------
      Seed->fDeltaIndex = fIndex;
    }

//-----------------------------------------------------------------------------
    void DeltaCandidate::MergeDeltaCandidate(DeltaCandidate* Delta,int PrintErrorDiagnostics) {
      int is1 = Delta->FirstStation();
      int is2 = Delta->LastStation ();
      for (int is=is1; is<=is2; is++) {
        if (Delta->seed[is] == nullptr)                               continue;
        if ((seed[is] != nullptr) and PrintErrorDiagnostics) {
          printf("ERROR in DeltaCandidate::MergeDeltaCandidate: ");
          printf("merged DC also has a segment in station %i,",is);
          printf(" LEAVE THE EXISTING ONE\n");
                                                                      continue;
        }
        seed[is] = Delta->seed[is];
        seed[is]->fDeltaIndex = fIndex;
//-----------------------------------------------------------------------------
// increment hit count only if a seed has been addded
//-----------------------------------------------------------------------------
        fNSeeds              += 1;
        fNHits               += seed[is]->NHits();
        fNStrawHits          += seed[is]->NStrawHits();
        fSumEDep             += seed[is]->SumEDep();

        if (fFirstStation > is) fFirstStation = is;
        if (fLastStation  < is) fLastStation  = is;
      }
//-----------------------------------------------------------------------------
// update XY sums
//-----------------------------------------------------------------------------
      fSx            += Delta->fSx;
      fSy            += Delta->fSy;
      fSnx2          += Delta->fSnx2;
      fSnxny         += Delta->fSnxny;
      fSny2          += Delta->fSny2;
      fSnxnr         += Delta->fSnxnr;
      fSnynr         += Delta->fSnynr;
//-----------------------------------------------------------------------------
// and recalculate the center-of-mass XY position
//-----------------------------------------------------------------------------
      double x_mean, y_mean, nxny_mean, nx2_mean, ny2_mean, nxnr_mean, nynr_mean;

      x_mean    = fSx   /fNHits;
      y_mean    = fSy   /fNHits;
      nxny_mean = fSnxny/fNHits;
      nx2_mean  = fSnx2 /fNHits;
      ny2_mean  = fSny2 /fNHits;
      nxnr_mean = fSnxnr/fNHits;
      nynr_mean = fSnynr/fNHits;

      double d  = (1-nx2_mean)*(1-ny2_mean)-nxny_mean*nxny_mean;

      double xc = ((x_mean-nxnr_mean)*(1-ny2_mean)+(y_mean-nynr_mean)*nxny_mean)/d;
      double yc = ((y_mean-nynr_mean)*(1-nx2_mean)+(x_mean-nxnr_mean)*nxny_mean)/d;

      CofM.SetX(xc);
      CofM.SetY(yc);
                                        // and recalculate phi
      phi             = CofM.phi();
//-----------------------------------------------------------------------------
// time
//-----------------------------------------------------------------------------
      fSt     += Delta->fSt;
      fSt2    += Delta->fSt2;
      fStz    += Delta->fStz;
      fSz     += Delta->fSz;
      fSz2    += Delta->fSz2;

      double tm, zm, tzm, t2m, z2m;

      tm  = fSt /fNSeeds;
      zm  = fSz /fNSeeds;
      tzm = fStz/fNSeeds;
      t2m = fSt2/fNSeeds;
      z2m = fSz2/fNSeeds;
                                        // 'combo-hit'-based way, FIXME
      fDtDz  = (tzm-tm*zm)/(z2m-zm*zm);
      fT0    = tm-fDtDz*zm;
      fSigT0 = sqrt((t2m-tm*tm)/(fNSeeds-0.9999));
    }

}
