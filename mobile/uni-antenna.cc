
/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Daedalus Research
 *	Group at the University of California Berkeley.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.

 * Ported from CMU/Monarch's code, nov'98 -Padma.
   omni-antenna.cc
   */

#include <antenna.h>
#include <uni-antenna.h> 
#include <math.h>

static class UniAntennaClass : public TclClass {
public:
  UniAntennaClass() : TclClass("Antenna/UniAntenna") {}
  TclObject* create(int, const char*const*) {
    return (new UniAntenna);
  }
} class_UniAntenna;

UniAntenna::UniAntenna() : is_copy_(false) {
  Gt_ = 1.0;
  Gr_ = 1.0;
  GtOmni_ = 0.0;
  GrOmni_ = 0.0;
  Dir_ = 0.0;
  Width_ = 360.0;
  bind("Gt_", &Gt_);
  bind("Gr_", &Gr_);
  bind("GtOmni_", &GtOmni_);
  bind("GrOmni_", &GrOmni_);
  bind("Dir_", &Dir_);
  bind("Width_", &Width_);
}

double
UniAntenna::normalize(double deg) {
  while (360.0 <= deg) {
    deg -= 360.0;
  }

  while (0.0 > deg) {
    deg += 360.0;
  }

  return deg;
}

double
UniAntenna::get_angle(double dX, double dY) {
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

void
UniAntenna::get_cone(double& lb, double& ub) {
  lb = Dir_ - (Width_/2.0);
  ub = Dir_ + (Width_/2.0);
  lb = normalize(lb);
  ub = normalize(ub);
}

bool
UniAntenna::is_in_cone(double dX, double dY) {
  bool result = false;
  double angle = get_angle(dX,dY);
  double lb, ub;
  get_cone(lb,ub);

  // If lb and ub are swapped, we had a wraparound. In this
  // case, the specified range is the inverse of the cone
  // we want to check for.
  if (lb > ub) {
    result = ! ((ub <= angle) && (angle <= lb));
  }
  else if (lb == ub) {
    result = true;
  }
  else {
    result = (lb <= angle) && (angle <= ub);
  }

  return result;
}

// return the gain for a signal to a node at vector dX, dY, dZ
// from the transmitter at wavelength lambda  
double
UniAntenna::getTxGain(double dX, double dY, double dZ, double l) {
  // XXX for now ignore dZ, just do 2 dimensions, and ignore lambda, too
  double gain = Gt_;

  // XXX very stupid model. Gt_ gain within cone, GtOmni_ outside.
  // Hopefully good enough for what we need initially...
  if (is_in_cone(dX,dY)) {
    gain = Gt_;
  }
  else {
    gain = GtOmni_;
  }
  
  return gain;
}

// return the gain for a signal from a node at vector dX, dY, dZ
// from the receiver at wavelength lambda
double
UniAntenna::getRxGain(double dX, double dY, double dZ, double l) {
  // XXX for now ignore dZ, just do 2 dimensions, and ignore lambda, too.
  double gain = Gr_;

  // XXX very stupid model. Gr_ gain within cone, GrOmni_ outside.
  // Hopefully good enough for what we need initially...
  if (is_in_cone(dX,dY)) {
    gain = Gr_;
  }
  else {
    gain = GrOmni_;
  }

  return gain;
}

// return a pointer to a copy of this antenna that will return the 
// same thing for get{Rx,Tx}Gain that this one would at this point
// in time.  This is needed b/c if a pkt is sent with a directable
// antenna, this antenna may be have been redirected by the time we
// call getTxGain on the copy to determine if the pkt is received.  
Antenna*
UniAntenna::copy() {
  UniAntenna* antcopy = (UniAntenna*)TclObject::New("Antenna/UniAntenna");
  antcopy->X_ = X_;
  antcopy->Y_ = Y_;
  antcopy->Z_ = Z_;
  antcopy->Gt_ = Gt_;
  antcopy->Gr_ = Gr_;
  antcopy->Dir_ = Dir_;
  antcopy->Width_ = Width_;
  antcopy->is_copy_ = true;
  return antcopy;
}

void
UniAntenna::release() {
  if (is_copy_) {
    TclObject::Delete((TclObject*)this);
  }
}


