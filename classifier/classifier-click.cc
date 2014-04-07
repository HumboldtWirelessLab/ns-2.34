/*
 * classifier-click classifier file for nsclick
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
/*
 *	Modified by Nicolas Letor to support wifi elements.
 * 	Performance Analysis of Telecommunication Systems (PATS) research group,
 * 	Interdisciplinary Institute for Broadband Technology (IBBT) & Universiteit Antwerpen.
 */
#include "config.h"
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
//#include <stl.h>
//#include <hash_map.h>
#include <map>
#include <string>
#include <stdarg.h>

#include "wireless-phy.h"

#include "agent.h"
#include "packet.h"
#include "rawpacket.h"
#include "ip.h"
#include "extrouter.h"
#include "classifier.h"
#include "classifier-ext.h"
#include "mobilenode.h"
#include "clicknode.h"
#include "address.h"
#include <click/simclick.h>
#include "scheduler.h"
#include "classifier-click.h"
#include "ll-ext.h"
#include "clickqueue.h"
#include "mac-802_11.h"

#include "packet_anno.h"

#include "random.h"

static class ClickClassifierClass : public TclClass {
public:
  ClickClassifierClass() : TclClass("Classifier/Ext/Click") {}
  TclObject* create(int, const char*const*) {
    return (new ClickClassifier());
  }
} class_click_classifier;


void
ClickEventHandler::handle(Event* event) {
    // XXX dangerous downcast - should use RTTI
    // XXX multithreading!
    ClickEvent* cevent = (ClickEvent*) event;
    cevent->simnode_->curtime = cevent->when_;
    //fprintf(stderr,"Should be calling simclick_click_run: %lf\n",event->time_);
    simclick_click_run(cevent->simnode_);
    delete cevent;
}

map<MACAddr,int> ClickClassifier::global_mactonodemap_;
map<MACAddr,int> ClickClassifier::global_mactonsmacmap_;
map<u_int32_t,int> ClickClassifier::global_ipmap_;

ClickClassifier::ClickClassifier() {
  extrouter_ = this;
  click_initialized_ = false;
  packetzerocopy_ = false;
}

