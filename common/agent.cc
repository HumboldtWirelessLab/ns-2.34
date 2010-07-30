/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1990-1997 Regents of the University of California.
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

#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /cvsroot/nsnam/ns-2/common/agent.cc,v 1.79 2006/02/21 15:20:17 mahrenho Exp $ (LBL)";
#endif

#include <assert.h>
#include <stdlib.h>

#include "config.h"
#include "agent.h"
#include "ip.h"
#include "tcp.h"
#include "flags.h"
#include "address.h"
#include "app.h"
#ifdef HAVE_STL
#include "nix/hdr_nv.h"
#include "nix/nixnode.h"
#endif //HAVE_STL

#include "rawpacket.h"
#include "extrouter.h"

#include <click/config.h>
#include <click/confparse.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

static class AgentClass : public TclClass {
public:
	AgentClass() : TclClass("Agent") {} 
	TclObject* create(int, const char*const*) {
		return (new Agent(PT_NTYPE));
	}
} class_agent;

int Agent::uidcnt_;		/* running unique id */

Agent::Agent(packet_t pkttype) : 
	size_(0), type_(pkttype), 
	channel_(0), traceName_(NULL),
	oldValueList_(NULL), app_(0), rawcvt_(false), et_(0)
{
}

void
Agent::delay_bind_init_all()
{
	delay_bind_init_one("agent_addr_");
	delay_bind_init_one("agent_port_");
	delay_bind_init_one("dst_addr_");
	delay_bind_init_one("dst_port_");
	delay_bind_init_one("fid_");
	delay_bind_init_one("prio_");
	delay_bind_init_one("flags_");
	delay_bind_init_one("ttl_");
	delay_bind_init_one("class_");
	Connector::delay_bind_init_all();
}

int
Agent::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer)
{
	if (delay_bind(varName, localName, "agent_addr_", (int*)&(here_.addr_), tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "agent_port_", (int*)&(here_.port_), tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "dst_addr_", (int*)&(dst_.addr_), tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "dst_port_", (int*)&(dst_.port_), tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "fid_", (int*)&fid_, tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "prio_", (int*)&prio_, tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "flags_", (int*)&flags_, tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "ttl_", &defttl_, tracer)) return TCL_OK;
	if (delay_bind(varName, localName, "class_", (int*)&fid_, tracer)) return TCL_OK;
	return Connector::delay_bind_dispatch(varName, localName, tracer);
}


Agent::~Agent()
{
	if (oldValueList_ != NULL) {
		OldValue *p = oldValueList_;
		while (oldValueList_ != NULL) {
			oldValueList_ = oldValueList_->next_;
			delete p;
			p = oldValueList_; 
		}
	}
}

