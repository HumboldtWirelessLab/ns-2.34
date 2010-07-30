/*
 * ll-ext.h
 *
 * Much like the multicast routing system, ext routers needs to know what
 * interface packets come in from. However, the multicast interface
 * thing doesn't quite do what we need it to, so you get what
 * we've got here. 
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

#ifndef ns_ll_ext_h
#define ns_ll_ext_h

#include "object.h"

class Packet;
class LLExt;

class LLExtEvent : public Event {
 public:
  LLExt* llext;
};

class LLExtEventHandler : public Handler {
 public:
  virtual void handle(Event* event);
};

class LLExt : public LL, public Tap {
 public:
  LLExt();
  virtual ~LLExt();
	
  virtual void recv(Packet* p, Handler* h);
  virtual void sendDown(Packet* p);

  // Allow us to do promiscuous mode by acting as a tap.
  void tap(const Packet *p);

  void setExtID(int newid) {extid_ = newid;}
  int getExtID() {return extid_;}
  int ready();
  int getpending() { return packetpending_; }
  void setpending(int newpend) { packetpending_ = newpend; };
  void setpromiscuous(bool promisc);
  
 protected:
  virtual int command(int argc, const char*const* argv);
  int extid_;
  int packetpending_;
  LLExtEventHandler evhandle_;
};

#endif