int
ClickClassifier::command(int argc, const char*const* argv)
{
  Tcl& tcl = Tcl::instance();
  if (2 == argc) {
    if (strcmp(argv[1], "getnodename") == 0) {
      // getnodename
      tcl.resultf(nodename_.c_str());
      return TCL_OK;
    }
    if (strcmp(argv[1], "runclick") == 0) {
      // runclick
      if (click_initialized_) {
	  simclick_node_t::curtime = GetSimTime();
	  simclick_click_run(this);
      }
      return TCL_OK;
    }
  }
  else if (3 == argc) {
    if(strcmp(argv[1], "loadclick") == 0) {
	simclick_node_t::curtime = GetSimTime();
	if (simclick_click_create(this, argv[2]) >= 0) {
	    click_initialized_ = true;
	    simclick_click_run(this);
	}

      return TCL_OK;
    }
    if (strcmp(argv[1], "getip") == 0) {
      // getip <ifname>
      int theif = GetIFID(argv[2]);
      //fprintf(stderr,"get ipaddr is %s\n",ifipaddrs_[theif].c_str());
      tcl.resultf(ifipaddrs_[theif].c_str());
      return TCL_OK;
    }
    if (strcmp(argv[1], "getmac") == 0) {
      // getmac <ifname>
      int theif = GetIFID(argv[2]);
      //fprintf(stderr,"get macaddr is %s\n",ifmacaddrs_[theif].c_str());
      tcl.resultf(ifmacaddrs_[theif].c_str());
      return TCL_OK;
    }
    if (strcmp(argv[1], "setnodename") == 0) {
      // setnodename <ifname>
      nodename_ = argv[2];
      return TCL_OK;
    }
    if (strcmp(argv[1], "setnodeaddr") == 0) {
      // setnodeaddr <nodeaddress>
      nodeaddr_ =  Address::instance().str2addr(argv[2]);
      return TCL_OK;
    }
    //mvhaen -- meant to set the trace file
    if (strcmp(argv[1], "tracetarget") == 0) {
      logtarget_ = ( CMUTrace* ) TclObject::lookup(argv[2]);
      if (logtarget_ == 0)
          return TCL_ERROR;
      return TCL_OK;
    }
    if (strcmp(argv[1], "packetzerocopy") == 0) {
      packetzerocopy_ = false;
      if (strcmp(argv[2], "true") == 0) packetzerocopy_ = true;
      return TCL_OK;
    }
  }
  else if (4 == argc) {
    if(strcmp(argv[1], "setip") == 0) {
      // setip <ifname> <ipaddr>
      int theif = GetIFID(argv[2]);
      ifipaddrs_[theif] = string(argv[3]);
      //fprintf(stderr,"ipaddr is %s\n",ifipaddrs_[theif].c_str());
      // Also save the binary form of this IP address in a static
      // (i.e. simulator global) hash map of IP addresses to ns-2
      // addresses. This lets us track map IP to ns-2 address when
      // we might need it.
      global_ipmap_[inet_addr(argv[3])] = nodeaddr_;
      return TCL_OK;
    }
    else if(strcmp(argv[1], "setmac") == 0) {
      // setmac <ifname> <macaddr>
      int theif = GetIFID(argv[2]);
      ifmacaddrs_[theif] = string(argv[3]);

      //fprintf(stderr,"macaddr is %s\n",ifmacaddrs_[theif].c_str());

      // Also save the binary form of this MAC address in a static
      // (i.e. simulator global) hash map of MAC addresses to ns-2
      // addresses. This lets us set the destination address in the
      // ns-2 packet header.
      MACAddr thismacaddr = MACAddr(string(argv[3]));
      global_mactonodemap_[thismacaddr] = nodeaddr_;
      LL* mylink = (LL*) slot_[theif];
      global_mactonsmacmap_[thismacaddr] = mylink->macDA();
      return TCL_OK;
    }
    else if (strcmp(argv[1], "readhandler") == 0) {
      char* readreturn = 0;
      simclick_node_t::curtime = GetSimTime();
      readreturn = simclick_click_read_handler(this,argv[2],argv[3],0,0);
      //fprintf(stderr, "readhandler: %s\n",clickretc);
      if (readreturn) {
	tcl.resultf("%s", readreturn);
	free(readreturn);
	readreturn = 0;
      }
      else {
	tcl.resultf("");
      }
      return TCL_OK;
    }
  } else if (argc == 5) {
    if (strcmp(argv[1], "writehandler") == 0) {
      int clickret;
      simclick_node_t::curtime = GetSimTime();
      clickret = simclick_click_write_handler(this, argv[2], argv[3], argv[4]);
      //fprintf(stderr, "writehandler: %i\n",clickret);
      tcl.resultf("%i", clickret);
      return TCL_OK;
    }
  }

  return ExtClassifier::command(argc, argv);
}


ClickClassifier::~ClickClassifier() {
}

int
ClickClassifier::route(Packet* p) {
  int result = 0;
  if (click_initialized_) {
    unsigned char* data = NULL;
    int len = ((PacketData*)(p->userdata()))->size();
    simclick_simpacketinfo simpinfo;
    hdr_cmn* chdr = HDR_CMN(p);
    int ifid = chdr->iface_;
    hdr_ip* iphdr = hdr_ip::access(p);
    simpinfo.id = chdr->uid();
    simpinfo.fid = iphdr->flowid();
    simpinfo.txfeedback = (chdr->txfeedback() == hdr_cmn::YES)?1:0;
    simpinfo.zero_copy = packetzerocopy_?1:0;
    hdr_raw* rhdr = hdr_raw::access(p);
    int nssubtype = rhdr->subtype;
    int clicktype = GetClickPacketType(nssubtype);
    simpinfo.simtype = rhdr->ns_type;

    simclick_node_t::curtime = GetSimTime();
    //fprintf(stderr,"Sending packet up to click...\n");

    unsigned char* pdat = p->accessdata();

    if ( packetzerocopy_ ) {
      PacketData *pdata = (PacketData*)(p->userdata());
      data = pdat;
      pdata->unlink_data();
      //fprintf(stderr, "Datapointer: %p Ref: %d Time: %f F: %d\n",p->accessdata(), p->ref_count(), GetSimTime(), simpinfo.txfeedback);
    } else {
      data = new unsigned char[len];
      memcpy(data,pdat,len);
    }

    /*
     * XXX Destroy packet for now. This may change if we wind
     * up having to track and reuse ns packets after they've gone through
     * click.
     */
    Packet::free(p);
    p = NULL;

    simclick_click_send(this,ifid,clicktype,data,len,&simpinfo);
    if (simpinfo.zero_copy == 0) delete[] data;                 //if no zero_copy, we have to delete data
    data = 0;
    pdat = 0;
  }
  else {
    fprintf(stderr,"No click upcall set!\n");
  }
  return result;
}