int Agent::command(int argc, const char*const* argv)
{
	Tcl& tcl = Tcl::instance();
	if (argc == 2) {
		if (strcmp(argv[1], "delete-agent-trace") == 0) {
			if ((traceName_ == 0) || (channel_ == 0))
				return (TCL_OK);
			deleteAgentTrace();
			return (TCL_OK);
		} else if (strcmp(argv[1], "show-monitor") == 0) {
			if ((traceName_ == 0) || (channel_ == 0))
				return (TCL_OK);
			monitorAgentTrace();
			return (TCL_OK);
		} else if (strcmp(argv[1], "close") == 0) {
			close();
			return (TCL_OK);
		} else if (strcmp(argv[1], "listen") == 0) {
                        listen();
                        return (TCL_OK);
                } else if (strcmp(argv[1], "dump-namtracedvars") == 0) {
			enum_tracedVars();
			return (TCL_OK);
		}
		
	}
	else if (argc == 3) {
		if (strcmp(argv[1], "attach") == 0) {
			int mode;
			const char* id = argv[2];
			channel_ = Tcl_GetChannel(tcl.interp(), (char*)id, &mode);
			if (channel_ == 0) {
				tcl.resultf("trace: can't attach %s for writing", id);
				return (TCL_ERROR);
			}
			return (TCL_OK);
		} else if (strcmp(argv[1], "add-agent-trace") == 0) {
			// we need to write nam traces and set agent trace name
			if (channel_ == 0) {
				tcl.resultf("agent %s: no trace file attached", name_);
				return (TCL_OK);
			}
			addAgentTrace(argv[2]);
			return (TCL_OK);
		} else if (strcmp(argv[1], "connect") == 0) {
			connect((nsaddr_t)atoi(argv[2]));
			return (TCL_OK);
		} else if (strcmp(argv[1], "send") == 0) {
			sendmsg(atoi(argv[2]));
			return (TCL_OK);
		} else if (strcmp(argv[1], "set_pkttype") == 0) {
			set_pkttype(packet_t(atoi(argv[2])));
			return (TCL_OK);
		} else if (strcmp(argv[1], "set-srcip") == 0) {
			if (!Click::cp_ip_address(Click::String(argv[2]), (unsigned char *) &srcip_))
				return TCL_ERROR;
			return (TCL_OK);
		} else if (strcmp(argv[1], "set-srcport") == 0) {
			srcport_ = atoi(argv[2]);
			return (TCL_OK);
		}
		else if (strcmp(argv[1], "set-destip") == 0) {
			if (!Click::cp_ip_address(Click::String(argv[2]), (unsigned char *) &destip_))
				return TCL_ERROR;
			return (TCL_OK);
		} else if (strcmp(argv[1], "set-destport") == 0) {
			destport_ = atoi(argv[2]);
			return (TCL_OK);
		} else if (strcmp(argv[1],"rawconvert") == 0) {
			rawcvt_ = atoi(argv[2]);
			return(TCL_OK);
		}
	}
	else if (argc == 4) {	
		if (strcmp(argv[1], "sendmsg") == 0) {
			sendmsg(atoi(argv[2]), argv[3]);
			return (TCL_OK);
		}
	}
	else if (argc == 5) {
		if (strcmp(argv[1], "sendto") == 0) {
			sendto(atoi(argv[2]), argv[3], (nsaddr_t)atoi(argv[4]));
			return (TCL_OK);
		}
	}
	if (strcmp(argv[1], "tracevar") == 0) {
		// wrapper of TclObject's trace command, because some tcl
		// agents (e.g. srm) uses it.
		const char* args[4];
		char tmp[6];
		strcpy(tmp, "trace");
		args[0] = argv[0];
		args[1] = tmp;
		args[2] = argv[2];
		if (argc > 3)
			args[3] = argv[3];
		return (Connector::command(argc, args));
	}
	return (Connector::command(argc, argv));
}

void Agent::flushAVar(TracedVar *v)
{
	char wrk[256], value[128];
	int n;

	// XXX we need to keep track of old values. What's the best way?
	v->value(value, 128);
	if (strcmp(value, "") == 0) 
		// no value, because no writes has occurred to this var
		return;
	sprintf(wrk, "f -t "TIME_FORMAT" -s %d -d %d -n %s -a %s -o %s -T v -x",
		Scheduler::instance().clock(), addr(), dst_.addr_,
		v->name(), traceName_, value); 
	n = strlen(wrk);
	wrk[n] = '\n';
	wrk[n+1] = 0;
	(void)Tcl_Write(channel_, wrk, n+1);
}

void Agent::deleteAgentTrace()
{
	char wrk[256];

	// XXX we don't know InstVar outside of Tcl! Is there any
	// tracedvars hidden in InstVar? If so, shall we have a tclclInt.h?
	TracedVar* var = tracedvar_;
	for ( ;  var != 0;  var = var->next_) 
		flushAVar(var);

	// we need to flush all var values to trace file, 
	// so nam can do backtracing
	sprintf(wrk, "a -t "TIME_FORMAT" -s %d -d %d -n %s -x",
		Scheduler::instance().clock(), here_.addr_,
		dst_.addr_, traceName_); 
	if (traceName_ != NULL)
		delete[] traceName_;
	traceName_ = NULL;
}

OldValue* Agent::lookupOldValue(TracedVar *v)
{
	OldValue *p = oldValueList_;
	while ((p != NULL) && (p->var_ != v))
		p = p->next_;
	return p;
}

void Agent::insertOldValue(TracedVar *v, const char *value)
{
	OldValue *p = new OldValue;
	assert(p != NULL);
	strncpy(p->val_, value, min(strlen(value)+1, TRACEVAR_MAXVALUELENGTH));
	p->var_ = v;
	p->next_ = NULL;
	if (oldValueList_ == NULL) 
		oldValueList_ = p;
	else {
		p->next_ = oldValueList_;
		oldValueList_ = p;
	}
}

