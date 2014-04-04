/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- 
 *
 * Copyright (c) 1996 Regents of the University of California.
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
 *	Engineering Group at Lawrence Berkeley Laboratory and the Daedalus
 *	research group at UC Berkeley.
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
 *
 * $Header: /cvsroot/nsnam/ns-2/mac/wireless-phy.cc,v 1.28 2007/09/04 04:32:18 tom_henderson Exp $
 *
 * Ported from CMU/Monarch's code, nov'98 -Padma Haldar.
 * wireless-phy.cc
 */
/*
 *	Modified by Nicolas Letor to support wifi elements.
 * 	Performance Analysis of Telecommunication Systems (PATS) research group,
 * 	Interdisciplinary Institute for Broadband Technology (IBBT) & Universiteit Antwerpen.
 */
#include <math.h>
#include <limits>
#include <packet.h>

#include <mobilenode.h>
#include <phy.h>
#include <propagation.h>
#include <modulation.h>
#include <omni-antenna.h>
#include <wireless-phy.h>
#include <packet.h>
#include <ip.h>
#include <agent.h>
#include <trace.h>
#include <sys/param.h>  /* for MIN/MAX */

#include "diffusion/diff_header.h"
#include "rawpacket.h"
#include "packet_anno.h"

//#define BRN_MADWIFI_DEBUG

void Sleep_Timer::expire(Event *) {
	a_->UpdateSleepEnergy();
}


/* ======================================================================
   WirelessPhy Interface
   ====================================================================== */
static class WirelessPhyClass: public TclClass {
public:
        WirelessPhyClass() : TclClass("Phy/WirelessPhy") {}
        TclObject* create(int, const char*const*) {
                return (new WirelessPhy);
        }
} class_WirelessPhy;


WirelessPhy::WirelessPhy() : Phy(), sleep_timer_(this), status_(IDLE)
{
	/*
	 *  It sounds like 10db should be the capture threshold.
	 *
	 *  If a node is presently receiving a packet a a power level
	 *  Pa, and a packet at power level Pb arrives, the following
	 *  comparion must be made to determine whether or not capture
	 *  occurs:
	 *
	 *    10 * log(Pa) - 10 * log(Pb) > 10db
	 *
	 *  OR equivalently
	 *
	 *    Pa/Pb > 10.
	 *
	 */
	bind("CPThresh_", &CPThresh_);
	bind("CSThresh_", &CSThresh_);
	bind("RXThresh_", &RXThresh_);

  Noise_ = 1.0286620123018115e-10;
  bind("Noise_", &Noise_);

  Noise_dbm_ = Noise_;

  if ( CPThresh_ < 0 ) CPThresh_ = pow10(CPThresh_/10);
  if ( CSThresh_ < 0 ) CSThresh_ = pow10(CSThresh_/10);
  if ( RXThresh_ < 0 ) {
    RXThresh_dbm_ = RXThresh_;
    RXThresh_ = pow10(RXThresh_/10);
  } else {
    RXThresh_dbm_ = (10 * log10(RXThresh_));
  }

  if ( Noise_ < 0 ) Noise_ = pow10(Noise_/10);

#ifdef BRN_MADWIFI_DEBUG
  fprintf(stderr,"CP: %e CS: %e RX: %e Noise: %e\n",CPThresh_,CSThresh_,RXThresh_,Noise_);
#endif
	//bind("bandwidth_", &bandwidth_);
	bind("Pt_", &Pt_);
  if ( Pt_ > 1 ) Pt_ = pow10(Pt_/10) / 1000; //dbm to Watt ( /1000)
	bind("freq_", &freq_);
	bind("L_", &L_);

#ifdef BRN_MADWIFI_DEBUG
  fprintf(stderr,"rx: %e %e %f %e\n", RXThresh_, CSThresh_, CPThresh_, Pt_ );
#endif

	// nletor -- multirate madwifi configuration
	RateCount_ = 0;
	bind("RateCount_",&RateCount_);
	if (RateCount_ > 0) {
		//ratelist = RateList(RateCount_);
		//rxlist = RXList(RateCount_);
		char buffer[32];
		for (int i=0; i< RateCount_; i++) {
				double rate;
				sprintf(buffer,"Rate%d",i);
				bind_bw(buffer,&rate);
				ratelist.push_back(rate);
				
				double thr;
				sprintf(buffer,"RXThresh%d",i);
				bind(buffer,&thr);
        if ( thr > 0.99 ) { //looks like db
          rxlist_db.push_back(thr);
          rxlist.push_back(pow10((Noise_dbm_ + thr)/10));
        } else {
          rxlist_db.push_back((10 * log10(thr)) - Noise_dbm_);
          rxlist.push_back(thr);
        }
		}

	}
	//!nletor

#ifdef BRN_MADWIFI_DEBUG
	for (int i = 0; i < RateCount_; i++) {
    fprintf(stderr,"Rate: %f Thr: %e Thr_dbm: %f\n",ratelist[i],rxlist[i],rxlist_db[i]);
  }
#endif
	lambda_ = SPEED_OF_LIGHT / freq_;

	node_ = 0;
	ant_ = 0;
	propagation_ = 0;
	modulation_ = 0;

	// Assume AT&T's Wavelan PCMCIA card -- Chalermek
        //	Pt_ = 8.5872e-4; // For 40m transmission range.
	//      Pt_ = 7.214e-3;  // For 100m transmission range.
	//      Pt_ = 0.2818; // For 250m transmission range.
	//	Pt_ = pow(10, 2.45) * 1e-3;         // 24.5 dbm, ~ 281.8mw
	
	Pt_consume_ = 0.660;  // 1.6 W drained power for transmission
	Pr_consume_ = 0.395;  // 1.2 W drained power for reception

	//	P_idle_ = 0.035; // 1.15 W drained power for idle

	P_idle_ = 0.0;
	P_sleep_ = 0.00;
	T_sleep_ = 10000;
	P_transition_ = 0.00;
	T_transition_ = 0.00; // 2.31 change: Was not initialized earlier
	node_on_=1;
	
	channel_idle_time_ = NOW;
	update_energy_time_ = NOW;
	last_send_time_ = NOW;
	
	sleep_timer_.resched(1.0);

}