string
ClickClassifier::GetIPAddr(int ifid) {
  return ifipaddrs_[ifid];
}

string
ClickClassifier::GetMACAddr(int ifid) {
  return ifmacaddrs_[ifid];
}

string
ClickClassifier::GetNodeName() {
  return nodename_;
}

int
ClickClassifier::GetNodeAddr()
{
  return nodeaddr_;
}

int
ClickClassifier::GetIFID(const char *ifname) const
{
    int r = -1;

    /*
     * Provide a mapping between a textual interface name
     * and the id numbers used. This is mostly for the
     * benefit of click scripts, i.e. you can still refer to
     * an interface as, say, /dev/eth0.
     */
    if (strstr(ifname, "tap") || strstr(ifname, "tun")) {
	/*
	 * A tapX or tunX interface goes to and from the kernel -
	 * always IFID_KERNELTAP
	 */
	r = ExtRouter::IFID_KERNELTAP;
    } else if (const char *devname = strstr(ifname, "eth")) {
	/*
	 * Anything with an "eth" followed by a number is
	 * a regular interface. Add the number to IFID_FIRSTIF
	 * to get the handle.
	 */
	while (*devname && !isdigit((unsigned char) *devname))
	    devname++;
	if (*devname)
	    r = atoi(devname) + ExtRouter::IFID_FIRSTIF;
    } else if (const char *devname = strstr(ifname, "drop")) {
	/*
	 * Anything with an "drop" followed by a number is
	 * a special interface on which we place packets that
	 * get dropped due to MAC layer feedback. Add the number to
	 * IFID_FIRSTIFDROP to get the handle.
	 */
	while (*devname && !isdigit((unsigned char) *devname))
	    devname++;
	if (*devname)
	    r = atoi(devname) + ExtRouter::IFID_FIRSTIFDROP;
    }

    return r;
}

/*
 * Click service methods
 */