// callback from traced variable updates
void Agent::trace(TracedVar* v) 
{
	if (channel_ == 0)
		return;
	char wrk[256], value[128];
	int n;

	// XXX we need to keep track of old values. What's the best way?
	v->value(value, 128);

	// XXX hack: how do I know ns has not started yet?
	// if there's nothing in value, return
	static int started = 0;
	if (!started) {
		Tcl::instance().evalc("[Simulator instance] is-started");
		if (Tcl::instance().result()[0] == '0')
			// Simulator not started, do nothing
			return;
		// remember for next time (so we don't always have to call to tcl)
		started = 1;
	};

	OldValue *ov = lookupOldValue(v);
	if (ov != NULL) {
		sprintf(wrk, 
			"f -t "TIME_FORMAT" -s %d -d %d -n %s -a %s -v %s -o %s -T v",
			Scheduler::instance().clock(), here_.addr_,
			dst_.addr_, v->name(), traceName_, value, ov->val_);
		strncpy(ov->val_, 
			value,
			min(strlen(value)+1, TRACEVAR_MAXVALUELENGTH));
	} else {
		// if there is value, insert it into old value list
		sprintf(wrk, "f -t "TIME_FORMAT" -s %d -d %d -n %s -a %s -v %s -T v",
			Scheduler::instance().clock(), here_.addr_,
			dst_.addr_, v->name(), traceName_, value);
		insertOldValue(v, value);
	}
	n = strlen(wrk);
	wrk[n] = '\n';
	wrk[n+1] = 0;
	(void)Tcl_Write(channel_, wrk, n+1);
}

void Agent::monitorAgentTrace()
{
	char wrk[256];
	int n;
	double curTime = (&Scheduler::instance() == NULL ? 0 : 
			  Scheduler::instance().clock());
	
	sprintf(wrk, "v -t "TIME_FORMAT" -e monitor_agent %d %s",
		curTime, here_.addr_, traceName_);
	n = strlen(wrk);
	wrk[n] = '\n';
	wrk[n+1] = 0;
	if (channel_)
		(void)Tcl_Write(channel_, wrk, n+1);
}

void Agent::addAgentTrace(const char *name)
{
	char wrk[256];
	int n;
	double curTime = (&Scheduler::instance() == NULL ? 0 : 
			  Scheduler::instance().clock());
	
	sprintf(wrk, "a -t "TIME_FORMAT" -s %d -d %d -n %s",
		curTime, here_.addr_, dst_.addr_, name);
	n = strlen(wrk);
	wrk[n] = '\n';
	wrk[n+1] = 0;
	if (channel_)
		(void)Tcl_Write(channel_, wrk, n+1);
	// keep agent trace name
	if (traceName_ != NULL)
		delete[] traceName_;
	traceName_ = new char[strlen(name)+1];
	strcpy(traceName_, name);
}

void Agent::timeout(int)
{
}

/* 
 * Callback to application to notify the reception of a number of bytes  
 */
void Agent::recvBytes(int nbytes)
{
	if (app_)
		app_->recv(nbytes);	
}

/* 
 * Callback to application to notify the termination of a connection  
 */
void Agent::idle()
{
	if (app_)
		app_->resume();
}

/* 
 * Assign application pointer for callback purposes    
 */
void Agent::attachApp(Application *app)
{
	app_ = app;
}

void Agent::close()
{
}

void Agent::listen()
{
}

/* 
 * This function is a placeholder in case applications want to dynamically
 * connect to agents (presently, must be done at configuration time).
 */
void Agent::connect(nsaddr_t /*dst*/)
{
/*
	dst_ = dst;
*/
}

/*
 * Place holders for sending application data
 */ 

void Agent::sendmsg(int /*sz*/, AppData* /*data*/, const char* /*flags*/)
{
	fprintf(stderr, 
	"Agent::sendmsg(int, AppData*, const char*) not implemented\n");
	abort();
}

void Agent::sendmsg(int /*nbytes*/, const char* /*flags*/)
{
}

void Agent::sendto(int /*sz*/, AppData* /*data*/, const char* /*flags*/,
		   nsaddr_t /*dst*/)
{
	fprintf(stderr, 
	"Agent::sendmsg(int, AppData*, const char*) not implemented\n");
	abort();
}

