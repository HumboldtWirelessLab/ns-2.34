/*
 * clickqueue.cc
 * Special queue which runs the external Click router when it unblocks
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

#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
//#include <stl.h>
//#include <hash_map.h>
#include <map>
#include <math.h>
#include <string>
#include <click/simclick.h>
#include "packet.h"
#include "extrouter.h"
#include "classifier.h"
#include "classifier-ext.h"
#include "classifier-click.h"
#include "clickqueue.h"

static class ClickQueueClass : public TclClass {
public:
	ClickQueueClass() : TclClass("Queue/ClickQueue") {}
	TclObject* create(int, const char*const*) {
		return (new ClickQueue);
	}
} class_click_queue;

int 
ClickQueue::command(int argc, const char*const* argv) {
	if (3 == argc) {
		if (!strcmp(argv[1],"setclickclassifier")) {
			cc_ = (ClickClassifier*)TclObject::lookup(argv[2]);
			return TCL_OK;
		}
	}
	return Queue::command(argc, argv);
}
/*
 * drop-tail
 */
ClickQueue::ClickQueue() { 
	pq_ = new PacketQueue; 
	qlim_ = 1;
	cc_ = 0;
}

ClickQueue::~ClickQueue() {
	delete pq_;
}

int
ClickQueue::is_full() {
	return (pq_->length() >= qlim_);
}

int
ClickQueue::ready() {
	return (!is_full() && !blocked());
}

void ClickQueue::enque(Packet* p)
{
	pq_->enque(p);
	if (pq_->length() > qlim_) {
		fprintf(stderr,"Hey!!! IFQ Overflow!!!\n");
		Packet *pp = pq_->deque();
		drop(pp);
	}
}

Packet* ClickQueue::deque()
{
	Packet* retval = pq_->deque();
	return retval;
}

void ClickQueue::on_unblock() {
	//
	// Queue has space - run the external router 
	// and give it a chance to fill things up.
	//
	if (cc_) {
		Scheduler& s = Scheduler::instance();
		double dcurtime = s.clock();
		double fracp, intp;
		fracp = modf(dcurtime,&intp);
		cc_->simclick_node_t::curtime.tv_sec = intp;
		cc_->simclick_node_t::curtime.tv_usec = (fracp*1.0e6+0.5);
		simclick_click_run(cc_);
	}
}

#if 0
void ClickQueue::runclick() {
	if (is_full()) {
		return;
	}
	//
	// Queue has space - run the external router 
	// and give it a chance to fill things up.
	//
	if (cc_) {
		simclick_click clickinst = cc_->GetClickinst();
		Scheduler& s = Scheduler::instance();
		double dcurtime = s.clock();
		simclick_simstate curstate;
		memset(&curstate,0,sizeof(curstate));
		double fracp, intp;
		fracp = modf(dcurtime,&intp);
		curstate.curtime.tv_sec = intp;
		curstate.curtime.tv_usec = (fracp*1.0e6+0.5);
		simclick_click_run(clickinst,&curstate);
	}
}
#endif