extern "C" {

static int simstrlcpy(char *buf, int len, const string &s) {
    if (len) {
	len--;
	if ((unsigned) len > s.length())
	    len = s.length();
	s.copy(buf, len);
	buf[len] = '\0';
    }
    return 0;
}

int simclick_sim_command(simclick_node_t *simnode, int cmd, ...)
{
    ClickClassifier *cc = static_cast<ClickClassifier *>(simnode);
    Tcl &tcl = Tcl::instance();
    va_list val;
    va_start(val, cmd);
    int r;

    switch (cmd) {
	
      case SIMCLICK_VERSION:
	r = 0;
	break;

      case SIMCLICK_SUPPORTS: {
	  int othercmd = va_arg(val, int);
	  r = (othercmd >= 0 && othercmd <= SIMCLICK_CHANGE_CHANNEL) ||
	      (othercmd >= SIMCLICK_GET_NODE_POSITION && othercmd <= SIMCLICK_WIFI_TX_CONTROL);
	  break;
      }

      case SIMCLICK_IFID_FROM_NAME: {
	  const char *ifname = va_arg(val, const char *);
	  r = cc->GetIFID(ifname);
	  break;
      }

      case SIMCLICK_IPADDR_FROM_NAME: {
	  const char *ifname = va_arg(val, const char *);
	  char *buf = va_arg(val, char *);
	  int len = va_arg(val, int);
	  int ifid = cc->GetIFID(ifname);
	  r = simstrlcpy(buf, len, cc->GetIPAddr(ifid));
	  break;
      }

      case SIMCLICK_MACADDR_FROM_NAME: {
	  const char *ifname = va_arg(val, const char *);
	  char *buf = va_arg(val, char *);
	  int len = va_arg(val, int);
	  int ifid = cc->GetIFID(ifname);
	  r = simstrlcpy(buf, len, cc->GetMACAddr(ifid));
	  break;
      }

      case SIMCLICK_SCHEDULE: {
	  const struct timeval *when = va_arg(val, const struct timeval *);
	  double simtime = when->tv_sec + (when->tv_usec / 1.0e6);
	  double simdelay = simtime - Scheduler::instance().clock();
	  if ( simdelay < 0 ) {
	    double clock_now = Scheduler::instance().clock();
	    int clock_sec = floor(clock_now);
	    int clock_usec = floor((clock_now - (1.0 * clock_sec)) * 1.0e6) ;

	    if ( (clock_sec < when->tv_sec) || ( (clock_sec == when->tv_sec) && (clock_usec < when->tv_usec))) {
	      fprintf(stderr,"Schedule past in ns\n");
	    } else {
	      if ( clock_sec == when->tv_sec && clock_usec == when->tv_usec ) {
	    	//fprintf(stderr,"schedule now in ns\n");
		simdelay = 0.0;
	      }	else {
	        fprintf(stderr,"WTF ! Schedule whatever in ns\n");
	      }        
	    }
	  }
	  ClickEvent *ev = new ClickEvent;
	  ev->simnode_ = simnode;
	  ev->when_ = *when;
	  Scheduler::instance().schedule(&cc->cevhandler_, ev, simdelay);
	  r = 0;
	  break;
      }

      case SIMCLICK_GET_NODE_NAME: {
	  char *buf = va_arg(val, char *);
	  int len = va_arg(val, int);
	  r = simstrlcpy(buf, len, cc->GetNodeName());
	  break;
      }

      case SIMCLICK_IF_READY: {
	  int ifid = va_arg(val, int);
	  r = cc->IFReady(ifid);
	  break;
      }

      case SIMCLICK_TRACE: {
	  const char *event = va_arg(val, const char *);
	  cc->trace("%s", event);
	  r = 1;
	  break;
      }

      case SIMCLICK_GET_NODE_ID:
	r = cc->GetNodeAddr();
	break;

      case SIMCLICK_GET_NEXT_PKT_ID:
	r = cc->GetNextPktID();
	break;

      case SIMCLICK_CHANGE_CHANNEL: {
	  int ifid = va_arg(val, int);
	  int channelid = va_arg(val, int);
	  char work[128];
	  //fprintf(stderr,"SwitchChannel %i %i %i\n", cc->GetNodeAddr(), ifid, channelid);
	  sprintf(work, "SwitchChannel %i %i %i", cc->GetNodeAddr(), ifid, channelid);
	  tcl.eval(work);
	  r = 0;
	  break;
      }
      case SIMCLICK_GET_RANDOM_INT: {
	  uint32_t *rand_num = va_arg(val, uint32_t*);
	  uint32_t max_rand = va_arg(val, uint32_t);
	  *rand_num = Random::integer(max_rand);
	  r = 0;
          break;
      }
      case SIMCLICK_GET_NODE_POSITION: {
        int *pos = va_arg(val, int *);
        cc->GetPosition(pos);
        break;
      }
      case SIMCLICK_SET_NODE_POSITION: {
        int *pos = va_arg(val, int *);
        cc->SetPosition(pos);
        break;
      }
      case SIMCLICK_GET_PERFORMANCE_COUNTER: {
        int *stats = va_arg(val, int *);
        cc->GetPerformanceCounter(0, stats);
        break;
      }
      case SIMCLICK_CCA_OPERATION: {
        int *cca = va_arg(val, int *);
        cc->HandleCCAOperation(cca);
        break;
      }
      case SIMCLICK_WIFI_SET_BACKOFF: {
        int *boq = va_arg(val, int *);
        cc->SetBackoffQueueInfo(boq);
        break;
      }
      case SIMCLICK_WIFI_GET_BACKOFF: {
        int *boq = va_arg(val, int *);
        cc->GetBackoffQueueInfo(boq);
        break;
      }
      case SIMCLICK_WIFI_TX_CONTROL: {
        char *txch = va_arg(val, char *);
        cc->HandleTXControl(txch);
        break;
      }
      default:
	r = -1;
	break;
	
    }
    
    va_end(val);
    return r;
}

int
simclick_sim_send(simclick_node_t *simnode,
		  int ifid,int type, const unsigned char* data,int len,
		  simclick_simpacketinfo* pinfo) {

  if (NULL == simnode) {
    return -1;
  }

  /*
   * Bail out if we get a bad ifid
   */
  if (ExtRouter::IFID_LASTIF < ifid) {
    return -1;
  }
  /*
   * XXX should probably use RTTI typesafe casts if they are now
   * reliably implemented across the compilers/platforms we want
   * to run on.
   */
  ClickClassifier* theclassifier = static_cast<ClickClassifier*>(simnode);

  return theclassifier->send_to_if(ifid,type,data,len,pinfo);
}

}