// to support application using message passing
void Agent::sendto(int /*sz*/, AppData* /*data*/, const char* /*flags*/,
		   ns_addr_t /*dst*/)
{
}

/* 
 * This function is a placeholder in case applications want to dynamically
 * connect to agents (presently, must be done at configuration time).
 */
void Agent::sendto(int /*nbytes*/, const char /*flags*/[], nsaddr_t /*dst*/)
{
/*
	dst_ = dst;
	sendmsg(nbytes, flags);
*/
}
// to support application using message passing
void Agent::sendto(int /*nbytes*/, const char /*flags*/[], ns_addr_t /*dst*/)
{
}

bool
Agent::toraw(Packet* p) {
	// XXX What about AppData and other such junk? Need to
	// figure out if anyone actually sends payloads.
	// XXX What about TCP option headers? Won't work with SACK right now
	bool result = false;
	struct hdr_tcp* htcp = HDR_TCP(p);
	struct hdr_ip* hip = HDR_IP(p);
	struct hdr_cmn* hcmn = HDR_CMN(p);
	struct hdr_raw* hraw = hdr_raw::access(p);
	int packetlen = sizeof(click_ip) + hcmn->size_;
	int paylen = hcmn->size_;
	unsigned char* pdat = 0;

	// build packet length
	if (hcmn->ptype_ == PT_ACK || hcmn->ptype_ == PT_TCP)
		packetlen += sizeof(click_tcp);
	else if (hcmn->ptype_ == PT_CBR)
		packetlen += sizeof(click_udp);
	else
		return false;

	// build packet
	p->allocdata(packetlen);
	pdat = p->accessdata();
	memset(pdat, 0, packetlen);

	// build IP header
	click_ip *ip = (click_ip *) pdat;
	ip->ip_v = 4;
	ip->ip_hl = sizeof(click_ip) >> 2;
	ip->ip_len = htons(packetlen);
	ip->ip_id = htons(ipseq_);
	ip->ip_p = (hcmn->ptype_ == PT_CBR ? IP_PROTO_UDP : IP_PROTO_TCP);
	ip->ip_src.s_addr = srcip_;
	ip->ip_dst.s_addr = destip_;
	ip->ip_tos = 0;
	ip->ip_off = 0;
	ip->ip_ttl = hip->ttl_;
	ip->ip_sum = 0;
#if HAVE_FAST_CHECKSUM
	ip->ip_sum = ip_fast_csum((unsigned char *)ip, sizeof(click_ip) >> 2);
#else
	ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));
#endif

	// build TCP/UDP header and payload
	if (paylen > 0)
		memset(pdat + (packetlen - paylen), 'A', paylen);
	
	if (hcmn->ptype_ == PT_CBR) {
		click_udp *udp = (click_udp *) (ip + 1);
		udp->uh_sport = htons(srcport_);
		udp->uh_dport = htons(destport_);
		uint16_t len = packetlen - sizeof(click_ip);
		udp->uh_ulen = htons(len); 
		udp->uh_sum = 0;
		unsigned csum = click_in_cksum((unsigned char *)udp, len);
		udp->uh_sum = click_in_cksum_pseudohdr(csum, ip, len);
	} else {
		click_tcp *tcp = (click_tcp *) (ip + 1);
		tcp->th_sport = htons(srcport_);
		tcp->th_dport = htons(destport_);
		tcp->th_seq = htonl(htcp->seqno_);
		tcp->th_ack = htonl(htcp->ackno_);
		tcp->th_flags2 = 0;
		tcp->th_off = sizeof(click_tcp) >> 2;
		tcp->th_flags = htcp->tcp_flags_;
		if (hcmn->ptype_ == PT_ACK)
			tcp->th_flags |= TH_ACK;
		tcp->th_win = 0; /* XXX */
		tcp->th_urp = 0;
		tcp->th_sum = 0;
		uint16_t len = packetlen - sizeof(click_ip);
		unsigned csum = click_in_cksum((unsigned char *)tcp, len);
		tcp->th_sum = click_in_cksum_pseudohdr(csum, ip, len);
	}

	hcmn->direction() = hdr_cmn::DOWN;
	hcmn->iface() = ExtRouter::IFID_KERNELTAP;
	hraw->subtype = hdr_raw::IP;
	hraw->ns_type = hcmn->ptype();
	hcmn->ptype() = PT_RAW;
	hcmn->size() = packetlen;
	ipseq_++;

	return result;
}

