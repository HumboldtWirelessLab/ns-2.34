/*
 * classifier-ext.cc
 * Base external router classifier
 */

/*****************************************************************************
 *  Copyright 2002, Univerity of Colorado at Boulder.                        *
 *                                                                           *
 *                        All Rights Reserved                                *
 *                                                                           *
 *  Permission to use, copy, modify, and distribute this software and its    *
 *  documentation for any purpose other than its incorporation into a        *
 *  commercial product is hereby granted without fee, provided that the      *
 *  above copyright notice appear in all copies and that both that           *
 *  copyright notice and this permission notice appear in supporting         *
 *  documentation, and that the name of the University not be used in        *
 *  advertising or publicity pertaining to distribution of the software      *
 *  without specific, written prior permission.                              *
 *                                                                           *
 *  UNIVERSITY OF COLORADO DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS      *
 *  SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND        *
 *  FITNESS FOR ANY PARTICULAR PURPOSE.  IN NO EVENT SHALL THE UNIVERSITY    *
 *  OF COLORADO BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL         *
 *  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA       *
 *  OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER        *
 *  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR         *
 *  PERFORMANCE OF THIS SOFTWARE.                                            *
 *                                                                           *
 ****************************************************************************/

#include <stdlib.h>
#include "config.h"
#include "packet.h"
#include "ip.h"
#include "extrouter.h"
#include "classifier.h"
#include "classifier-hash.h"
#include "classifier-ext.h"


static class ExtClassifierClass : public TclClass {
public:
  ExtClassifierClass() : TclClass("Classifier/Ext") {}
  TclObject* create(int, const char*const*) {
    return (new ExtClassifier());
  }
} class_ext_classifier;


ExtClassifier::ExtClassifier() {
  extrouter_ = NULL;
}

ExtClassifier::~ExtClassifier() {
}

int ExtClassifier::command(int argc, const char*const* argv) {
  int result = TCL_OK;
  //Tcl& tcl = Tcl::instance();

  result = Classifier::command(argc,argv);
  return result;
}

void ExtClassifier::recv(Packet* p, Handler* h) {
  /*
   * Use the interface and direction to decide what to do. If the
   * packet is going down and came from an agent, it needs to
   * go to the external router for processing. If coming up
   * from the external router it needs to be sent to the appropriate
   * local agent for processing. Otherwise, it just goes either down
   * to the ns network interface or up to the external router.
   */
  struct hdr_cmn* hdr = HDR_CMN(p);
  int extid = hdr->iface();
  if (hdr_cmn::DOWN == hdr->direction()) {
    if (ExtRouter::IFID_KERNELTAP == extid) {
      /*
       * Packet came from an agent - needs to go to the external router
       */
      //fprintf(stderr,"To external router\n");
      if (NULL != extrouter_) {
	extrouter_->route(p);
      }
      else {
	fprintf(stderr,"No external router set!\n");
      }
    }
    else {
      /*
       * Packet came from the external router - needs to go to the net
       */
      int cl = classify(p);
      if ((cl >= 0) && (cl <= maxslot_)) {
	NsObject* target = NULL;
	target = slot_[cl];
	if (NULL == target) {
	  /*
	   * "Drop" the packet
	   */
	  //puts("Dropping the packet");
	  Packet::free(p);
	}
	else {
	  //puts("Sending packet out!!!");
	  target->recv(p,h);
	}
      }
      else {
	fprintf(stderr,"Invalid slot: %d maxslot is %d\n",cl,maxslot_);
      }
    }
  }
  else if (hdr_cmn::UP == hdr->direction()) {
    if (ExtRouter::IFID_KERNELTAP == extid) {
      /*
       * Packet came from the external router - needs to go to an agent.
       */
      NsObject* target = NULL;
      target = slot_[0];
      if (NULL == target) {
	/*
	 * "Drop" the packet
	 */
	//fprintf(stderr,"Dropping the packet\n");
	Packet::free(p);
      }
      else {
	//fprintf(stderr,"Packet going to agent\n");
	target->recv(p,h);
      }

      //fprintf(stderr,"Hey! Send packets to agents!\n");
    }
    else {
      /*
       * Packet came from the net - needs to go to the external router
       */
      if (NULL != extrouter_) {
	extrouter_->route(p);
      }
    }
  }
  else {
    fprintf(stderr,"No packet direction set...");
  }
}

int
ExtClassifier::classify(Packet* p) {
  struct hdr_cmn* hdr = HDR_CMN(p);
  int extid = hdr->iface();

  /*
   * Simple  mapping between extid and slot number.
   * No real reason to make things more complicated right now.
   */
  int slot = extid;
  return slot;
}