int
WirelessPhy::command(int argc, const char*const* argv)
{
	TclObject *obj; 
	Tcl& tcl = Tcl::instance();

	if (argc==2) {
		if (strcasecmp(argv[1], "NodeOn") == 0) {
			node_on();

			if (em() == NULL) 
				return TCL_OK;
			if (NOW > update_energy_time_) {
				update_energy_time_ = NOW;
			}
			return TCL_OK;
		} else if (strcasecmp(argv[1], "NodeOff") == 0) {
			node_off();

			if (em() == NULL) 
				return TCL_OK;
			if (NOW > update_energy_time_) {
				em()->DecrIdleEnergy(NOW-update_energy_time_,
						     P_idle_);
				update_energy_time_ = NOW;
			}
			return TCL_OK;
		} else if (strcasecmp(argv[1], "getantenna") == 0) {
			tcl.result(ant_->name());
			return TCL_OK;
		}

	} else if(argc == 3) {
		if (strcasecmp(argv[1], "setTxPower") == 0) {
			Pt_consume_ = atof(argv[2]);
			return TCL_OK;
		} else if (strcasecmp(argv[1], "setRxPower") == 0) {
			Pr_consume_ = atof(argv[2]);
			return TCL_OK;
		} else if (strcasecmp(argv[1], "setIdlePower") == 0) {
			P_idle_ = atof(argv[2]);
			return TCL_OK;
		}else if (strcasecmp(argv[1], "setSleepPower") == 0) {
			P_sleep_ = atof(argv[2]);
			return TCL_OK;
		}else if (strcasecmp(argv[1], "setSleepTime") == 0) {
			T_sleep_ = atof(argv[2]);
			return TCL_OK;		
		} else if (strcasecmp(argv[1], "setTransitionPower") == 0) {
			P_transition_ = atof(argv[2]);
			return TCL_OK;
		} else if (strcasecmp(argv[1], "setTransitionTime") == 0) {
			T_transition_ = atof(argv[2]);
			return TCL_OK;
		}else if( (obj = TclObject::lookup(argv[2])) == 0) {
			fprintf(stderr,"WirelessPhy: %s lookup of %s failed\n", 
				argv[1], argv[2]);
			return TCL_ERROR;
		}else if (strcmp(argv[1], "propagation") == 0) {
			assert(propagation_ == 0);
			propagation_ = (Propagation*) obj;
			return TCL_OK;
		} else if (strcasecmp(argv[1], "antenna") == 0) {
			ant_ = (Antenna*) obj;
			return TCL_OK;
		} else if (strcasecmp(argv[1], "node") == 0) {
			assert(node_ == 0);
			node_ = (Node *)obj;
			return TCL_OK;
		}
	}
	return Phy::command(argc,argv);
}
 
