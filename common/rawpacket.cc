/*
 * rawpacket.cc
 * Main file for the raw packet type
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

#include <stdio.h>
#include "agent.h"
#include "packet.h"
#include "rawpacket.h"
#include "extrouter.h"

#include <click/config.h>
#include <click/confparse.hh>
#include <clicknet/ip.h>
#include <clicknet/udp.h>

int hdr_raw::offset_;

/*
 * RawHeaderClass based on the ping example in the ns-2 tutorial.
 */
static class RawHeaderClass : public PacketHeaderClass {
public:
  RawHeaderClass() : PacketHeaderClass("PacketHeader/Raw",sizeof(hdr_raw)){
    bind_offset(&hdr_raw::offset_);
  }
} class_rawhdr;

static class RawClass : public TclClass {
public:
  RawClass() : TclClass("Agent/Raw") {}
  TclObject* create(int,const char*const*) {
    return (new RawAgent());
  }
} class_raw;
  

RawAgent::RawAgent() : Agent(PT_RAW) {
  ipseq_ = 0;
}

int RawAgent::command(int argc,const char*const* argv) {
  if (argc == 2) {
    if (strcmp(argv[1], "send") == 0) {
      char* testmsg = "Howdy Howdy Howdy\n";
      send_udp_str(srcip_,srcport_,destip_,destport_,testmsg);
      return (TCL_OK);
    }
  }
  else if (argc == 3) {
    if (strcmp(argv[1], "send") == 0) {
      const char* testmsg = argv[2];
      send_udp_str(srcip_,srcport_,destip_,destport_,testmsg);
      return (TCL_OK);
    }
    if (strcmp(argv[1], "set-srcip") == 0) {
	if (!Click::cp_ip_address(Click::String(argv[2]), (unsigned char *) &srcip_))
	    return TCL_ERROR;
	return (TCL_OK);
    }
    if (strcmp(argv[1], "set-srcport") == 0) {
      srcport_ = atoi(argv[2]);
      return (TCL_OK);
    }
    if (strcmp(argv[1], "set-destip") == 0) {
	if (!Click::cp_ip_address(Click::String(argv[2]), (unsigned char *) &destip_))
	    return TCL_ERROR;
	return (TCL_OK);
    }
    if (strcmp(argv[1], "set-destport") == 0) {
      destport_ = atoi(argv[2]);
      return (TCL_OK);
    }
  }
  else if (argc == 7) {
    if (strcmp(argv[1],"send-udp") == 0) {
      // saddr,sport,daddr,dport,payload
      // For right now only text strings can be sent
      // as payload.
	u_long saddr, daddr;
	if (!Click::cp_ip_address(Click::String(argv[2]), (unsigned char *) &saddr)
	    || !Click::cp_ip_address(Click::String(argv[4]), (unsigned char *) &daddr))
	    return TCL_ERROR;
      u_short sport = atoi(argv[3]);
      u_short dport = atoi(argv[5]);
      send_udp_str(saddr,sport,daddr,dport,argv[6]);
      // return TCL_OK, so the calling function knows that the
      // command has been processed
      return (TCL_OK);
    }
  }

  // If the command hasn't been processed by RawAgent()::command,
  // call the command() function for the base class
  return (Agent::command(argc, argv));
}

void
RawAgent::sendmsg(int nbytes, const char *flags) {
  // Make a string full of 'A's and use it for the payload
  char* stuff = new char[nbytes];
  memset(stuff,'A',nbytes);
  send_udp(srcip_,srcport_,destip_,destport_,stuff,nbytes);
  delete[] stuff;
  stuff = 0;
}

void
RawAgent::send_udp_str(u_long saddr,u_short sport,u_long daddr,u_short dport,
		       const char* payload) {
  send_udp(saddr,sport,daddr,dport,payload,strlen(payload));
}

void
RawAgent::send_udp(u_long saddr,u_short sport,u_long daddr,u_short dport,
		   const char* payload,int paylen) {
    int packetlen = paylen + sizeof(click_ip) + sizeof(click_udp);
    Packet* pkt = allocpkt(packetlen);
    hdr_cmn* hcmn = HDR_CMN(pkt);
    hcmn->direction() = hdr_cmn::DOWN;
    hcmn->iface() = ExtRouter::IFID_KERNELTAP;
    hcmn->ptype() = PT_RAW;
    hcmn->size() = packetlen;
    // Access the raw header for the new packet:
    hdr_raw* hdr = hdr_raw::access(pkt);
    hdr->subtype = hdr_raw::IP;
    hdr->ns_type = PT_RAW;
    unsigned char* pdat = pkt->accessdata();
    memset(pdat,0,packetlen);

    click_ip *ip = reinterpret_cast<click_ip *>(pdat);
    click_udp *udp = reinterpret_cast<click_udp *>(ip + 1);

    // set up IP header
    ip->ip_v = 4;
    ip->ip_hl = sizeof(click_ip) >> 2;
    ip->ip_len = htons(packetlen);
    ip->ip_id = htons(ipseq_);
    ip->ip_p = IP_PROTO_UDP;
    ip->ip_src.s_addr = saddr;
    ip->ip_dst.s_addr = daddr;
    ip->ip_tos = 0;
    ip->ip_off = 0;
    ip->ip_ttl = 255;

    ip->ip_sum = 0;
#if HAVE_FAST_CHECKSUM
    ip->ip_sum = ip_fast_csum((unsigned char *)ip, sizeof(click_ip) >> 2);
#else
    ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
#endif
    
    // set up UDP header
    udp->uh_sport = htons(sport);
    udp->uh_dport = htons(dport);
    uint16_t len = packetlen - sizeof(click_ip);
    udp->uh_ulen = htons(len);
    udp->uh_sum = 0;
    unsigned csum = click_in_cksum((unsigned char *)udp, len);
    udp->uh_sum = click_in_cksum_pseudohdr(csum, ip, len);
  
    // Send the packet
    send(pkt, 0);
    ipseq_++;
}

void RawAgent::recv(Packet* pkt, Handler*)
{
  // Access the raw header for the received packet
  hdr_raw* hdr = hdr_raw::access(pkt);

  if (hdr_raw::PSTRING == hdr->subtype) {
    unsigned char* pdat = pkt->accessdata();
    unsigned int len = pdat[0];

    // Shovel the string to the screen...
    fwrite(pdat+1,sizeof(char),len,stdout);
  }
}