int
ClickClassifier::send_to_if(int ifid,int type,const unsigned char* data,
			 int len,simclick_simpacketinfo* pinfo) {
  int result = 0;

  /*
   * Package raw data into an ns-2 format raw packet, then send
   * it on down the line.
   */

  Packet* pkt = MakeRawPacket(type,ifid,data,len,pinfo);
  //fprintf(stderr,"simclickid == %d\n",simclickid);
  recv(pkt,0);

  return result;
}

int
ClickClassifier::IFReady(int ifid) {
  NsObject* target = NULL;
  int ready = 0;

  // XXX assumes direct ifid->slot mapping
  if (ExtRouter::IFID_KERNELTAP == ifid) {
    return 1;
  }

  target = slot_[ifid];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ready = llext->ready();
  }
  else {
    ready = 0;
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return ready;
}

int
ClickClassifier::GetNSSubtype(int type) {
  switch (type) {
  case SIMCLICK_PTYPE_ETHER:
    return hdr_raw::ETHERNET;

  case SIMCLICK_PTYPE_IP:
    return hdr_raw::IP;

  default:
    return hdr_raw::NONE;
  }

  return hdr_raw::NONE;
}

int
ClickClassifier::GetClickPacketType(int nssubtype) {
  switch (nssubtype) {
  case hdr_raw::ETHERNET:
    return SIMCLICK_PTYPE_ETHER;

  case hdr_raw::IP:
    return SIMCLICK_PTYPE_IP;

  case hdr_raw::MADWIFI:
    return SIMCLICK_PTYPE_ETHER;
  
  default:
    return SIMCLICK_PTYPE_UNKNOWN;
  }

  return SIMCLICK_PTYPE_UNKNOWN;
}

// XXX 
// Normally I'd bitterly complain about code like this. However,
// I don't really want to worry about annoying differences
// between IP header files across different platforms, and I
// want to get this code up and running ASAP. So... I'm defining
// a few things here to handle the minimal packet cracking I
// need to do to create raw packets. If more complicated
// packet munging is called for, something better should be created.
#define NS_ETHER_OFFSET_DADDR 0
#define NS_ETHER_OFFSET_SADDR 6
#define NS_ETHER_HEADER_SIZE 14
#define NS_80211_OFFSET_DADDR 4
#define NS_80211_OFFSET_SADDR 10

void
ClickClassifier::LinkLayerFailedCallback(Packet* p, void* arg) {
  // Hit the callback and then free the packet
  ((ClickClassifier*)arg)->LinkLayerFailed(p);
  Packet::free(p);
}

void
ClickClassifier::LinkLayerFailed(Packet* p) {
  //fprintf(stderr,"XXX Lost a packet!!!\n");
  if (click_initialized_) {
    unsigned char* data = NULL;
    int len = ((PacketData*)(p->userdata()))->size();
    simclick_simpacketinfo simpinfo;
    hdr_cmn* chdr = HDR_CMN(p);
    int ifid = chdr->iface_ + IFID_LASTIF;
    hdr_ip* iphdr = hdr_ip::access(p);
    simpinfo.id = chdr->uid();
    simpinfo.fid = iphdr->flowid();
    simpinfo.txfeedback = (chdr->txfeedback() == hdr_cmn::YES)?1:0;
    simpinfo.zero_copy = packetzerocopy_?1:0;
    hdr_raw* rhdr = hdr_raw::access(p);
    int nssubtype = rhdr->subtype;
    int clicktype = GetClickPacketType(nssubtype);
    simclick_node_t::curtime = GetSimTime();

    unsigned char* pdat = p->accessdata();

    if ( packetzerocopy_ ) {
      PacketData *pdata = (PacketData*)(p->userdata());
      data = pdat;
      pdata->unlink_data();
      //fprintf(stderr, "Datapointer: %p\n",p->accessdata() );
    } else {
      data = new unsigned char[len];
      memcpy(data,pdat,len);
    }

    //fprintf(stderr,"Sending packet up to click...\n");
    simclick_click_send(this,ifid,clicktype,data,len,&simpinfo);
    if (simpinfo.zero_copy == 0) delete[] data;                 //if no zero_copy, we have to delete data
    data = 0;
  }
  else {
    fprintf(stderr,"No click upcall set!\n");
  }
}