click_wifi_extra* 
getWifiExtra(Packet* p);

void 
WirelessPhy::sendDown(Packet *p)
{
	/*
	 * Sanity Check
	 */
	assert(initialized());
	
	if (em()) {
			//node is off here...
			if (Is_node_on() != true ) {
			Packet::free(p);
			return;
			}
			if(Is_node_on() == true && Is_sleeping() == true){
			em()-> DecrSleepEnergy(NOW-update_energy_time_,
							P_sleep_);
			update_energy_time_ = NOW;

			}

	}
	/*
	 * Decrease node's energy
	 */
	if(em()) {
		if (em()->energy() > 0) {

		    double txtime = hdr_cmn::access(p)->txtime();
		    double start_time = MAX(channel_idle_time_, NOW);
		    double end_time = MAX(channel_idle_time_, NOW+txtime);
		    double actual_txtime = end_time-start_time;

		    if (start_time > update_energy_time_) {
			    em()->DecrIdleEnergy(start_time - 
						 update_energy_time_, P_idle_);
			    update_energy_time_ = start_time;
		    }

		    /* It turns out that MAC sends packet even though, it's
		       receiving some packets.
		    
		    if (txtime-actual_txtime > 0.000001) {
			    fprintf(stderr,"Something may be wrong at MAC\n");
			    fprintf(stderr,"act_tx = %lf, tx = %lf\n", actual_txtime, txtime);
		    }
		    */

		   // Sanity check
		   double temp = MAX(NOW,last_send_time_);

		   /*
		   if (NOW < last_send_time_) {
			   fprintf(stderr,"Argggg !! Overlapping transmission. NOW %lf last %lf temp %lf\n", NOW, last_send_time_, temp);
		   }
		   */
		   
		   double begin_adjust_time = MIN(channel_idle_time_, temp);
		   double finish_adjust_time = MIN(channel_idle_time_, NOW+txtime);
		   double gap_adjust_time = finish_adjust_time - begin_adjust_time;
		   if (gap_adjust_time < 0.0) {
			   fprintf(stderr,"What the heck ! negative gap time.\n");
		   }

		   if ((gap_adjust_time > 0.0) && (status_ == RECV)) {
			   em()->DecrTxEnergy(gap_adjust_time,
					      Pt_consume_-Pr_consume_);
		   }

		   em()->DecrTxEnergy(actual_txtime,Pt_consume_);
//		   if (end_time > channel_idle_time_) {
//			   status_ = SEND;
//		   }
//
		   status_ = IDLE;

		   last_send_time_ = NOW+txtime;
		   channel_idle_time_ = end_time;
		   update_energy_time_ = end_time;

		   if (em()->energy() <= 0) {
			   em()->setenergy(0);
			   ((MobileNode*)node())->log_energy(0);
		   }

		} else {

			// log node energy
			if (em()->energy() > 0) {
				((MobileNode *)node_)->log_energy(1);
			} 
//
			Packet::free(p);
			return;
		}
	}

	/*
	 *  Stamp the packet with the interface arguments
	 */
	p->txinfo_.stamp((MobileNode*)node(), ant_->copy(), Pt_, lambda_);
	
	// Send the packet
	channel_->recv(p, this);
}

