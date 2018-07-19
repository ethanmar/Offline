//
// StrawElectronics collects the electronics response behavior of a Mu2e straw in
// several functions.
//
// $Id: StrawElectronics.cc,v 1.17 2014/09/22 12:23:28 brownd Exp $
// $Author: brownd $
// $Date: 2014/09/22 12:23:28 $
//
// Original author David Brown, LBNL
//
#include "TrackerConditions/inc/StrawElectronics.hh"
#include "GeneralUtilities/inc/DigitalFiltering.hh"
#include "cetlib_except/exception.h"
#include "TMath.h"
#include <math.h>
#include <complex>
#include <algorithm>

using namespace std;
namespace mu2e {
  using namespace TrkTypes;
  double StrawElectronics::_pC_per_uA_ns(1000.0); // unit conversion from pC/ns to microAmp.

  StrawElectronics::StrawElectronics(fhicl::ParameterSet const& pset) :
    _dVdI{pset.get<vector<double> >("thresholddVdI",vector<double>(96,pset.get<double>("DefaultThresholddVdI",1.1e7))),
      pset.get<vector<double> >("adcdVdI",vector<double>(96,pset.get<double>("DefaultAdcdVdI",2.11e4)))}, // mVolt/uAmps (transimpedance gain)
    _tdeadAnalog(pset.get<double>("DeadTimeAnalog",100.0)), // nsec dead after threshold crossing (pulse baseline restoration time)
    _tdeadDigital(pset.get<double>("DeadTimeDigital",100.0)), // nsec dead after threshold crossing (electronics processing time)
    _vsat(pset.get<double>("SaturationVoltage",90.0)), // mVolt
    _vthresh(pset.get<vector<double> >("DiscriminatorThreshold",
          vector<double>(192,pset.get<double>("DefaultDiscriminatorThreshold",12.0)))), //mVolt, post amplification
    _snoise(pset.get<double>("StrawNoise",2.8)), // mvolt
    _analognoise{pset.get<double>("thresholdAnalogNoise",0.95), //mVolt
      pset.get<double>("adcAnalogNoise",3.0)},
    _ADCLSB(pset.get<double>("ADCLSB",0.3662)), //mVolt
    _maxADC(pset.get<int>("maxADC",4095)),
    _nADCpre(pset.get<unsigned>("nADCPresamples",5)),
    _ADCPeriod(pset.get<double>("ADCPeriod",20.0)), // nsec
    _ADCOffset(pset.get<double>("ADCOffset",2.0)), // nsec
    _maxtsep(pset.get<unsigned>("MaxThreshTimeSeparation",2)), // ADC clock ticks
    _TDCLSB(pset.get<double>("TDCLSB",0.03125)),  // nsec
    _maxTDC(pset.get<unsigned>("maxTDC",65535)), // 16 bits
    _TOTLSB(pset.get<double>("TOTLSB",4.0)), //ns
    _maxTOT(pset.get<unsigned>("maxTOT",15)),
    _tdcResolution(pset.get<double>("TDCResolution",0.1)), // ns
    _electronicsTimeDelay(pset.get<double>("ElectronicsTimeDelay",0.0)), // nsec
    _ewMarkerROCJitter(pset.get<double>("EventWindowMarkerROCJitter",0.5)), // ps (jitter per panel per microbuncH)
    _flashStart(pset.get<double>("FlashStart",1695.0)), //nsec
    _flashEnd(pset.get<double>("FlashEnd",500.0)), // nsec
    _responseBins(pset.get<int>("ResponseBins",10000)),
    _sampleRate(pset.get<double>("SampleRate",10.0)), // ghz
    _saturationSampleFactor(pset.get<int>("SaturationSampleFactor",50)),
    _preampPoles(pset.get<vector<double> >("PreampPoles",vector<double>{160.,7.})),
    _preampZeros(pset.get<vector<double> >("PreampZeros",vector<double>{0.2})),
    _adcPoles(pset.get<vector<double> >("ADCPoles",vector<double>{6.24137023,4.6,30.0, 6.24})),
    _adcZeros(pset.get<vector<double> >("ADCZeros",vector<double>{0.72343156})),
    _preampToAdc1Poles(pset.get<vector<double> >("PreampToAdc1Poles",vector<double>{6.24137032,30.0})),
    _preampToAdc1Zeros(pset.get<vector<double> >("PreampToAdc1Zeros",vector<double>{0.72343156})),
    _preampToAdc2Poles(pset.get<vector<double> >("PreampToAdc2Poles",vector<double>{4.6, 6.24})),
    _preampToAdc2Zeros(pset.get<vector<double> >("PreampToAdc2Zeros",vector<double>{})),
    _wireDistances(pset.get<vector<double> >("WireDistances",vector<double>{0.0,1200.0})),
    _currentMeans(pset.get<vector<double> >("CurrentMeans",vector<double>{0.0,1.29})),
    _currentNormalizations(pset.get<vector<double> >("CurrentNormalizations",vector<double>{1.0,1.0})),
    _currentSigmas(pset.get<vector<double> >("CurrentSigmas",vector<double>{2.2,3.0})),
    _currentT0s(pset.get<vector<double> >("CurrentT0s",vector<double>{4.7, 8.2})),
    _clusterLookbackTime(pset.get<double>("ClusterLookbackTime",5.0)),
    _timeOffsetPanel(pset.get<vector<double> >("TimeOffsetPanel",vector<double>(240,0))),
    _timeOffsetStrawHV(pset.get<vector<double> >("TimeOffsetStrawHV",vector<double>(96,0))),
    _timeOffsetStrawCal(pset.get<vector<double> >("TimeOffsetStrawCal",vector<double>(96,0)))
    {
      _flashStartTDC = tdcResponse(_flashStart-_electronicsTimeDelay); // we dont want the timeDelay in here that tdcResponse adds
      _flashEndTDC = tdcResponse(_flashEnd-_electronicsTimeDelay); 
      _ADCped.resize(96,0);
      for (int i=0;i<96;i++){
	double avgThresh = (_vthresh[i*2+0] + _vthresh[i*2+1])/2.;
	_ADCped[i] = (ADCValue) ((_maxADC+1)/2. - avgThresh/_ADCLSB * 20);
      }
      _ttrunc[thresh] = (_responseBins/2)/_sampleRate;
      _ttrunc[adc] = (_responseBins/2)/_sampleRate;

      // precompute ion drift current pulses
      _currentImpulse = std::vector<double>(_responseBins,0);
      _currentImpulse[_responseBins/2] = 1;
      _preampToAdc2Response = std::vector<double>(_responseBins,0);
      calculateResponse(_preampToAdc2Poles,_preampToAdc2Zeros,_currentImpulse,_preampToAdc2Response);

      for (size_t ai=0;ai<_wireDistances.size();ai++){
	_wPoints.push_back(WireDistancePoint(_wireDistances[ai],_currentMeans[ai],_currentNormalizations[ai],_currentSigmas[ai],_currentT0s[ai]));
	_wPoints[ai]._currentPulse = std::vector<double>(_responseBins,0);
	double integral = 0;
	for (int i=0;i<_responseBins;i++){
	  double t_gaus = (i-_responseBins/2)/_sampleRate;
	  double val_gaus = 1/sqrt(TMath::TwoPi()*_wPoints[ai]._sigma*_wPoints[ai]._sigma)*exp(-((t_gaus-_wPoints[ai]._mean)*(t_gaus-_wPoints[ai]._mean))/(2*_wPoints[ai]._sigma*_wPoints[ai]._sigma));
	  for (int j=0;j<_responseBins-i;j++){
	    double t_tail = j/_sampleRate;
	    double val = val_gaus / (t_tail + _wPoints[ai]._t0);
	    _wPoints[ai]._currentPulse[i+j] += val;
	    integral += val;
	  }
	}
	for (int i=0;i<_responseBins;i++){
	  // correct for sampleRate so that calculateResponse peak is independent of it
	  // this combined with pC_per_uA_ns is the unit transform from pC to uA
	  _wPoints[ai]._currentPulse[i] /= _sampleRate * _pC_per_uA_ns;
	  _wPoints[ai]._currentPulse[i] /= 4.615; // normalization for 1/(t+t0)
	  _wPoints[ai]._currentPulse[i] *= _wPoints[ai]._normalization;
	}

	// calculate parameters for transfer function
	_wPoints[ai]._preampResponse = std::vector<double>(_responseBins,0);
	_wPoints[ai]._adcResponse = std::vector<double>(_responseBins,0);
      _wPoints[ai]._preampToAdc1Response = std::vector<double>(_responseBins,0);
      calculateResponse(_preampPoles,_preampZeros,_wPoints[ai]._currentPulse,_wPoints[ai]._preampResponse);
      calculateResponse(_adcPoles,_adcZeros,_wPoints[ai]._currentPulse,_wPoints[ai]._adcResponse);
      calculateResponse(_preampToAdc1Poles,_preampToAdc1Zeros,_wPoints[ai]._currentPulse,_wPoints[ai]._preampToAdc1Response);

      // now set other parameters
      _wPoints[ai]._tmax[thresh] = 0;
      _wPoints[ai]._linmax[thresh] = 0;
      _wPoints[ai]._tmax[adc] = 0;
      _wPoints[ai]._linmax[adc] = 0;
      double preampToAdc1Max = 0;
      for (int i=0;i<_responseBins;i++){
        if (_wPoints[ai]._preampResponse[i] > _wPoints[ai]._linmax[thresh]){
          _wPoints[ai]._linmax[thresh] = _wPoints[ai]._preampResponse[i];
          _wPoints[ai]._tmax[thresh] = (-_responseBins/2 + i)/_sampleRate;
        }
        if (_wPoints[ai]._adcResponse[i] > _wPoints[ai]._linmax[adc]){
          _wPoints[ai]._linmax[adc] = _wPoints[ai]._adcResponse[i];
          _wPoints[ai]._tmax[adc] = (-_responseBins/2 + i)/_sampleRate;
        }
        if (_wPoints[ai]._preampToAdc1Response[i] > preampToAdc1Max)
          preampToAdc1Max = _wPoints[ai]._preampToAdc1Response[i];
      }
      
      // normalize preampToAdc1Response to match preampResponse
      for (int i=0;i<_responseBins;i++){
        _wPoints[ai]._preampToAdc1Response[i] *= _wPoints[ai]._linmax[thresh]/preampToAdc1Max;
      }
    }

    // normalize preampToAdc2Response to match adcResponse
    std::vector<double> preampToAdc2test(_responseBins,0);
    calculateResponse(_preampToAdc2Poles,_preampToAdc2Zeros,_wPoints[0]._preampToAdc1Response,preampToAdc2test);
    double preampToAdc2Max = 0;
    for (int i=0;i<_responseBins;i++){
      if (preampToAdc2test[i] > preampToAdc2Max)
        preampToAdc2Max = preampToAdc2test[i];
    }
    for (int i=0;i<_responseBins;i++){
      _preampToAdc2Response[i] *= _wPoints[0]._linmax[adc]/preampToAdc2Max;
      preampToAdc2test[i] *= _wPoints[0]._linmax[adc]/preampToAdc2Max;
    }
 }

