/*
 * clickqueue.h
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

#ifndef ns_clickqueue_h
#define ns_clickqueue_h

#include <string.h>
#include "queue.h"
#include "config.h"

/*
 * A degenerate queue designed to work with Click routers. Should only
 * have 1 packet ever sitting in it since the queueing should be handled
 * by Click. Also makes sure that simclick gets run whenever the queue
 * empties and unblocks so that the polling simulated network driver
 * will insert packets when it has them.
 */
class ClickQueue : public Queue {
  public:
	ClickQueue();
	~ClickQueue();
	void enque(Packet*);
	Packet* deque();

	int is_full();
	int ready();
  protected:
	int command(int argc, const char*const* argv); 
	ClickClassifier* cc_;
	virtual void on_unblock();
};

#endif