int 
WirelessPhy::sendUp(Packet *p)
{
	/*
	 * Sanity Check
	 */
	assert(initialized());

  double CPThr = CPThresh_;

	//unsigned char* pdat = p->accessdata();
	
	click_wifi_extra *ceh = 0;
	struct hdr_cmn* chdr = HDR_CMN(p);
	if (chdr->ptype() == PT_RAW){
		hdr_raw* rhdr = hdr_raw::access(p);
		if (rhdr->subtype == hdr_raw::MADWIFI) {
			ceh = (click_wifi_extra*)(p->accessdata());
      uint32_t jammer_flag = ceh->flags & (1 << 17);
			//u_int8_t rate = ceh->rate;
			memset(ceh,0,sizeof(click_wifi_extra));
      ceh->flags = jammer_flag;
			ceh->rate = int(p->txinfo_.getRate()/500000);
			ceh->magic = WIFI_EXTRA_MAGIC;
			//printf("Rate is %e Rate is %i and %i %i \n",p->txinfo_.getRate(),rate,int(p->txinfo_.getRate()/500000),ceh->rate);
		} 
	} 

	PacketStamp s;
	double Pr;
	int pkt_recvd = 0;

	Pr = p->txinfo_.getTxPr();
	
	// if the node is in sleeping mode, drop the packet simply
	if (em()) {
			if (Is_node_on()!= true){
			pkt_recvd = 0;
			goto DONE;
			}

			if (Is_sleeping()==true && (Is_node_on() == true)) {
				pkt_recvd = 0;
				goto DONE;
			}
			
	}
	// if the energy goes to ZERO, drop the packet simply
	if (em()) {
		if (em()->energy() <= 0) {
			pkt_recvd = 0;
			goto DONE;
		}
	}

	if(propagation_) {
		s.stamp((MobileNode*)node(), ant_, 0, lambda_);
		Pr = propagation_->Pr(&p->txinfo_, &s, this);

		//fprintf(stderr,"PR: %e\n",Pr);
    //analog to atheros formule -95dBm is lowest sens.
#define BRN_MADWIFI
#ifndef BRN_MADWIFI
    double LPr = (10 * log(Pr * 1000));
    double LRXThr = (10 * log(RXThresh_ * 1000));
#else
    double LPr = (10 * log10(Pr));
    double LRXThr = (10 * log10(RXThresh_));
#endif
    if (LPr < numeric_limits<double>::infinity()){
#ifndef BRN_MADWIFI
      p->txinfo_.RxPrMadwifi = (short int)( (LPr - LRXThr ) * (60.0/100.0) );
#else
      p->txinfo_.RxPrMadwifi = (short int)(LPr - LRXThr );
#endif
      p->txinfo_.RxPrMadwifi = (p->txinfo_.RxPrMadwifi > 60) ? 60 : p->txinfo_.RxPrMadwifi;
    } else {
      p->txinfo_.RxPrMadwifi = 60;
    }
#ifdef BRN_MADWIFI_DEBUG
    fprintf(stderr,"RX-Power: Pr: %f Thr: %f RSSI: %d (%e)\n",LPr, LRXThr,p->txinfo_.RxPrMadwifi,RXThresh_ );
#endif
    if(ceh != 0){
      ceh->rssi = p->txinfo_.RxPrMadwifi;
      ceh->silence = (short int)RXThresh_dbm_;
    }

		if (Pr < CSThresh_) {
			pkt_recvd = 0;
			goto DONE;
		}


		double RXThr = RXThresh_; //rxlist[RateCount_ - 1];
		
		// compare to correct rate,moet nog aan gesleuteld worden
    int i;
		for (i = (RateCount_-1); i >= 0; i--){
			if (ratelist[i] == p->txinfo_.getRate()) {
        //fprintf(stderr,"Used: Rate: %f List: %f threshold: %e\n",p->txinfo_.getRate(),ratelist[i],rxlist[i]);
				RXThr = rxlist[i];
        CPThr = rxlist_db[i];
        break;
			}	/*else {
        fprintf(stderr,"Failed: Rate: %f List: %f threshold: %e\n",p->txinfo_.getRate(),ratelist[i],rxlist[i]);
      }*/
		}
#ifdef BRN_MADWIFI_DEBUG
		fprintf(stderr,"Rate is %e Pr is %e RXThr is %e RXsucc: %d\n",p->txinfo_.getRate(),Pr,RXThr, (int)((Pr>=RXThr)?1:0));
#endif
		
    if ((RateCount_ > 0 ) && (i < 0)) {
      fprintf(stderr,"Rate not found set thr higher than pr, so its drop\n");
      RXThr = Pr + 1.0;
    }

		if (RateCount_ > 0 ) { 
			if (Pr < RXThr) {
			/*
			 * We can detect, but not successfully receive
			 * this packet.
			 */
			hdr_cmn *hdr = HDR_CMN(p);
			hdr->error() = 1;
#if DEBUG > 3
			printf("SM %f.9 _%d_ drop pkt from %d low POWER %e/%e\n",
			       Scheduler::instance().clock(), node()->index(),
			       p->txinfo_.getNode()->index(),
			       Pr,RXThresh);
#endif
		}
		} else {
			if (Pr < RXThresh_) {
				/*
				 * We can detect, but not successfully receive
				 * this packet.
				 */
				hdr_cmn *hdr = HDR_CMN(p);
				hdr->error() = 1;
			#if DEBUG > 3
				printf("SM %f.9 _%d_ drop pkt from %d low POWER %e/%e\n",
				       Scheduler::instance().clock(), node()->index(),
				       p->txinfo_.getNode()->index(),
				       Pr,RXThresh);
			#endif
			}
		}
	}
	if(modulation_) {
		hdr_cmn *hdr = HDR_CMN(p);
		hdr->error() = modulation_->BitError(Pr);
		if(ceh != 0){
			ceh->flags |= WIFI_EXTRA_RX_ERR;	
		}
	}
	
	/*
	 * The MAC layer must be notified of the packet reception
	 * now - ie; when the first bit has been detected - so that
	 * it can properly do Collision Avoidance / Detection.
	 */
	pkt_recvd = 1;

DONE:
	p->txinfo_.getAntenna()->release();

	/* WILD HACK: The following two variables are a wild hack.
	   They will go away in the next release...
	   They're used by the mac-802_11 object to determine
	   capture.  This will be moved into the net-if family of 
	   objects in the future. */
	p->txinfo_.RxPr = Pr;
	//p->txinfo_.CPThresh = CPThresh_;
  p->txinfo_.CPThresh = CPThr;

	/*
	 * Decrease energy if packet successfully received
	 */
	if(pkt_recvd && em()) {

		double rcvtime = hdr_cmn::access(p)->txtime();
		// no way to reach here if the energy level < 0
		
		double start_time = MAX(channel_idle_time_, NOW);
		double end_time = MAX(channel_idle_time_, NOW+rcvtime);
		double actual_rcvtime = end_time-start_time;

		if (start_time > update_energy_time_) {
			em()->DecrIdleEnergy(start_time-update_energy_time_,
					     P_idle_);
			update_energy_time_ = start_time;
		}
		
		em()->DecrRcvEnergy(actual_rcvtime,Pr_consume_);
/*
  if (end_time > channel_idle_time_) {
  status_ = RECV;
  }
*/
		channel_idle_time_ = end_time;
		update_energy_time_ = end_time;

		status_ = IDLE;

		/*
		  hdr_diff *dfh = HDR_DIFF(p);
		  printf("Node %d receives (%d, %d, %d) energy %lf.\n",
		  node()->address(), dfh->sender_id.addr_, 
		  dfh->sender_id.port_, dfh->pk_num, node()->energy());
		*/

		// log node energy
		if (em()->energy() > 0) {
		((MobileNode *)node_)->log_energy(1);
        	} 

		if (em()->energy() <= 0) {  
			// saying node died
			em()->setenergy(0);
			((MobileNode*)node())->log_energy(0);
		}
	}
	
	return pkt_recvd;
}

