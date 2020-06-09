#ifndef RecoDataProducts_CaloRecoDigi_hh
#define RecoDataProducts_CaloRecoDigi_hh

#include "canvas/Persistency/Common/Ptr.h"
#include "RecoDataProducts/inc/CaloDigi.hh"
#include <vector>


namespace mu2e
{
  class CaloRecoDigi 
  {
     public:
       CaloRecoDigi(): 
          _caloDigi(),_eDep(0),_eDepErr(0),_time(0),_timeErr(0),_chi2(-1),_ndf(0),_pileUp(false)
       {} 

       CaloRecoDigi(art::Ptr<CaloDigi> caloDigi, double eDep, double eDepErr, double time, 
                       double timeErr, double chi2, int ndf, bool pileUp) :
	  _caloDigi(caloDigi),_eDep(eDep),_eDepErr(eDepErr),_time(time),_timeErr(timeErr),
	  _chi2(chi2),_ndf(ndf),_pileUp(pileUp)
       {}

       const art::Ptr<CaloDigi>&  caloDigiPtr()   const {return _caloDigi;}
             int                  ROid()          const {return _caloDigi->roId();} 
             double               energyDep()     const {return _eDep;} 
             double               energyDepErr()  const {return _eDepErr;} 
             double               time()          const {return _time;} 
             double               timeErr()       const {return _timeErr;} 
             double               chi2()          const {return _chi2;}   
             int                  ndf()           const {return _ndf;}   
             bool                 pileUp()        const {return _pileUp;}  


     private:
       art::Ptr<CaloDigi>  _caloDigi;   
       double              _eDep; 
       double              _eDepErr; 
       double              _time; 
       double              _timeErr; 
       double              _chi2; 
       int                 _ndf; 
       bool                _pileUp;
  };


  typedef std::vector<mu2e::CaloRecoDigi> CaloRecoDigiCollection;
}
#endif 
