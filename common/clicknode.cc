/*
 * clicknode.cc 
 * Base class for nsclick nodes.
 *
 * XXX Should probably move a bunch of the functionality in this
 * class to a superclass, i.e. something called ExtNode, since a lot
 * of this should work with most Ext routing stuff, not just click.
 *
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "connector.h"
#include "delay.h"
#include "packet.h"
#include "agent.h"
#include "rawpacket.h"
#include "random.h"
#include "trace.h"
#include "address.h"

#include "arp.h"
#include "topography.h"
#include "ll.h"
#include "mac.h"
#include "propagation.h"
#include "mobilenode.h"
#include "phy.h"
#include "wired-phy.h"
#include "god.h"
#include "extrouter.h"
#include "extclickrouter.h"
#include <sys/time.h>
#include <click/simclick.h>
#include "clicknode.h"


static class ClickNodeClass : public TclClass {
public:
  ClickNodeClass() : TclClass("Node/MobileNode/ClickNode") {}
  TclObject* create(int, const char*const*) {
    ClickNode* thenode = new ClickNode;
    if (!thenode) {
      return NULL;
    }

    /*
     * Do post-constructor initialization.
     */
    int result = thenode->cinit();
    if (0 > result) {
      delete thenode;
      thenode = NULL;
    }

    return thenode;
  }
} class_clicknode;


ClickNode::ClickNode(void) {
}

int
ClickNode::cinit() {
  int result = 0;
  return result;
}

int
ClickNode::command(int argc, const char*const* argv)
{
  //Tcl& tcl = Tcl::instance();
  if (2 == argc) {
  }
  else if (3 == argc) {
    if(strcmp(argv[1], "addif") == 0) {
      Phy* phyp = (Phy*)TclObject::lookup(argv[2]);
      if(phyp == 0) {
	return TCL_ERROR;
      }
      phyp->insertnode(&ifhead_);
      phyp->setnode(this);
      return TCL_OK;
    }
  }
  else if (4 == argc) {
  }
  else if (5 == argc) {
  }

  return MobileNode::command(argc, argv);
}


/* ======================================================================
   Other class functions
   ====================================================================== */
void
ClickNode::dump(void) {
  printf("Dumping a clicknode...\n");
}