  StrawElectronics::~StrawElectronics() {}

  void StrawElectronics::calculateResponse(std::vector<double> &poles, std::vector<double> &zeros, std::vector<double> &input, std::vector<double> &response) {
    std::vector<double> za;
    std::vector<double> pa;
    for (size_t i=0;i<poles.size();i++){
      if (poles[i] != 0)
        pa.push_back(poles[i]*-1*TMath::TwoPi());
    }
    for (size_t i=0;i<zeros.size();i++){
      if (zeros[i] != 0)
        za.push_back(zeros[i]*-1*TMath::TwoPi());
    }
    std::vector<double> a(za.size()+1,0);
    std::vector<double> b(pa.size()+1,0);
    std::vector<double> aprime(std::max(za.size()+1,pa.size()+1),0);
    std::vector<double> bprime(std::max(za.size()+1,pa.size()+1),0);
    DigitalFiltering::zpk2tf(b,a,za,pa); 
    DigitalFiltering::bilinear(bprime,aprime,a,b,_sampleRate*1000.);

    // calculate gain at 160 MHz
    std::complex<double> w (0,160 * TMath::TwoPi());
    std::complex<double> numerator,denominator;
    for (size_t i=0;i<b.size();i++)
      denominator += std::complex<double>(b[i],0)*std::pow(w,b.size()-i-1);
    for (size_t i=0;i<a.size();i++)
      numerator += std::complex<double>(a[i],0)*std::pow(w,a.size()-i-1);

    double gain_160 = std::abs(numerator/denominator);

    // calculate impulse response
    for (size_t i=0;i<static_cast<size_t>(_responseBins);i++){
      response[i] = 0;
      for (size_t j=0;j<bprime.size();j++){
        if (i >= j){
          response[i] += input[i-j]*bprime[j];
        }
      }
      for (size_t j=1;j<aprime.size();j++){
        if (i >= j){
          response[i] += -1*response[i-j]*aprime[j];
        }
      }
    }

    for (int i=0;i<_responseBins;i++)
      response[i] *= 1 / gain_160;
  }

