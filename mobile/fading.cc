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
 * 
 propagation.cc
 $Id: propagation.cc,v 1.8 2006/01/24 00:36:43 sallyfloyd Exp $
*/

#include <stdio.h>

#include <topography.h>
#include <fading.h>
#include <wireless-phy.h>

class PacketStamp;
int
Fading::command(int argc, const char*const* argv)
{
  TclObject *obj;  

  if(argc == 3) 
    {
      if( (obj = TclObject::lookup(argv[2])) == 0) 
	{
	  fprintf(stderr, "Fading: %s lookup of %s failed\n", argv[1],
		  argv[2]);
	  return TCL_ERROR;
	}
    }
  return TclObject::command(argc,argv);
}

/* As new network-intefaces are added, add a default method here */

double
Fading::compute(PacketStamp *, PacketStamp *, Phy *)
{
	fprintf(stderr,"Fading model %s not implemented for generic NetIF\n", name);
	abort();
	return 0; // Make msvc happy
}

double
Fading::compute(PacketStamp *, PacketStamp *, WirelessPhy *)
{
	fprintf(stderr,"Fading model %s not implemented for SharedMedia interface\n", name);
	abort();
	return 0; // Make msvc happy
}

/********************************************************************************************************/
/*************************************   Implemetation of Fading-Models   *******************************/
/********************************************************************************************************/

// methods for free space model
static class FadingNoneClass: public TclClass {
public:
	FadingNoneClass() : TclClass("Fading/None") {}
	TclObject* create(int, const char*const*) {
		return (new FadingNone);
	}
} class_fadingnone;


double FadingNone::compute(PacketStamp* tx, PacketStamp* rx, WirelessPhy* ifp)
{
    return 0.0;
}

// methods for fading model
static class FadingRayleighClass: public TclClass {
public:
    FadingRayleighClass() : TclClass("Fading/Rayleigh") {}
    TclObject* create(int, const char*const*) {
        return (new FadingRayleigh);
    }
} class_fadingrayleigh;

FadingRayleigh::FadingRayleigh()
{
  bind("seed_", &seed_);
  ranVar = new RNG;
  ranVar->set_seed(RNG::PREDEF_SEED_SOURCE, seed_);
}

FadingRayleigh::~FadingRayleigh()
{
    delete ranVar;
}

double FadingRayleigh::compute(PacketStamp* tx, PacketStamp* rx, WirelessPhy* ifp)
{
  static double VARIANCE = 0.6366197723676; // = 1/sqrt(pi/2)
  return 5.0 * log10(-2.0 * VARIANCE * log(ranVar->normal(0.0, 1.0))) / Constants.log10;
}
// FadingRayleigh
