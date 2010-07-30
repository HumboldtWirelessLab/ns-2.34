/*
 *
 * This classifier is intended for use with external routers bolted
 * on to ns-2, e.g. Click. It uses a combination of the packet direction
 * and interface ID to decide where to send stuff.
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

#ifndef ns_classifier_ext_h
#define ns_classifier_ext_h

#include "object.h"

class Packet;

class ExtClassifier : public Classifier {
 public:
	ExtClassifier();
	virtual ~ExtClassifier();
	
	virtual void recv(Packet* p, Handler* h);

	void setExtRouter(ExtRouter* ext) {extrouter_ = ext;}
	ExtRouter* getExtRouter() {return extrouter_;}

	virtual int classify(Packet*);

 protected:
	virtual int command(int argc, const char*const* argv);
	ExtRouter* extrouter_;
};

#endif