Packet*
ClickClassifier::MakeRawPacket(int type,int ifid,const unsigned char* data,
			       int len,simclick_simpacketinfo* pinfo){
  Packet* pkt = Packet::alloc(len);
  /*
   * Shovel raw data into packet
   */
  hdr_raw* rhdr = hdr_raw::access(pkt);
  // nletor -- check if it is a wireless packet and threat it accordingly
  click_wifi_extra *ceh = (click_wifi_extra *) data;
  if (ceh->magic == WIFI_EXTRA_MAGIC) {
	rhdr->subtype = hdr_raw::MADWIFI;  
  } else {
  rhdr->subtype = GetNSSubtype(type);
  }
  unsigned char* pdat = pkt->accessdata();
  memcpy(pdat,data,len);

  /*
   * Set some of the packet header stuff ns-2 wants
   */
  struct hdr_cmn* chdr = HDR_CMN(pkt);
  chdr->iface() = ifid;
  chdr->ptype() = PT_RAW;
  chdr->size() = len;
  if (pinfo->id >= 0) {
    chdr->uid() = pinfo->id;
  }
  else {
    chdr->uid() = Agent::getnextuid();
  }
  rhdr->ns_type = (-1 == pinfo->simtype) ? PT_RAW : pinfo->simtype;
  chdr->xmit_failure_ = LinkLayerFailedCallback;
  chdr->xmit_failure_data_ = (void*)this;

  hdr_ip* iphdr = hdr_ip::access(pkt);
  iphdr->flowid() = 0;
  if (pinfo->fid >= 0) {
    iphdr->flowid() = pinfo->fid;
  }

  /*
   * A packet coming in from click on the kernel tap device is
   * considered to be going up into the node, on any other device
   * going down out of it.
   */
  if (ExtRouter::IFID_KERNELTAP == ifid) {
    chdr->direction() = hdr_cmn::UP;
  }
  else {
    chdr->direction() = hdr_cmn::DOWN;
    // Going out to a network adapter, and we're already 
    // ethernet encapsulated. The ns-2 interface code will
    // tack on ethernet header overhead as well, so we subtract
    // it out of our simulated size here to avoid actual packet
    // size inflation
    if (hdr_raw::ETHERNET == rhdr->subtype) {
      chdr->size() -= NS_ETHER_HEADER_SIZE;
    } else if (hdr_raw::MADWIFI == rhdr->subtype) {	
	  // nsmadwifi
		chdr->size() -= sizeof(click_wifi_extra);
		chdr->size() -= sizeof(click_wifi);
    }
  }

  // If we've got ethernet encapsulation, translate mac address
  // to ns address. Otherwise we're SOL.
  struct hdr_mac* mhdr = HDR_MAC(pkt);
  if (hdr_raw::ETHERNET == rhdr->subtype) {
    MACAddr dmac(data + NS_ETHER_OFFSET_DADDR);
    MACAddr smac(data + NS_ETHER_OFFSET_SADDR);
    if (dmac.is_broadcast()) {
      mhdr->macDA_ = MAC_BROADCAST;
    }
    else {
      mhdr->macDA_ = global_mactonsmacmap_[dmac];
      //fprintf(stderr,"XXX using real MAC: %s -> %d\n",dmac.to_string().c_str(),mhdr->macDA_);
    }
    mhdr->macSA_ = global_mactonsmacmap_[smac];
    chdr->next_hop_ = global_mactonodemap_[dmac];
    chdr->prev_hop_ = global_mactonodemap_[smac];
  } else if (hdr_raw::MADWIFI == rhdr->subtype) {
  	//TODO
	MACAddr dmac(data + NS_ETHER_OFFSET_DADDR + sizeof(click_wifi_extra) + 4); // destination address (STA,AP whatever)
    MACAddr smac(data + NS_ETHER_OFFSET_SADDR + sizeof(click_wifi_extra) + 4); // source address (STA,AP whatever)
	if (dmac.is_broadcast()) {
      mhdr->macDA_ = MAC_BROADCAST;
    } else {
      mhdr->macDA_ = global_mactonsmacmap_[dmac];
    }
    mhdr->macSA_ = global_mactonsmacmap_[smac];
    chdr->next_hop_ = global_mactonodemap_[dmac];
    chdr->prev_hop_ = global_mactonodemap_[smac];
	  
  } else {
    //fprintf(stderr,"XXX using broadcast mac XXX\n");
    mhdr->macDA_ = MAC_BROADCAST;
  }

  // Got an IP packet? Must have come from click, and therefore
  // the next hop is us.
  if ((ExtRouter::IFID_KERNELTAP == ifid) && (hdr_raw::IP == rhdr->subtype)) {
    chdr->next_hop() = nodeaddr_;
  }

  return pkt;
}

