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
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
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
 */

#ifndef ns_fading_h
#define ns_fading_h

#include <topography.h>
#include <phy.h>
#include <wireless-phy.h>
#include <packet-stamp.h>

class PacketStamp;
class WirelessPhy;
/*======================================================================
   Fading Models

	Using postion and wireless transmission interface properties,
	propagation models compute the power with which a given
	packet will be received.

	Not all propagation models will be implemented for all interface
	types, since a propagation model may only be appropriate for
	certain types of interfaces.

   ====================================================================== */

class Fading : public TclObject {

public:
  Fading() : name(NULL) {}

  // calculate the Pr by which the receiver will get a packet sent by
  // the node that applied the tx PacketStamp for a given inteface 
  // type
  virtual double compute(PacketStamp *tx, PacketStamp *rx, Phy *);
  virtual double compute(PacketStamp *tx, PacketStamp *rx, WirelessPhy *);
  virtual int command(int argc, const char*const* argv);

protected:
  char *name;
};


// No Fading
class FadingNone : public Fading {
public:
//  FadingNone();
    virtual double compute(PacketStamp *tx, PacketStamp *rx, WirelessPhy *ifp);
};

// FadingRayleigh
class FadingRayleigh: public Fading {
public:
    FadingRayleigh();
    ~FadingRayleigh();
    virtual double compute(PacketStamp *tx, PacketStamp *rx, WirelessPhy *ifp);

protected:
    RNG *ranVar;    // random number generator for normal distribution
    int seed_;      // seed for random number generator
};



#endif /* __fading_h__ */