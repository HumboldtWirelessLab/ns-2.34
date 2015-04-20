/*
 *
 * This might not seem like a regular classifier, and it isn't.
 * It essentially has a fixed interface ID which it sends along
 * with its packet to the ClickNode it lives on, the idea being
 * that the Click subsystem will be the thing which actually
 * does the classifying, not the classifier.
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

#ifndef ns_classifier_click_h
#define ns_classifier_click_h

#include "object.h"
#include "cmu-trace.h"

class Packet;


class ClickEvent : public Event {
 public:
  simclick_node_t *simnode_;
  // Store an extra copy of the call time in sec/usec format.
  // This is to sidestep some roundoff errors which occured
  // when going back and forth between sec/usec and doubles.
  struct timeval when_;
};

class ClickEventHandler : public Handler {
 public:
  virtual void handle(Event* event);
};

class MACAddr {
 public:
  MACAddr() {
    memset(macaddr_,0,6);
  }
  explicit MACAddr(const string straddr) {
      unsigned crap[6];
      sscanf(straddr.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X", &crap[0],
	     &crap[1], &crap[2], &crap[3], &crap[4], &crap[5]);
      for (int i = 0; i < 6; i++)
	  macaddr_[i] = crap[i];
  }  
  explicit MACAddr(const unsigned char* rawaddr) {
    memcpy(macaddr_,rawaddr,6);
  }
  bool operator==(const MACAddr& rhs) const {
    return(0 == memcmp(macaddr_,rhs.macaddr_,6));
  }

  bool is_broadcast() {
    for (int i=0;i<6;i++) {
      if (macaddr_[i] != 0xff) {
	return false;
      }
    }
    return true;
  }

  string to_string() {
    char tmp[64];
    sprintf(tmp, "%02X:%02X:%02X:%02X:%02X:%02X", macaddr_[0],
	   macaddr_[1], macaddr_[2], macaddr_[3], macaddr_[4],
	   macaddr_[5]);

    return string(tmp);
  }
  unsigned char macaddr_[6];
};

namespace std {
template<>
struct less<MACAddr> {
  bool operator()(const MACAddr& l, const MACAddr& r) const {
    // Treat MAC as a big old integer...
    uint32_t leftu = *((uint32_t*)(l.macaddr_));
    uint32_t rightu = *((uint32_t*)(r.macaddr_));
    uint16_t leftl = *((uint16_t*)(l.macaddr_+4));
    uint16_t rightl = *((uint16_t*)(r.macaddr_+4));
    
    // Check the upper bytes first, if those are equal check lower
    if (leftu < rightu) {
      return true;
    }
    else if (leftu == rightu) {
      return (leftl < rightl);
    }
    return false;
  }
};
}

class ClickClassifier : public ExtClassifier, public ExtRouter,
    public simclick_node_t {
 public:
  ClickClassifier();
  virtual ~ClickClassifier();
  virtual int command(int argc, const char*const* argv);
  
  /*
   * Stuff to handle click requests
   */
 public:
  virtual int send_to_if(int ifid,int type,const unsigned char* data,
			 int len,simclick_simpacketinfo* pinfo);
  ClickEventHandler cevhandler_;

  // ExtRouter method
  virtual int route(Packet* p);

  string GetIPAddr(int ifid);
  string GetMACAddr(int ifid);
  string GetNodeName();
  int GetNodeAddr(); /// ToNSTrace
  int IFReady(int ifid);
  static void LinkLayerFailedCallback(Packet* p, void* arg);
  void LinkLayerFailed(Packet* p);
  void trace(char* fmt, ...); /// ToNSTrace
  int GetNextPktID(); /// ToNSTrace
  int GetIFID(const char *) const;
  int GetPosition(int *pos);
  int SetPosition(int *pos);
  int GetPerformanceCounter(int ifid, int *perf_counter);
  int HandleCCAOperation(int *cca);
  int SetBackoffQueueInfo(int *boq);
  int GetBackoffQueueInfo(int *boq);
  int HandleTXControl(char *txh);
  int GetRxTxStats(void *rxtxstats);
  int GetTxPower();
  int SetTxPower(int txpower);
  int GetRates(int *rates);

 protected:
  int GetNSSubtype(int clicktype);
  int GetClickPacketType(int nssubtype);
  struct timeval GetSimTime();
  Packet* MakeRawPacket(int type,int ifid,const unsigned char* data,int len,
			simclick_simpacketinfo* pinfo);
  typedef map<int,string> STRmap;
  map<int,string> ifipaddrs_;
  map<int,string> ifmacaddrs_;
  static map<MACAddr,int> global_mactonodemap_;
  static map<MACAddr,int> global_mactonsmacmap_;
  static map<u_int32_t,int> global_ipmap_;
  string nodename_;
  int nodeaddr_;
  bool click_initialized_;
  bool packetzerocopy_;



  //mvhaen -- meant to allow a click router to add to an ns2 trace file.
  Trace *logtarget_;
};

#endif
