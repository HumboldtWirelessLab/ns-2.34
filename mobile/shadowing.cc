/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */

/*
 * shadowing.cc
 * Copyright (C) 2000 by the University of Southern California
 * $Id: shadowing.cc,v 1.5 2008/02/20 04:59:14 tom_henderson Exp $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * The copyright of this module includes the following
 * linking-with-specific-other-licenses addition:
 *
 * In addition, as a special exception, the copyright holders of
 * this module give you permission to combine (via static or
 * dynamic linking) this module with free software programs or
 * libraries that are released under the GNU LGPL and with code
 * included in the standard release of ns-2 under the Apache 2.0
 * license or under otherwise-compatible licenses with advertising
 * requirements (or modified versions of such code, with unchanged
 * license).  You may copy and distribute such a system following the
 * terms of the GNU GPL for this module and the licenses of the
 * other code concerned, provided that you include the source code of
 * that other code when and as the GNU GPL requires distribution of
 * source code.
 *
 * Note that people who make modified versions of this module
 * are not obligated to grant this special exception for their
 * modified versions; it is their choice whether to do so.  The GNU
 * General Public License gives permission to release a modified
 * version without this exception; this exception also makes it
 * possible to release a modified version which carries forward this
 * exception.
 *
 */

/*
 * Shadowing propation model, including the path-loss model.
 * This statistcal model is applicable for both outdoor and indoor.
 * Wei Ye, weiye@isi.edu, 2000
 */


#include <math.h>

#include <delay.h>
#include <packet.h>

#include <packet-stamp.h>
#include <antenna.h>
#include <mobilenode.h>
#include <propagation.h>
#include <wireless-phy.h>
#include <shadowing.h>
#include "scheduler.h"


static class ShadowingClass: public TclClass {
public:
	ShadowingClass() : TclClass("Propagation/Shadowing") {}
	TclObject* create(int, const char*const*) {
		return (new Shadowing);
	}
} class_shadowing;


Shadowing::Shadowing()
{
  propagation_ = NULL;

  init_std_db_ = 0.0;
  no_nodes_ = -1;
  no_nodes_shift_ = -1;

	bind("pathlossExp_", &pathlossExp_);
	bind("std_db_", &std_db_);
    bind("init_std_db_", &init_std_db_);
	bind("dist0_", &dist0_);
	bind("seed_", &seed_);
    bind("nonodes_", &no_nodes_);
	
	ranVar = new RNG;
	ranVar->set_seed(RNG::PREDEF_SEED_SOURCE, seed_);

  for ( int i = 0; i < 200; i++ )
    madwifi_db_to_mw[i] = pow10(((double)i)/(10.0*2.0)) / 1000.0; //  /2 since we can use 0.5dbm steps for powercontrol

  if ( no_nodes_ > 0 ) {
    if ( no_nodes_ < 16  ) {
      no_nodes_ = 16;
      no_nodes_shift_ = 4;
    } else if ( no_nodes_ < 64  ) {
      no_nodes_ = 64;
      no_nodes_shift_ = 6;
    } else if ( no_nodes_ < 256  ) {
      no_nodes_ = 256;
      no_nodes_shift_ = 8;
    } else {
      no_nodes_ = 0;
    }
  }

  if ( no_nodes_<= 0 ) {
    Pr0_lookup_array = NULL;
    avg_db_lookup_array = NULL;
    init_std_db_array = NULL;
  } else {
    Pr0_lookup_array = new double[no_nodes_ * no_nodes_ * POWER_STEPS];
    avg_db_lookup_array = new double[no_nodes_ * no_nodes_ * POWER_STEPS];
    memset(Pr0_lookup_array, 0, no_nodes_ * no_nodes_ * POWER_STEPS * sizeof(double));
    memset(avg_db_lookup_array, 0, no_nodes_ * no_nodes_ * POWER_STEPS * sizeof(double));

    if ( init_std_db_ != 0.0 ) {
      init_std_db_array = new double[no_nodes_ * no_nodes_];
      memset(init_std_db_array, 0, no_nodes_ * no_nodes_ * sizeof(double));

      for( int i = 0; i < (no_nodes_ * no_nodes_); i++) {
        init_std_db_array[i] = ranVar->normal(0.0, init_std_db_);
      }
    } else {
      init_std_db_array = NULL;
    }
  }
}


Shadowing::~Shadowing()
{
	delete ranVar;
  if ( Pr0_lookup_array != NULL ) {
    delete[] Pr0_lookup_array;
    delete[] avg_db_lookup_array;
    delete[] init_std_db_array;
  }
}