  double StrawElectronics::linearResponse(StrawId sid, Path ipath, double time, double charge, double distance, bool forsaturation) const {
    int index = time*_sampleRate + _responseBins/2.;
    if ( index >= _responseBins)
      index = _responseBins-1;
    if (index < 0)
      index = 0;
    int  distIndex = 0;
    for (size_t i=1;i<_wPoints.size()-1;i++){
      if (distance < _wPoints[i]._distance)
        break;
      distIndex = i;
    }
    double distFrac = 1 - (distance - _wPoints[distIndex]._distance)/(_wPoints[distIndex+1]._distance - _wPoints[distIndex]._distance);
    double p0, p1;
    if (ipath == thresh){
      if (forsaturation){
        p0 = _wPoints[distIndex]._preampToAdc1Response[index];
        p1 = _wPoints[distIndex + 1]._preampToAdc1Response[index];
      }else{
        p0 = _wPoints[distIndex]._preampResponse[index];
        p1 = _wPoints[distIndex + 1]._preampResponse[index];
      }
    }else{
      p0 = _wPoints[distIndex]._adcResponse[index];
      p1 = _wPoints[distIndex + 1]._adcResponse[index];
    }
    return charge * ( p0 * distFrac + p1 * (1 - distFrac)) * _dVdI[ipath][sid.getStraw()];
  }

