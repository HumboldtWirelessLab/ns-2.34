/*
 * ll-ext.cc
 * This is a special link layer which explicitly notifies the attached
 * queue when it becomes free.
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

// XXX This is really a click link layer now - not just ext...

#include "config.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
//#include <stl.h>
//#include <hash_map.h>
#include <map>
#include <math.h>
#include <string>
#include "packet.h"
#include "ip.h"
#include "mac.h"
#include "classifier.h"
//#include "classifier-hash.h"
#include "scheduler.h"
#include "ll.h"
#include <click/simclick.h>
#include "ll-ext.h"
#include "packet.h"
#include "extrouter.h"
#include "classifier.h"
#include "classifier-ext.h"
#include "classifier-click.h"
#include "clickqueue.h"

static class LLExtClass : public TclClass {
public:
  LLExtClass() : TclClass("LL/Ext") {}
  TclObject* create(int, const char*const*) {
    return (new LLExt());
  }
} class_ll_ext;

void
LLExtEventHandler::handle(Event* event) {
  // XXX dangerous downcast - should use RTTI
  LLExtEvent* myevent = (LLExtEvent*) event;
  myevent->llext->setpending(0);
  delete myevent;
}


LLExt::LLExt() {
  extid_ = -1;
  macDA_ = -1;
  packetpending_ = 0;
}

LLExt::~LLExt() {
}

int LLExt::command(int argc, const char*const* argv) {
  //Tcl& tcl = Tcl::instance();
  if(argc == 2) {
  }
  else if (argc == 3) {
    if (strcmp("setid",argv[1]) == 0) {
      extid_ = atoi(argv[2]);
      return TCL_OK;
    }
    else if (strcmp("setpromiscuous",argv[1]) == 0) {
      bool promisc = (atoi(argv[2]) != 0);
      setpromiscuous(promisc);
      return TCL_OK;
    }
  }
  else if (argc == 4) {
  }

  return LL::command(argc,argv);
}

void LLExt::recv(Packet* p, Handler* h) {
  /*
   * Tag the packet and then defer to standard link layer handling.
   */
  struct hdr_cmn* hdr = HDR_CMN(p);
  hdr->iface() = extid_;
  // printf("ll = %d, ifid = %d\n",(int)this,hdr->iface());
  LL::recv(p,h);
}

void LLExt::sendDown(Packet* p) {
  // Someone decided that it would be A Good Thing to overlay
  // the 802.11 MAC packet info on top of the regular MAC packet info.
  // We need to fix the source and destination addresses here by accessing the
  // MAC object itself.
  struct hdr_mac* mhdr = HDR_MAC(p);
  int macdst = mhdr->macDA();
  int macsrc = mhdr->macSA();
  memset(mhdr,0,sizeof(struct hdr_mac));
  mac_->hdr_dst((char*)mhdr,macdst);
  mac_->hdr_src((char*)mhdr,macsrc);

  // Bleah. Send the packet down, mark ourself as being busy, and then
  // schedule an event to mark ourselves unbusy.
  packetpending_ = 1;
  LL::sendDown(p);
  LLExtEvent* llev = new LLExtEvent();
  llev->llext = this;
  Scheduler& s = Scheduler::instance();
  s.schedule(&evhandle_,llev,delay_);
}

int LLExt::ready() {
  ClickQueue* pcq = (ClickQueue*) ifq_;
  if (pcq) {
    return (!packetpending_ && pcq->ready());
  }

  // No ClickQueue? Then we're always ready.
  return 1;
}

void
LLExt::setpromiscuous(bool promisc) {
  if (!mac_) {
    return;
  }

  if (promisc) {
    mac_->installTap(this,true);
  }
  else {
    mac_->installTap(0,true);
  }
}

void 
LLExt::tap(const Packet *packet)
  /* process packets that are promiscously listened to from the MAC layer tap
  *** do not change or free packet *** */
{
  // XXX send a copy of packets received here up to the next layer.
  // This code assumes that the tap is being used with the "filterown"
  // option set, otherwise duplicate packets will get sent up the pipe.
  Packet* newp = packet->copy();
  recv(newp,0);
}