double Shadowing::Pr(PacketStamp *t, PacketStamp *r, WirelessPhy *ifp)
{
#ifdef PROB_DEBUG
    fprintf(stderr,"Pathloss Shadowing Pr\n");
#endif
	double L = ifp->getL();		// system loss
	double lambda = ifp->getLambda();   // wavelength

	double Xt, Yt, Zt;		// loc of transmitter
	double Xr, Yr, Zr;		// loc of receiver

  MobileNode *tNode = t->getNode();
  MobileNode *rNode = r->getNode();

  int tNodeID = tNode->id_;
  int rNodeID = rNode->id_;
  int PrLevel = t->getPrLevel();

  int lookup_index_no_pow = (tNodeID << no_nodes_shift_) + rNodeID;
  int lookup_index_ = (lookup_index_no_pow << POWER_STEPS_SHIFT) + PrLevel;

  bool cached = false;
  bool use_cache = false;

  double avg_db = 0.0;
  double Pr0;

  if ( (Pr0_lookup_array != NULL) && (tNodeID < no_nodes_) && (rNodeID < no_nodes_) &&
       (PrLevel > 0) && (PrLevel < MADWIFI_DB2MW_SIZE)) {

    if ((tNode->speed() != 0.0) || (rNode->speed() != 0)) {
      //detect mobility: remove cache
      delete[] Pr0_lookup_array;
      delete[] avg_db_lookup_array;
      Pr0_lookup_array = NULL;
      avg_db_lookup_array = NULL;
    } else {
      use_cache = true;
      Pr0 = Pr0_lookup_array[lookup_index_];
      if ( Pr0 != 0.0 ) {
        cached = true;
        avg_db = avg_db_lookup_array[lookup_index_];
      }
    }
  }

  /* deterministic part can be cached */
  if ( !cached ) {
    t->getNode()->getLoc(&Xt, &Yt, &Zt);
    r->getNode()->getLoc(&Xr, &Yr, &Zr);

    // Is antenna position relative to node position?
    Xr += r->getAntenna()->getX();
    Yr += r->getAntenna()->getY();
    Zr += r->getAntenna()->getZ();
    Xt += t->getAntenna()->getX();
    Yt += t->getAntenna()->getY();
    Zt += t->getAntenna()->getZ();

    double dX = Xr - Xt;
    double dY = Yr - Yt;
    double dZ = Zr - Zt;
    double dist = sqrt(dX * dX + dY * dY + dZ * dZ);

    // get antenna gain
    double Gt = t->getAntenna()->getTxGain(dX, dY, dZ, lambda);
    double Gr = r->getAntenna()->getRxGain(dX, dY, dZ, lambda);

    // calculate receiving power at reference distance
    double txpr;

    if ( PrLevel == 0 ) txpr = t->getTxPr();
    else if ( PrLevel < MADWIFI_DB2MW_SIZE ) txpr = madwifi_db_to_mw[PrLevel];
    else txpr = pow10(((double)PrLevel)/(10.0*2.0)) / 1000.0; //  /2 since we can use 0.5dbm steps for powercontrol

    if ( propagation_ == NULL) {
      Pr0 = Friis(txpr, Gt, Gr, lambda, L, dist0_);
    } else {
      Pr0 = propagation_->Pr(t, r, ifp);
    }

    //fprintf(stderr,"%f: %d %e %e Dist: %f (%f,%f,%f) -> (%f,%f,%f) %d -> %d\n", Scheduler::instance().clock(),t->getPrLevel(),txpr, Pr0, dist,Xt, Yt, Zt,Xr, Yr, Zr, tNodeID, rNodeID );

    // calculate average power loss predicted by path loss model
    if (dist > dist0_) avg_db = -10.0 * pathlossExp_ * log10(dist/dist0_);

    if ( use_cache ) {
      Pr0_lookup_array[lookup_index_] = Pr0;
      avg_db_lookup_array[lookup_index_] = avg_db;
    }
  }

	// get power loss by adding a log-normal random variable (shadowing)
	// the power loss is relative to that at reference distance dist0_
	double powerLoss_db = avg_db + ranVar->normal(0.0, std_db_);

    //add init std db
    if ( init_std_db_array ) powerLoss_db += init_std_db_array[lookup_index_no_pow];

	// calculate the receiving power at dist
	double Pr = Pr0 * pow(10.0, powerLoss_db/10.0);
	
	return Pr;
}


int Shadowing::command(int argc, const char* const* argv)
{
    TclObject *obj;
	if (argc == 4) {
		if (strcmp(argv[1], "seed") == 0) {
			int s = atoi(argv[3]);
			if (strcmp(argv[2], "raw") == 0) {
				ranVar->set_seed(RNG::RAW_SEED_SOURCE, s);
			} else if (strcmp(argv[2], "predef") == 0) {
				ranVar->set_seed(RNG::PREDEF_SEED_SOURCE, s);
				// s is the index in predefined seed array
				// 0 <= s < 64
			} else if (strcmp(argv[2], "heuristic") == 0) {
				ranVar->set_seed(RNG::HEURISTIC_SEED_SOURCE, 0);
			}
			return(TCL_OK);
		}
	}
	if(argc == 3) {
	    if (strcmp(argv[1], "propagation") == 0) {
            if( (obj = TclObject::lookup(argv[2])) == 0) {
                fprintf(stderr, "Shadowing: Propagation: %s lookup of %s failed\n", argv[1], argv[2]);
                return TCL_ERROR;
            }
            propagation_ = (Propagation *) obj;
            assert(propagation_);

            return TCL_OK;
        }
    }
	
	return Propagation::command(argc, argv);
}