  double StrawElectronics::adcImpulseResponse(StrawId sid, double time, double charge) const {
    int index = time*_sampleRate + _responseBins/2.;
    if ( index >= _responseBins)
      index = _responseBins-1;
    if (index < 0)
      index = 0;
    return charge * _preampToAdc2Response[index] * _saturationSampleFactor * _dVdI[adc][sid.getStraw()]/_dVdI[thresh][sid.getStraw()];
  }
 
  double StrawElectronics::saturatedResponse(double vlin) const {
    if (vlin < _vsat)
      return vlin;
    else
      return _vsat;
  }

  double StrawElectronics::maxResponseTime(Path ipath,double distance) const {
    int  distIndex = 0;
    for (size_t i=1;i<_wPoints.size()-1;i++){
      if (distance < _wPoints[i]._distance)
        break;
      distIndex = i;
    }
    double distFrac = 1 - (distance - _wPoints[distIndex]._distance)/(_wPoints[distIndex+1]._distance - _wPoints[distIndex]._distance);
    double p0 = _wPoints[distIndex]._tmax[ipath];
    double p1 = _wPoints[distIndex + 1]._tmax[ipath];
    
    return p0 * distFrac + p1 * (1 - distFrac);
  }

  double StrawElectronics::maxLinearResponse(StrawId sid, Path ipath,double distance,double charge) const {
    int  distIndex = 0;
    for (size_t i=1;i<_wPoints.size()-1;i++){
      if (distance < _wPoints[i]._distance)
        break;
      distIndex = i;
    }
    double distFrac = 1 - (distance - _wPoints[distIndex]._distance)/(_wPoints[distIndex+1]._distance - _wPoints[distIndex]._distance);
    double p0 = _wPoints[distIndex]._linmax[ipath];
    double p1 = _wPoints[distIndex + 1]._linmax[ipath];
 
    return charge * (p0 * distFrac + p1 * (1 - distFrac)) * _dVdI[ipath][sid.getStraw()];
  }