void
WirelessPhy::node_on()
{

        node_on_= TRUE;
	status_ = IDLE;

       if (em() == NULL)
 	    return;	
   	if (NOW > update_energy_time_) {
      	    update_energy_time_ = NOW;
   	}
}

void 
WirelessPhy::node_off()
{

        node_on_= FALSE;
	status_ = SLEEP;

	if (em() == NULL)
            return;
        if (NOW > update_energy_time_) {
            em()->DecrIdleEnergy(NOW-update_energy_time_,
                                P_idle_);
            update_energy_time_ = NOW;
	}
}

void 
WirelessPhy::node_wakeup()
{

	if (status_== IDLE)
		return;

	if (em() == NULL)
            return;

        if ( NOW > update_energy_time_ && (status_== SLEEP) ) {
		//the power consumption when radio goes from SLEEP mode to IDLE mode
		em()->DecrTransitionEnergy(T_transition_,P_transition_);
		
		em()->DecrSleepEnergy(NOW-update_energy_time_,
				      P_sleep_);
		status_ = IDLE;
	        update_energy_time_ = NOW;
		
		// log node energy
		if (em()->energy() > 0) {
			((MobileNode *)node_)->log_energy(1);
	        } else {
			((MobileNode *)node_)->log_energy(0);   
	        }
	}
}

