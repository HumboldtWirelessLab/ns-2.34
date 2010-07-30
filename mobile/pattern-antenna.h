/*
 * Copyright (c) 2003 University of Colorado, Boulder
 * All rights reserved.
 *
 */

#ifndef ns_patternantenna_h
#define ns_patternantenna_h

#include <antenna.h>

class PatternAntenna : public Antenna {

public:
  PatternAntenna();
  virtual int command(int argc, const char*const* argv);
  // return the gain for a signal to a node at vector dX, dY, dZ
  // from the transmitter at wavelength lambda  
  virtual double getTxGain(double, double, double, double);

  // return the gain for a signal from a node at vector dX, dY, dZ
  // from the receiver at wavelength lambda
  virtual double getRxGain(double, double, double, double);

  // return a pointer to a copy of this antenna that will return the 
  // same thing for get{Rx,Tx}Gain that this one would at this point
  // in time.  This is needed b/c if a pkt is sent with a directable
  // antenna, this antenna may be have been redirected by the time we
  // call getTxGain on the copy to determine if the pkt is received.  
  virtual Antenna* copy();
  virtual void release();

  void setDir(double newdir) { Dir_ = newdir; }
  double getDir() { return Dir_; }

  void setHorizRxGainPattern(int samplecount, double* samples);
  void setHorizTxGainPattern(int samplecount, double* samples);

  static double get_angle(double dX, double dY);
  static double normalize(double deg);

  int read_pattern_from_msi(const char* msifile);

protected:
  double Dir_;
  typedef class gain_pattern {
  public:
    gain_pattern();
    ~gain_pattern();
    void set_gain_pattern(int samplecount, double* samples);
    double get_gain(double angle);
    void copy_pattern(gain_pattern& pat);

  protected:
    int samplecount_;
    double* samples_;
    double sample_quantum_;
  };

  gain_pattern horiz_tx_gain_;
  gain_pattern horiz_rx_gain_;
};


#endif // ns_uniantenna_h