  ADCValue StrawElectronics::adcResponse(StrawId sid, double mvolts) const {
    return min(static_cast<ADCValue>(max(static_cast<int>(floor(mvolts/_ADCLSB)+_ADCped[sid.getStraw()]),0)),_maxADC);
  }

  TDCValue StrawElectronics::tdcResponse(double time) const {
    // here time should already be relative to event window marker arrival at ROC.
    // we add electronics absolute delay before TDC actually digitizeds
    double time_from_ewm = time+_electronicsTimeDelay;
    return min(static_cast<TDCValue>(max(static_cast<int>(floor((time_from_ewm)/_TDCLSB)),0)),_maxTDC);
  }

  
  void StrawElectronics::digitizeWaveform(StrawId sid, ADCVoltages const& wf, ADCWaveform& adc) const{
    if(wf.size() != adc.size()){
      throw cet::exception("SIM") 
	<< "mu2e::StrawElectronics: wrong number of voltages to digitize" 
	<< endl;
    }
    for(size_t iadc=0;iadc<adc.size();++iadc){
      adc.at(iadc) = adcResponse(sid, wf[iadc]);
    }
  }

  bool StrawElectronics::digitizeTimes(TDCTimes const& times,TDCValues& tdcs) const {
    for(size_t itime=0;itime<2;++itime)
      tdcs[itime] = tdcResponse(times[itime]);
    // test these times against the flash blanking.  not sure if we should require
    // one or both to be valid FIXME!.
    // The test should be done module the TDC clock not the exact TDC FIXME!
    bool notflash(true);
    for(auto tdc : tdcs){
      notflash &= tdc > _flashEndTDC && tdc < _flashStartTDC;
    }
    return notflash;
  }

  void StrawElectronics::adcTimes(double time, ADCTimes& adctimes) const {
// clock has a fixed phase; Assume we digitize with a fixed delay relative to the leading edge
    adctimes.clear();
    adctimes.reserve(TrkTypes::NADC);
// find the phase immediately proceeding this time.  Subtract presamples
    size_t phase = std::max((int)0,int(ceil(time/_ADCPeriod))-(int)_nADCpre);
    for(unsigned itime=0;itime<TrkTypes::NADC;++itime){
      adctimes.push_back((phase+itime)*_ADCPeriod+_ADCOffset);
    }
  }
  
  bool StrawElectronics::combineEnds(double t1, double t2) const {
    // currently two clock ticks are allowed for coincidence time
    int clockTicks1 = static_cast<int>(floor((t1)/_ADCPeriod));
    int clockTicks2 = static_cast<int>(floor((t2)/_ADCPeriod));
    return (unsigned)abs(clockTicks1-clockTicks2) < _maxtsep;
  }

  void StrawElectronics::uncalibrateTimes(TrkTypes::TDCTimes &times, const StrawId &id) const {
    times[StrawEnd::hv] -= _timeOffsetPanel[id.getPanel()] + _timeOffsetStrawHV[id.getStraw()];
    times[StrawEnd::cal] -= _timeOffsetPanel[id.getPanel()] + _timeOffsetStrawCal[id.getStraw()];
  }

  double StrawElectronics::adcVoltage(StrawId sid, ADCValue adcval) const {
    return (adcval-_ADCped[sid.getStraw()])*_ADCLSB;
  }

  double StrawElectronics::adcCurrent(StrawId sid, ADCValue adcval) const {
  // this includes the effects from normalization of the pulse shape
    return adcVoltage(sid,adcval)/_dVdI[adc][sid.getStraw()];
  }

  double StrawElectronics::mypow(double val,unsigned n) {
    switch ( n ) {
      case 1:
	return val; break;
      case 2:
	return val*val; break;
      case 3:
	return val*val*val; break;
      case 4:
	return val*val*val*val; break;
      case 5:
	return val*val*val*val*val; break;
      default:
	return 0.0; break;
    }
  }

}