bool
Agent::fromraw(Packet* p) {
	struct hdr_tcp* htcp = HDR_TCP(p);
	//struct hdr_ip* hip = HDR_IP(p);
	struct hdr_cmn* hcmn = HDR_CMN(p);
	//struct hdr_flags* hflg = hdr_flags::access(p);
	struct click_ip* ip = 0;
	struct click_tcp* tcp = 0;
	//struct click_udp* udp = 0;
	unsigned char* pdat = 0;
	bool result = false;

	if (PT_RAW != hcmn->ptype()) return false;

	pdat = p->accessdata();
	ip = (click_ip*)pdat;

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		tcp = (click_tcp*)(pdat + (ip->ip_hl << 2));
		htcp->seqno_ = ntohl(tcp->th_seq);
		htcp->ackno_ = ntohl(tcp->th_ack);
		htcp->tcp_flags_ = tcp->th_flags;
		htcp->hlen_ = (ip->ip_hl << 2) + (tcp->th_off << 2);
		hcmn->ptype_ = (tcp->th_flags & TH_ACK) ? PT_ACK : PT_TCP;
		result = true;
		break;
	case IPPROTO_UDP:
		hcmn->ptype_ = PT_CBR;
		result = true;
		break;
	default:
		result = false;
		break;
	}

	return result;
}

void Agent::recv(Packet* p, Handler*)
{
	if (app_) {
		if (rawcvt_) fromraw(p);
		app_->recv(hdr_cmn::access(p)->size());
	}
	/*
	 * didn't expect packet (or we're a null agent?)
	 */
	Packet::free(p);
}

/*
 * initpkt: fill in all the generic fields of a pkt
 */

void
Agent::initpkt(Packet* p) const
{
	hdr_cmn* ch = hdr_cmn::access(p);
	ch->uid() = uidcnt_++;
	ch->ptype() = type_;
	ch->size() = size_;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value(); // from packet.h (agent is local)
	ch->direction() = hdr_cmn::NONE;

	ch->error() = 0;	/* pkt not corrupt to start with */

	hdr_ip* iph = hdr_ip::access(p);
	iph->saddr() = here_.addr_;
	iph->sport() = here_.port_;
	iph->daddr() = dst_.addr_;
	iph->dport() = dst_.port_;
	
	//DEBUG
	//if (dst_ != -1)
	//  printf("pl break\n");
	
	iph->flowid() = fid_;
	iph->prio() = prio_;
	iph->ttl() = defttl_;

	hdr_flags* hf = hdr_flags::access(p);
	hf->ecn_capable_ = 0;
	hf->ecn_ = 0;
	hf->eln_ = 0;
	hf->ecn_to_echo_ = 0;
	hf->fs_ = 0;
	hf->no_ts_ = 0;
	hf->pri_ = 0;
	hf->cong_action_ = 0;
	hf->qs_ = 0;
#ifdef HAVE_STL

 	hdr_nv* nv = hdr_nv::access(p);
 	if (0)
		printf("Off hdr_nv %d, ip_hdr %d myaddr %d\n",
		       hdr_nv::offset(), hdr_ip::offset(), here_.addr_);
 	NixNode* pNixNode = NixNode::GetNodeObject(here_.addr_);
	// 	if (0)
	//		printf("Node Object %p\n", reinterpret_cast<void *>(pNixNode) );
 	if (pNixNode) { 
 		// If we get non-null, indicates nixvector routing in use
 		// Delete any left over nv in the packet
 		// Get a nixvector to the target (may create new)
 		NixVec* pNv = pNixNode->GetNixVector(dst_.addr_);
 		pNv->Reset();
 		nv->nv() = pNv; // And set the nixvec in the packet
 		nv->h_used = 0; // And reset used portion to 0
 	}
#endif //HAVE_STL
}

/*
 * allocate a packet and fill in all the generic fields
 */
Packet*
Agent::allocpkt() const
{
	Packet* p = Packet::alloc();
	initpkt(p);
	return (p);
}

/* allocate a packet and fill in all the generic fields and allocate
 * a buffer of n bytes for data
 */
Packet*
Agent::allocpkt(int n) const
{
        Packet* p = allocpkt();

	if (n > 0)
	        p->allocdata(n);

	return(p);
}