struct timeval
ClickClassifier::GetSimTime() {
  struct timeval curtime;
  double ns2time = Scheduler::instance().clock();
  double fracp,intp;
  fracp = modf(ns2time,&intp);
  curtime.tv_sec = (long) intp;
  curtime.tv_usec = (long) (fracp * 1.0e6 + 0.5); 
  return curtime;
}

void
ClickClassifier::trace(char* fmt, ...)
{
	va_list ap;

	if ( !logtarget_ ) {
		printf( "ClickClassifier: need to configure tracetarget\n" );
		return ;
	}
	va_start( ap, fmt );
	vsprintf( logtarget_->pt_->buffer(), fmt, ap );
	logtarget_->pt_->dump();
	va_end( ap );
}

int
ClickClassifier::GetNextPktID()
{
	return Agent::getnextuid();
}

int
ClickClassifier::GetPosition(int *pos) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    pos[0] = round(((MobileNode*)(llext->getMac()->getPhy()->getNode()))->X());
    pos[1] = round(((MobileNode*)(llext->getMac()->getPhy()->getNode()))->Y());
    pos[2] = round(((MobileNode*)(llext->getMac()->getPhy()->getNode()))->Z());
    pos[3] = round(((MobileNode*)(llext->getMac()->getPhy()->getNode()))->speed());
  }
  else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

int
ClickClassifier::SetPosition(int *pos) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ((MobileNode*)(llext->getMac()->getPhy()->getNode()))->set_destination((double)pos[0],(double)pos[1], (double)pos[2], (double)pos[3]);
  }
  else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

int
ClickClassifier::GetPerformanceCounter(int ifid, int *perf_counter)  {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ((Mac802_11*)(llext->getMac()))->getPerformanceCounter(perf_counter);
  }
  else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

int
ClickClassifier::HandleCCAOperation(int *cca) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;

    if ( cca[0] == 0 ) {//read
      double rx_t = ((WirelessPhy*)(llext->getMac()->getPhy()))->getRXThresh();
      double cs_t = ((WirelessPhy*)(llext->getMac()->getPhy()))->getCSThresh();
      double cp_t = ((WirelessPhy*)(llext->getMac()->getPhy()))->getCPThresh();
      //fprintf(stderr,"CCA: %f %f %f\n",rx_t,cs_t,cp_t);
      cca[1] = round((10 * log10(cs_t)));
      cca[2] = round((10 * log10(rx_t)));
      cca[3] = round(cp_t);
    }
    if ( cca[0] == 1 ) {//set
      ((WirelessPhy*)(llext->getMac()->getPhy()))->setCSThresh(pow(10.0,(((double)cca[1])/10.0)));
      ((WirelessPhy*)(llext->getMac()->getPhy()))->setRXThresh(pow(10.0,(((double)cca[2])/10.0)));
      ((WirelessPhy*)(llext->getMac()->getPhy()))->setCPThresh((double)cca[3]);
    }
  } else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

int
ClickClassifier::GetBackoffQueueInfo(int *boq) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ((Mac802_11*)(llext->getMac()))->getBackoffQueueInfo(boq);
  } else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

int
ClickClassifier::SetBackoffQueueInfo(int *boq) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ((Mac802_11*)(llext->getMac()))->setBackoffQueueInfo(boq);
  } else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}


int
ClickClassifier::HandleTXControl(char *txc) {
  NsObject* target = slot_[ExtRouter::IFID_FIRSTIF];
  if (target) {
    LLExt* llext = (LLExt*) target;
    ((Mac802_11*)(llext->getMac()))->handleTXControl(txc);
  } else {
    fprintf(stderr,"ERROR: network interface does not exist\n");
  }

  return 0;
}

