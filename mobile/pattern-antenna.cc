
/*
 * Copyright (c) 2003 University of Colorado, Boulder
 * All rights reserved.
 *
 */

#include <antenna.h>
#include <pattern-antenna.h> 
#include <math.h>
#include <iostream>
#include <fstream>
#include <string>

static class PatternAntennaClass : public TclClass {
public:
  PatternAntennaClass() : TclClass("Antenna/PatternAntenna") {}
  TclObject* create(int, const char*const*) {
    return (new PatternAntenna);
  }
} class_PatternAntenna;

PatternAntenna::PatternAntenna() {
  Dir_ = 0.0;
  bind("Dir_", &Dir_);
}

double
PatternAntenna::normalize(double deg) {
  while (360.0 <= deg) {
    deg -= 360.0;
  }

  while (0.0 > deg) {
    deg += 360.0;
  }

  return deg;
}

double
PatternAntenna::get_angle(double dX, double dY) {
  double angle;

  // First take care of dX or both == 0
  if ((0.0 == dY) && (0.0 == dX)) {
    angle = 0.0;
  }
  else if (0.0 == dX) {
    angle = M_PI/2.0;
  }
  else {
    angle = atan2(dY,dX);
    if (0.0 > angle) {
      angle += 2*M_PI;
    }
  }

  angle = (180.0/M_PI) * angle;

  return angle;
}

PatternAntenna::gain_pattern::gain_pattern() {
  samplecount_ = 0;
  samples_ = 0;
}

PatternAntenna::gain_pattern::~gain_pattern() {
  if (samples_) {
    free(samples_);
    samples_ = 0;
  }
}

void
PatternAntenna::gain_pattern::set_gain_pattern(int samplecount,double* samples)
{
  if (samples_) {
    free(samples_);
    samples_ = 0;
    samplecount = 0;
  }
  samples_ = (double*)malloc(samplecount*sizeof(double));
  samplecount_ = samplecount;
  sample_quantum_ = 360.0/samplecount_;
  memcpy(samples_,samples,samplecount*sizeof(double));
}

double
PatternAntenna::gain_pattern::get_gain(double angle) {
  // Find closest sample to angle.
  // XXX Maybe interpolate between samples at some point?
  int whichsamp = ( (int)((angle/sample_quantum_) + 0.5) ) % samplecount_;
  return samples_[whichsamp];
}

void
PatternAntenna::gain_pattern::copy_pattern(gain_pattern& pat) {
  set_gain_pattern(pat.samplecount_,pat.samples_);
}

// return the gain for a signal to a node at vector dX, dY, dZ
// from the transmitter at wavelength lambda  
double
PatternAntenna::getTxGain(double dX, double dY, double dZ, double l) {
  double angle = get_angle(dX,dY);
  return horiz_tx_gain_.get_gain(angle);
}

// return the gain for a signal from a node at vector dX, dY, dZ
// from the receiver at wavelength lambda
double
PatternAntenna::getRxGain(double dX, double dY, double dZ, double l) {
  double angle = get_angle(dX,dY);
  return horiz_rx_gain_.get_gain(angle);
}

void
PatternAntenna::setHorizRxGainPattern(int samplecount, double* samples) {
  horiz_rx_gain_.set_gain_pattern(samplecount,samples);
}


void
PatternAntenna::setHorizTxGainPattern(int samplecount, double* samples) {
  horiz_tx_gain_.set_gain_pattern(samplecount,samples);
}


// return a pointer to a copy of this antenna that will return the 
// same thing for get{Rx,Tx}Gain that this one would at this point
// in time.  This is needed b/c if a pkt is sent with a directable
// antenna, this antenna may be have been redirected by the time we
// call getTxGain on the copy to determine if the pkt is received.  
Antenna*
PatternAntenna::copy() {
  PatternAntenna* antcopy =
    (PatternAntenna*)TclObject::New("Antenna/PatternAntenna");
  antcopy->horiz_rx_gain_.copy_pattern(horiz_rx_gain_);
  antcopy->horiz_tx_gain_.copy_pattern(horiz_tx_gain_);
  return antcopy;
}

void
PatternAntenna::release() {
  TclObject::Delete((TclObject*)this);
}

int
PatternAntenna::read_pattern_from_msi(const char* msifile) {
  // XXX This code is by no means great. It is simplistic
  // and fragile, but I think it'll work well enough for the
  // simplistic kinds of tasks we'll give it.
  double gain;
  double* hpoints = 0;
  int hpointcount = 0;
  string units;
  string nxttok;
  int i = 0;
  //int result = 0;
  int finalresult = 0;
  ifstream msistrm(msifile);
  // MSI uses 0 deg as due north, we have 0 deg as due east (and 90 as N).
  // Add 90 deg to MSI numbers to fix this.
  int hoffset = 90;
  // MSI can use either dBd or dBi. I believe that ns-2 uses dBi.
  // We'll set this appropriately when we get the units.
  double gainoffset = 0.0;

  if (!msistrm) {
    return -1;
  }

  while (!msistrm.eof()) {
    msistrm >> nxttok;

    if (msistrm.eof()) {
      continue;
    }
    else if ("GAIN" == nxttok) {
      msistrm >> gain;
      msistrm >> units;
      //cout << "GAIN " << gain  << " " << units << endl;
      gainoffset = 0.0;
      if ("dBd" == units) {
	gainoffset = 2.15;
      }
    }
    else if ("HORIZONTAL" == nxttok) {
      msistrm >> hpointcount;
      //cout << "Horizontal pointcount: " << hpointcount << endl;
      hpoints = (double*) malloc(hpointcount*sizeof(double));
      for (i=0;i<hpointcount;i++) {
	double index = 0;
	double curpoint = 0;
	msistrm >> index;
	msistrm >> curpoint;
	//cout << "POINTS: " << index << " " << curpoint << endl;
	int hindx = (i + hoffset) % 360;
	if (0 > hindx) hindx += 360;
	hpoints[hindx] = gain - curpoint + gainoffset;
      }
    }
  }

  if (hpoints) {
    if (hpointcount) {
      // Assume same gain for Tx and Rx
      setHorizRxGainPattern(hpointcount,hpoints);
      setHorizTxGainPattern(hpointcount,hpoints);
    }
    free(hpoints);
    hpoints = 0;
    hpointcount = 0;
  }
  return finalresult;
}

int
PatternAntenna::command(int argc, const char*const* argv)
{
  Tcl& tcl = Tcl::instance();
  if (2 == argc) {
  }
  else if (3 == argc) {
    if(strcmp(argv[1], "loadmsi") == 0) {
      int result = read_pattern_from_msi(argv[2]);
      tcl.resultf("%d",result);
      return TCL_OK;
    }
  }
  else if (4 == argc) {
  } else if (argc == 5) {
  }

  return Antenna::command(argc, argv);
}