void 
WirelessPhy::node_sleep()
{
//
//        node_on_= FALSE;
//
	if (status_== SLEEP)
		return;

	if (em() == NULL)
            return;

        if ( NOW > update_energy_time_ && (status_== IDLE) ) {
	//the power consumption when radio goes from IDLE mode to SLEEP mode
	    em()->DecrTransitionEnergy(T_transition_,P_transition_);

            em()->DecrIdleEnergy(NOW-update_energy_time_,
                                P_idle_);
		status_ = SLEEP;
	        update_energy_time_ = NOW;

	// log node energy
		if (em()->energy() > 0) {
			((MobileNode *)node_)->log_energy(1);
	        } else {
			((MobileNode *)node_)->log_energy(0);   
	        }
	}
}
//
void
WirelessPhy::dump(void) const
{
	Phy::dump();
	fprintf(stdout,
		"\tPt: %f, Gt: %f, Gr: %f, lambda: %f, L: %f\n",
		Pt_, ant_->getTxGain(0,0,0,lambda_), ant_->getRxGain(0,0,0,lambda_), lambda_, L_);
	//fprintf(stdout, "\tbandwidth: %f\n", bandwidth_);
	fprintf(stdout, "--------------------------------------------------\n");
}


void WirelessPhy::UpdateIdleEnergy()
{
	if (em() == NULL) {
		return;
	}
	if (NOW > update_energy_time_ && (Is_node_on()==TRUE && status_ == IDLE ) ) {
		  em()-> DecrIdleEnergy(NOW-update_energy_time_,
					P_idle_);
		  update_energy_time_ = NOW;
	}

	// log node energy
	if (em()->energy() > 0) {
		((MobileNode *)node_)->log_energy(1);
        } else {
		((MobileNode *)node_)->log_energy(0);   
        }

//	idle_timer_.resched(10.0);
}

double WirelessPhy::getDist(double Pr, double Pt, double Gt, double Gr,
			    double hr, double ht, double L, double lambda)
{
	if (propagation_) {
		return propagation_->getDist(Pr, Pt, Gt, Gr, hr, ht, L,
					     lambda);
	}
	return 0;
}

//
void WirelessPhy::UpdateSleepEnergy()
{
	if (em() == NULL) {
		return;
	}
	if (NOW > update_energy_time_ && ( Is_node_on()==TRUE  && Is_sleeping() == true) ) {
		  em()-> DecrSleepEnergy(NOW-update_energy_time_,
					P_sleep_);
		  update_energy_time_ = NOW;
		// log node energy
		if (em()->energy() > 0) {
			((MobileNode *)node_)->log_energy(1);
        	} else {
			((MobileNode *)node_)->log_energy(0);   
        	}
	}
	
	//A hack to make states consistent with those of in Energy Model for AF
	int static s=em()->sleep();
	if(em()->sleep()!=s){

		s=em()->sleep();	
		if(s==1)
			node_sleep();
		else
			node_wakeup();			
//		printf("\n AF hack %d\n",em()->sleep());	
	}	
	
	sleep_timer_.resched(10.0);
}
