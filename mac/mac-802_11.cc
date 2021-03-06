/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*-
 *
 * Copyright (c) 1997 Regents of the University of California.
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
 *
 * $Header: /cvsroot/nsnam/ns-2/mac/mac-802_11.cc,v 1.58 2008/12/14 14:31:59 tom_henderson Exp $
 *
 * Ported from CMU/Monarch's code, nov'98 -Padma.
 * Contributions by:
 *   - Mike Holland
 *   - Sushmita
 */

/*
 *	Modified by Nicolas Letor to support wifi elements.
 * 	Performance Analysis of Telecommunication Systems (PATS) research group,
 * 	Interdisciplinary Institute for Broadband Technology (IBBT) & Universiteit Antwerpen.
 */
#include "delay.h"
#include "connector.h"
#include "agent.h"
#include "packet.h"
#include "rawpacket.h"
#include "packet_anno.h"
#include "random.h"
#include "mobilenode.h"

// #define DEBUG 99

#include "arp.h"
#include "ll.h"
#include "mac.h"
#include "mac-timers.h"
#include "mac-802_11.h"
#include "cmu-trace.h"

// Added by Sushmita to support event tracing
#include "agent.h"
#include "basetrace.h"

#include <click/../../elements/brn/routing/identity/rxtxcontrol.h>

#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#include <map>
#include <string>
#include <stdarg.h>
#include "ll-ext.h"
#include "extrouter.h"
#include <click/simclick.h>
#include <click/bitvector.hh>
#include "classifier-ext.h"
#include "classifier-click.h"
#include "classifier-click.h"
#include "clickqueue.h"

/* our backoff timer doesn't count down in idle times during a
 * frame-exchange sequence as the mac tx state isn't idle; genreally
 * these idle times are less than DIFS and won't contribute to
 * counting down the backoff period, but this could be a real
 * problem if the frame exchange ends up in a timeout! in that case,
 * i.e. if a timeout happens we've not been counting down for the
 * duration of the timeout, and in fact begin counting down only
 * DIFS after the timeout!! we lose the timeout interval - which
 * will is not the REAL case! also, the backoff timer could be NULL
 * and we could have a pending transmission which we could have
 * sent! one could argue this is an implementation artifact which
 * doesn't violate the spec.. and the timeout interval is expected
 * to be less than DIFS .. which means its not a lot of time we
 * lose.. anyway if everyone hears everyone the only reason a ack will
 * be delayed will be due to a collision => the medium won't really be
 * idle for a DIFS for this to really matter!!
 */

inline void
Mac802_11::checkBackoffTimer()
{
	if(is_idle() && mhBackoff_.paused())
		mhBackoff_.resume(phymib_.getDIFS());
	if(! is_idle() && mhBackoff_.busy() && ! mhBackoff_.paused())
		mhBackoff_.pause();
}

inline void
Mac802_11::transmit(Packet *p, double timeout)
{
	tx_active_ = 1;

  if ( p == pktTx_ ) {
    struct hdr_mac802_11* dh = HDR_MAC802_11(p);
    u_int32_t dst = ETHER_ADDR(dh->dh_ra);

    tx_count++;
    rxtx_stats_.tx_no_data_++;

    if ( dst != MAC_BROADCAST ) rxtx_stats_.tx_no_unic_++;
    else rxtx_stats_.tx_no_bcast_++;

  } else if ( p == pktRTS_ ) {
    rts_tx_count++;
    rxtx_stats_.tx_no_rts_++;
  } else if ( p == pktCTRL_ ) {
    hdr_cmn* ch = HDR_CMN(p);
    (void)ch;
    struct ack_frame *af = (struct ack_frame*)p->access(hdr_mac::offset_);

    if ( af->af_fc.fc_subtype == MAC_Subtype_ACK ) {
      rxtx_stats_.tx_no_ack_++;
    } else if ( af->af_fc.fc_subtype == MAC_Subtype_CTS ) {
      rxtx_stats_.tx_no_cts_++;
    }
  }

	if (EOTtarget_) {
		assert (eotPacket_ == NULL);
		eotPacket_ = p->copy();
	}

	/*
	 * If I'm transmitting without doing CS, such as when
	 * sending an ACK, any incoming packet will be "missed"
	 * and hence, must be discarded.
	 */
	if(rx_state_ != MAC_IDLE) {
		//assert(dh->dh_fc.fc_type == MAC_Type_Control);
		//assert(dh->dh_fc.fc_subtype == MAC_Subtype_ACK);
		assert(pktRx_);
		struct hdr_cmn *ch = HDR_CMN(pktRx_);
		ch->error() = 1;        /* force packet discard */
	}

	/*
	 * pass the packet on the "interface" which will in turn
	 * place the packet on the channel.
	 *
	 * NOTE: a handler is passed along so that the Network
	 *       Interface can distinguish between incoming and
	 *       outgoing packets.
	 */
	downtarget_->recv(p->copy(), this);
	mhSend_.start(timeout);
	mhIF_.start(txtime(p));
}

//#define PERFORMANCE_COUNTER_DEBUG 1
//#define DEBUG_PERFORMANCE_COUNTER_BUG 1

char *get_mac_state_string(uint32_t mode) {
  switch (mode) {
    case MAC_IDLE: return "MAC_IDLE";
    case MAC_POLLING: return "MAC_POLLING";
    case MAC_RECV: return "MAC_RECV";
    case MAC_SEND: return "MAC_SEND";
    case MAC_RTS: return "MAC_RTS";
    case MAC_BCN: return "MAC_BCN";
    case MAC_CTS: return "MAC_CTS";
    case MAC_ACK: return "MAC_ACK";
    case MAC_COLL: return "MAC_COLL";
    case MAC_MGMT: return "MAC_MGMT";
  }
  return "unknown";
}

inline void
Mac802_11::setRxState(MacState newState, bool jam)
{
#ifdef PERFORMANCE_COUNTER_DEBUG
  fprintf(stderr, "Node: %d RXState: %d (%s) time: %2.9f cycle: %f busy: %f rx: %f tx: %f\n", (u_int32_t)index_, newState, get_mac_state_string(newState), Scheduler::instance().clock(),
          perf_stats_.cylce_counter,perf_stats_.busy_counter, perf_stats_.rx_counter,perf_stats_.tx_counter );
#endif

  double diff_cycles = Scheduler::instance().clock() - perf_stats_.ts_mode_start;

#ifdef PERFORMANCE_COUNTER_DEBUG
  fprintf(stderr, "Node: %d Time: %f last: %f diff: %f\n",(u_int32_t)index_, Scheduler::instance().clock() , 
          perf_stats_.ts_mode_start, diff_cycles);
#endif

  switch (perf_stats_.current_mode) {
    case PERFORMANCE_MODE_IDLE:
      break;
    case PERFORMANCE_MODE_RX:
      if ( ! perf_stats_.jam ) perf_stats_.rx_counter += diff_cycles;
      perf_stats_.busy_counter += diff_cycles;
      break;
    case PERFORMANCE_MODE_TX:
      if ( newState != MAC_IDLE ) {
        //caused by unicast msg (recv ack while transm. data)
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
        fprintf(stderr,"Set to RX while tranmitting\n");
#endif
        if ( pktRTS_ != NULL ) {
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          fprintf(stderr,"send rts\n");
#endif
          perf_stats_.tx_counter += diff_cycles;
          perf_stats_.busy_counter += diff_cycles;
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          exit(0);
#endif
        } else if ( pktCTRL_ != NULL ) {
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          fprintf(stderr,"send cts\n");
#endif
          perf_stats_.tx_counter += diff_cycles;
          perf_stats_.busy_counter += diff_cycles;
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          exit(0);
#endif
        } else {
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          fprintf(stderr,"send data\n");
#endif
          perf_stats_.tx_counter += diff_cycles;
          perf_stats_.busy_counter += diff_cycles;
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
          exit(0);
#endif
        }
      }
      break;
    case PERFORMANCE_MODE_BUSY:
      perf_stats_.busy_counter += diff_cycles;
      break;
  }

  perf_stats_.cylce_counter += diff_cycles;

  if ( newState == MAC_IDLE ) {
    perf_stats_.current_mode = PERFORMANCE_MODE_IDLE;
  } else if ( newState == MAC_COLL ) {
    perf_stats_.current_mode = PERFORMANCE_MODE_BUSY;
  } else {
    perf_stats_.current_mode = PERFORMANCE_MODE_RX;
  }

  perf_stats_.jam = jam;
  perf_stats_.ts_mode_start = Scheduler::instance().clock();

	rx_state_ = newState;
	checkBackoffTimer();
}

inline void
Mac802_11::setTxState(MacState newState)
{

#ifdef PERFORMANCE_COUNTER_DEBUG
  fprintf(stderr, "Node: %d TXState: %d (%s) time: %2.9f cycle: %f busy: %f rx: %f tx: %f\n", (u_int32_t)index_, newState, get_mac_state_string(newState), Scheduler::instance().clock(),
           perf_stats_.cylce_counter,perf_stats_.busy_counter, perf_stats_.rx_counter,perf_stats_.tx_counter );
#endif

  double diff_cycles = Scheduler::instance().clock() - perf_stats_.ts_mode_start;
  perf_stats_.cylce_counter += diff_cycles;

#ifdef PERFORMANCE_COUNTER_DEBUG
  fprintf(stderr, "Node: %d Time: %f last: %f diff: %f\n",(u_int32_t)index_, Scheduler::instance().clock() , perf_stats_.ts_mode_start, diff_cycles);
#endif

  switch (perf_stats_.current_mode) {
    case PERFORMANCE_MODE_IDLE:
      break;
    case PERFORMANCE_MODE_RX:
        //"Error" caused by unicast msg (tx ack while rec. data). mac switched directly from rx rts/data to tx cts/ack

      if ( (newState != MAC_IDLE) && (newState != MAC_ACK) && (newState != MAC_CTS) ) {
        fprintf(stderr,"Set to TX while receiving: current mode: %d new mode: %d\n",perf_stats_.current_mode, newState);
        if ( pktRTS_ != NULL ) {
          fprintf(stderr,"cts\n");
          fprintf(stderr,"Hangup. Please write email including output and all sim-files!!\n");
          exit(0);
        } else if ( pktCTRL_ != NULL ) {
          fprintf(stderr,"data?\n");
        } else {
          fprintf(stderr,"Hangup. Please write email including output and all sim-files!!\n");
          exit(0);
        }
      }
      if ( ! perf_stats_.jam ) perf_stats_.rx_counter += diff_cycles;
      perf_stats_.busy_counter += diff_cycles;
      break;
    case PERFORMANCE_MODE_TX:
      perf_stats_.tx_counter += diff_cycles;
      perf_stats_.busy_counter += diff_cycles;
      break;
    case PERFORMANCE_MODE_BUSY:
      perf_stats_.busy_counter += diff_cycles;
      break;
  }

  if ( newState == MAC_IDLE ) {
    perf_stats_.current_mode = PERFORMANCE_MODE_IDLE;
  } else {
    perf_stats_.current_mode = PERFORMANCE_MODE_TX;
  }
  perf_stats_.jam = false;
  perf_stats_.ts_mode_start = Scheduler::instance().clock();

  tx_state_ = newState;
	checkBackoffTimer();
}

void
Mac802_11::getPerformanceCounter(int *perf_count)
{
  double diff_cycles = Scheduler::instance().clock() - perf_stats_.ts_mode_start;
  perf_stats_.cylce_counter += diff_cycles;

  switch (perf_stats_.current_mode) {
    case PERFORMANCE_MODE_IDLE:
      break;
    case PERFORMANCE_MODE_RX:
      if ( ! perf_stats_.jam ) perf_stats_.rx_counter += diff_cycles;
      perf_stats_.busy_counter += diff_cycles;
      break;
    case PERFORMANCE_MODE_TX:
      perf_stats_.tx_counter += diff_cycles;
      perf_stats_.busy_counter += diff_cycles;
      break;
    case PERFORMANCE_MODE_BUSY:
      perf_stats_.busy_counter += diff_cycles;
      break;
  }

  perf_stats_.ts_mode_start = Scheduler::instance().clock();

#ifdef PERFORMANCE_COUNTER_DEBUG
  double idle = 0.0;
#endif
  double busy = 0.0;
  double rx = 0.0;
  double tx = 0.0;

  if ( perf_stats_.cylce_counter > 0 ) {
#ifdef PERFORMANCE_COUNTER_DEBUG
    idle = (100 * (perf_stats_.cylce_counter-perf_stats_.busy_counter))/perf_stats_.cylce_counter;
#endif
    busy = (100 * perf_stats_.busy_counter)/perf_stats_.cylce_counter;
    rx = (100 * perf_stats_.rx_counter)/perf_stats_.cylce_counter;
    tx = (100 * perf_stats_.tx_counter)/perf_stats_.cylce_counter;

#ifdef PERFORMANCE_COUNTER_DEBUG
    printf("Node: %d Time: %f Idle: %f Busy: %f RX: %f TX: %f\n",(u_int32_t)index_,perf_stats_.ts_mode_start,idle,busy,rx,tx);
#endif
  }
//#ifdef PERFORMANCE_COUNTER_DEBUG
  else {
    printf("Node: %d no cycles\n",(u_int32_t)index_);
  }
//#endif

  //percentage
  perf_count[0] = round(busy);
  perf_count[1] = round(rx);
  perf_count[2] = round(tx);

  //raw value
  perf_count[3] = round(perf_stats_.cylce_counter * CYCLES_PER_SECONDS);
  perf_count[4] = round(perf_stats_.busy_counter * CYCLES_PER_SECONDS);
  perf_count[5] = round(perf_stats_.rx_counter * CYCLES_PER_SECONDS);
  perf_count[6] = round(perf_stats_.tx_counter * CYCLES_PER_SECONDS);

  perf_stats_.cylce_counter = 0.0;
  perf_stats_.busy_counter = 0.0;
  perf_stats_.rx_counter = 0.0;
  perf_stats_.tx_counter = 0.0;

}

// helper function
click_wifi_extra*
getWifiExtra(Packet* p)
{
  struct hdr_cmn* chdr = HDR_CMN(p);
  if (chdr->ptype() == PT_RAW){
    hdr_raw* rhdr = hdr_raw::access(p);
    if (rhdr->subtype == hdr_raw::MADWIFI) {
      click_wifi_extra* ceh = (click_wifi_extra*)(p->accessdata());
      if (ceh->magic == WIFI_EXTRA_MAGIC){
        return ceh;
      }
    }
  }
  return 0;
}

click_wifi*
getWifi(Packet* p)
{
  struct hdr_cmn* chdr = HDR_CMN(p);
  if (chdr->ptype() == PT_RAW){
    hdr_raw* rhdr = hdr_raw::access(p);
    if (rhdr->subtype == hdr_raw::MADWIFI) {
      click_wifi_extra* ceh = (click_wifi_extra*)(p->accessdata());
      if (ceh->magic == WIFI_EXTRA_MAGIC){
        return (click_wifi*)(p->accessdata() + sizeof(click_wifi_extra));
      }
    }
  }
  return 0;
}

bool
qos_no_ack(Packet *p)
{
  click_wifi *wh = getWifi(p);

  bool has_qos = ((wh->i_fc[0] & MAC_Subtype_QOS) != 0);
  bool no_ack = false;
  if ( has_qos ) {
    //fprintf(stderr,"has qos\n");
    struct Click::wifi_qos_field *qos = (struct Click::wifi_qos_field*)&(wh[1]);
    no_ack = ((qos->qos & NO_ACK) != 0);
    /*if ( no_ack ) {
      fprintf(stderr,"Found no ack\n");
    }*/
  }
  return no_ack;
}

inline bool is_jammer_msg(Packet *p)
{
  click_wifi_extra* ceh = getWifiExtra(p);

  if ( ceh != 0 ) return (((ceh->flags >> 17) & 1) != 0);

  return false;
}

void
Mac802_11::setBackoffQueueInfo(int *boq)
{
  phymib_.setBackoffQueueInfo(boq);
}

void
Mac802_11::getBackoffQueueInfo(int *boq)
{
  phymib_.getBackoffQueueInfo(boq);
}

void
Mac802_11::handleTXControl(char *txc)
{
  struct tx_control_header* txch = (struct tx_control_header*)txc;

  switch (txch->operation) {
    case TX_ABORT:
      if ( tx_control_state_ == TX_CONTROL_BUSY ) {
        if ( memcmp(txch->dst_ea, tx_target_mac_, 6) == 0 ) {

          tx_control_state_ = TX_CONTROL_ABORT;

#if 1
          //fprintf(stderr,"TXP: %p\n", pktTx_);
          transmit_abort(pktTx_, 0);
          tx_control_state_ = TX_CONTROL_IDLE;
#endif
        } else {
          fprintf(stderr,"wrong mac\n");
          txch->flags = 2;
        }
      } else if ( tx_control_state_ == TX_CONTROL_ABORT ) {
        fprintf(stderr,"Abort already on the way\n");
        if ( memcmp(txch->dst_ea, tx_target_mac_, 6) != 0 ) {
          fprintf(stderr,"Abort for another mac??\n");
        }
      } else {
        fprintf(stderr,"is idle\n");
        txch->flags = 1;
      }
      break;
    case SET_BACKOFFSCHEME:
      //fprintf(stderr,"Set Bo: %d\n",txch->bo_scheme);
      if ( bo_scheme_ != NULL ) {
        delete bo_scheme_;
        bo_scheme_ = NULL;
      }

      switch (txch->bo_scheme) {
        case MAC_BACKOFF_SCHEME_DEFAULT:
          break;
        case MAC_BACKOFF_SCHEME_EXPONENTIAL:
          bo_scheme_ = new ExponentialBackoff();
          break;
        case MAC_BACKOFF_SCHEME_FIBONACCI:
          bo_scheme_ = new FibonacciBackoff();
          break;
      }

      if ( bo_scheme_ == NULL ) {
        cw_ = phymib_.getCWMin();
      } else {
        cw_ = bo_scheme_->reset_cw(phymib_.getCWMin(queue_index_),phymib_.getCWMax(queue_index_));
      }

      break;
  }
}

inline void
Mac802_11::transmit_abort(Packet *p, double timeout)
{
  if (!p) {
    fprintf(stderr,"ERROR: Abort with NULL-Packet. Ignore!\n");
    fprintf(stdout,"STDOUT:Abort with NULL-Packet. Ignore!\n");
    return;
  }

  ssrc_ = 0;
  slrc_ = 0;
  rst_cw();

  tx_active_ = 0;
  tx_control_state_ = TX_CONTROL_IDLE;

  setTxState(MAC_IDLE);

  pktTx_ = 0;
  pktRTS_ = 0;

  mhSend_.stop();
  mhIF_.stop();
  mhBackoff_.pause();

  mhBackoff_.start(cw_, is_idle());

  if ( macmib_.getTXFeedback() == 1 ) {
    click_wifi_extra* ceh_feedback = getWifiExtra(p);
    if (ceh_feedback != 0) {

      ceh_feedback->retries = tx_count;
      ceh_feedback->virt_col = (rts_tx_count << 4) + tx_count;
      ceh_feedback->flags |= WIFI_EXTRA_TX;
      ceh_feedback->flags |= WIFI_EXTRA_TX_FAIL;
      ceh_feedback->flags |= Click::WIFI_EXTRA_TX_ABORT;
      ceh_feedback->flags |= Click::WIFI_EXTRA_EXT_RETRY_INFO;
      ceh_feedback->silence = -95;
      ceh_feedback->rssi = 0;
      struct hdr_cmn* ch_feedback = HDR_CMN(p);
      ch_feedback->direction() = hdr_cmn::UP;
      ch_feedback->txfeedback() = hdr_cmn::YES;

      uptarget_->recv(p, (Handler*) 0); // send feedback WIFI_EXTRA_TX

      /*fprintf(stderr, "Connector: %p\n", (Connector*)uptarget_ );
      fprintf(stderr, "LLext: %p\n", (LLExt*)((Connector*)uptarget_)->target() );
      fprintf(stderr, "LL: %p\n", (LL*)((Connector*)uptarget_)->target() );
      fprintf(stderr, "queue: %p\n", (ClickQueue*)((LLExt*)((Connector*)uptarget_)->target())->ifq());*/

      if ( ((Connector*)uptarget_)->target() == NULL ) { //No cmu trace -> then uptarget i LLc
        ((ClickQueue*)((LLExt*)uptarget_)->ifq())->resume_on_abort();
      } else {
        ((ClickQueue*)((LLExt*)((Connector*)uptarget_)->target())->ifq())->resume_on_abort();
      }

    }
  }
}

void
Mac802_11::getRxTxStats(void *rxtx_stats)
{
  memcpy(rxtx_stats, &rxtx_stats_, sizeof(struct rx_tx_stats));
}



/* ======================================================================
   TCL Hooks for the simulator
   ====================================================================== */
static class Mac802_11Class : public TclClass {
public:
	Mac802_11Class() : TclClass("Mac/802_11") {}
	TclObject* create(int, const char*const*) {
	return (new Mac802_11());

}
} class_mac802_11;


/* ======================================================================
   Mac  and Phy MIB Class Functions
   ====================================================================== */

PHY_MIB::PHY_MIB(Mac802_11 *parent)
{
	/*
	 * Bind the phy mib objects.  Note that these will be bound
	 * to Mac/802_11 variables
	 */

	parent->bind("CWMin_", &(CWMin[0]));
	parent->bind("CWMax_", &(CWMax[0]));
	parent->bind("CWMin1_", &(CWMin[1]));
	parent->bind("CWMax1_", &(CWMax[1]));
	parent->bind("CWMin2_", &(CWMin[2]));
	parent->bind("CWMax2_", &(CWMax[2]));
	parent->bind("CWMin3_", &(CWMin[3]));
	parent->bind("CWMax3_", &(CWMax[3]));
	parent->bind("NoHWQueues_", &NoHwQueues);

	parent->bind("SlotTime_", &SlotTime);
	parent->bind("SIFS_", &SIFSTime);
	parent->bind("BeaconInterval_", &BeaconInterval);

	parent->bind("PreambleLength_", &PreambleLength);
	parent->bind("PLCPHeaderLength_", &PLCPHeaderLength);
	parent->bind_bw("PLCPDataRate_", &PLCPDataRate);
}

void
PHY_MIB::setBackoffQueueInfo(int *boq)
{
  int mq = MAC_Max_HW_Queue;
  if ( boq[0] < mq ) mq = boq[0];
  boq[1] = MAC_Max_HW_Queue;

  for ( int q = 0; q < mq; q++) {
     CWMin[q] = boq[2 + q];
     CWMax[q] = boq[2 + boq[0] + q];
  }
}

void
PHY_MIB::getBackoffQueueInfo(int *boq)
{
  if ( boq[0] > MAC_Max_HW_Queue ) boq[0] = MAC_Max_HW_Queue; //how many queues are in the data
  boq[1] = MAC_Max_HW_Queue;                                  //how many queues are present

  for ( int q = 0; q < boq[0]; q++) {
     boq[2 + q] = CWMin[q];
     boq[2 + boq[0] + q] = CWMax[q];
  }
}

MAC_MIB::MAC_MIB(Mac802_11 *parent)
{
	/*
	 * Bind the phy mib objects.  Note that these will be bound
	 * to Mac/802_11 variables
	 */

	parent->bind("RTSThreshold_", &RTSThreshold);
	parent->bind("ShortRetryLimit_", &ShortRetryLimit);
	parent->bind("LongRetryLimit_", &LongRetryLimit);
	parent->bind("ScanType_", &ScanType);
	parent->bind("ProbeDelay_", &ProbeDelay);
	parent->bind("MaxChannelTime_", &MaxChannelTime);
	parent->bind("MinChannelTime_", &MinChannelTime);
	parent->bind("ChannelTime_", &ChannelTime);
  Promisc = 0;
	parent->bind("Promisc_", &Promisc);
	if ( Promisc > 1 ) Promisc = 1;
  TXFeedback = 0;
  parent->bind("TXFeedback_", &TXFeedback);
  if ( TXFeedback > 1 ) TXFeedback = 1;
  FilterDub = 1;
  parent->bind("FilterDub_", &FilterDub);
  if ( FilterDub > 1 ) FilterDub = 1;
  ControlFrames = 1;
  parent->bind("ControlFrames_", &ControlFrames);
  if ( ControlFrames > 1 ) ControlFrames = 1;

  MadwifiTPC = 0;
  parent->bind("MadwifiTPC_", &MadwifiTPC);
  if ( MadwifiTPC > 1 ) MadwifiTPC = 1;

  setRTSThresholdDefault(getRTSThreshold());
}



/* ======================================================================
   Mac Class Functions
   ====================================================================== */
Mac802_11::Mac802_11() :
	Mac(), phymib_(this), macmib_(this), mhIF_(this), mhNav_(this),
	mhRecv_(this), mhSend_(this),
	mhDefer_(this), mhBackoff_(this), mhBeacon_(this), mhProbe_(this)
{

	nav_ = 0.0;
	tx_state_ = rx_state_ = MAC_IDLE;
	tx_active_ = 0;
	eotPacket_ = NULL;
	pktRTS_ = 0;
	pktCTRL_ = 0;
	pktBEACON_ = 0;
	pktASSOCREP_ = 0;
	pktASSOCREQ_ = 0;
	pktAUTHENTICATE_ = 0;
	pktPROBEREQ_ = 0;
	pktPROBEREP_ = 0;
	BeaconTxtime_ = 0;
	infra_mode_ = 0;
	queue_index_ = 0;
  bo_scheme_ = NULL;
  cw_ = phymib_.getCWMin();
  retry_number_ = 0;
	ssrc_ = slrc_ = 0;
	// Added by Sushmita
        et_ = new EventTrace();

	sta_seqno_ = 1;
	cache_ = 0;
	cache_node_count_ = 0;
	client_list = NULL;
	ap_list = NULL;
	queue_head = NULL;
	Pr = 0;
	ap_temp = -1;
	ap_addr = -1;
	tx_mgmt_ = 0;
	associated = 0;
	authenticated = 0;
	OnMinChannelTime = 0;
	OnMaxChannelTime = 0;
	Recv_Busy_ = 0;
	handoff= 0;
//	ssid_ = "0";

	// chk if basic/data rates are set
	// otherwise use bandwidth_ as default;

	Tcl& tcl = Tcl::instance();
	tcl.evalf("Mac/802_11 set basicRate_");
	if (strcmp(tcl.result(), "0") != 0)
		bind_bw("basicRate_", &basicRate_);
	else
		basicRate_ = bandwidth_;

	tcl.evalf("Mac/802_11 set dataRate_");
	if (strcmp(tcl.result(), "0") != 0)
		bind_bw("dataRate_", &dataRate_);
	else
		dataRate_ = bandwidth_;


	bind_bool("bugFix_timer_", &bugFix_timer_);

        EOTtarget_ = 0;
       	bss_id_ = IBSS_ID;

  perf_stats_.current_mode = PERFORMANCE_MODE_IDLE;
  perf_stats_.ts_mode_start = 0.0;

  perf_stats_.cylce_counter = 0.0;
  perf_stats_.busy_counter = 0.0;
  perf_stats_.rx_counter = 0.0;
  perf_stats_.tx_counter = 0.0;

  tx_control_state_ = TX_CONTROL_IDLE;
  memset(tx_target_mac_,0,6);
  memset(tx_source_mac_,0,6);

  memset(&rxtx_stats_, 0, sizeof(struct rx_tx_stats));
}


int
Mac802_11::command(int argc, const char*const* argv)
{
	if (argc == 3) {
		if (strcmp(argv[1], "eot-target") == 0) {
			EOTtarget_ = (NsObject*) TclObject::lookup(argv[2]);
			if (EOTtarget_ == 0)
				return TCL_ERROR;
			return TCL_OK;
		}
		if (strcmp(argv[1], "ap") == 0) {
			ap_addr = addr();
			bss_id_ = addr();
			infra_mode_ = 1;
			mhBeacon_.start((Random::random() % cw_) *
					phymib_.getSlotTime());
			return TCL_OK;
		}
		if (strcmp(argv[1], "ScanType") == 0) {
			if (strcmp(argv[2], "ACTIVE") == 0) {
				ScanType_ = ACTIVE;
				infra_mode_ = 1;
				mhProbe_.start(macmib_.getProbeDelay());
				probe_delay = 1;
			} else if (strcmp(argv[2], "PASSIVE") == 0) {
				ScanType_ = PASSIVE;
				mhProbe_.start(macmib_.getChannelTime());
				infra_mode_ = 1;
			}
			return TCL_OK;
		} else if (strcmp(argv[1], "log-target") == 0) {
			logtarget_ = (NsObject*) TclObject::lookup(argv[2]);
			if(logtarget_ == 0)
				return TCL_ERROR;
			return TCL_OK;
		} else if(strcmp(argv[1], "nodes") == 0) {
			if(cache_) return TCL_ERROR;
			cache_node_count_ = atoi(argv[2]);
			cache_ = new Host[cache_node_count_ + 1];
			assert(cache_);
			bzero(cache_, sizeof(Host) * (cache_node_count_+1 ));
			return TCL_OK;
		} else if(strcmp(argv[1], "eventtrace") == 0) {
			// command added to support event tracing by Sushmita
                        et_ = (EventTrace *)TclObject::lookup(argv[2]);
                        return (TCL_OK);
                }
	}
	return Mac::command(argc, argv);
}

// Added by Sushmita to support event tracing
void Mac802_11::trace_event(char *eventtype, Packet *p)
{
        if (et_ == NULL) return;
        char *wrk = et_->buffer();
        char *nwrk = et_->nbuffer();
        //char *src_nodeaddr =
	//       Address::instance().print_nodeaddr(iph->saddr());
        //char *dst_nodeaddr =
        //      Address::instance().print_nodeaddr(iph->daddr());

        struct hdr_mac802_11* dh = HDR_MAC802_11(p);

        //struct hdr_cmn *ch = HDR_CMN(p);

	if(wrk != 0) {
		sprintf(wrk, "E -t "TIME_FORMAT" %s %2x ",
			et_->round(Scheduler::instance().clock()),
                        eventtype,
                        //ETHER_ADDR(dh->dh_sa)
                        ETHER_ADDR(dh->dh_ta)
                        );
        }
        if(nwrk != 0) {
                sprintf(nwrk, "E -t "TIME_FORMAT" %s %2x ",
                        et_->round(Scheduler::instance().clock()),
                        eventtype,
                        //ETHER_ADDR(dh->dh_sa)
                        ETHER_ADDR(dh->dh_ta)
                        );
        }
        et_->dump();
}

/* ======================================================================
   Debugging Routines
   ====================================================================== */
void
Mac802_11::trace_pkt(Packet *p)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_mac802_11* dh = HDR_MAC802_11(p);
	u_int16_t *t = (u_int16_t*) &dh->dh_fc;

	fprintf(stderr, "\t[ %2x %2x %2x %2x ] %x %s %d\n",
		*t, dh->dh_duration,
		 ETHER_ADDR(dh->dh_ra), ETHER_ADDR(dh->dh_ta),
		index_, packet_info.name(ch->ptype()), ch->size());
}

void
Mac802_11::dump(char *fname)
{
	fprintf(stderr,
		"\n%s --- (INDEX: %d, time: %2.9f)\n",
		fname, index_, Scheduler::instance().clock());

	fprintf(stderr,
		"\ttx_state_: %x, rx_state_: %x, nav: %2.9f, idle: %d\n",
		tx_state_, rx_state_, nav_, is_idle());

	fprintf(stderr,
		"\tpktTx_: %lx, pktRx_: %lx, pktRTS_: %lx, pktCTRL_: %lx, pktBEACON_: %lx, pktASSOCREQ_: %lx, pktASSOCREP_: %lx, pktPROBEREQ_: %lx, pktPROBEREP_: %lx, pktAUTHENTICATE_: %lx, callback: %lx\n", (long) pktTx_, (long) pktRx_, (long) pktRTS_,
		(long) pktCTRL_, (long) pktBEACON_, (long) pktASSOCREQ_, (long) pktASSOCREP_, (long) pktPROBEREQ_, (long) pktPROBEREP_, (long) pktAUTHENTICATE_, (long) callback_);

	fprintf(stderr,
		"\tDefer: %d, Backoff: %d (%d), Recv: %d, Timer: %d Nav: %d\n",
		mhDefer_.busy(), mhBackoff_.busy(), mhBackoff_.paused(),
		mhRecv_.busy(), mhSend_.busy(), mhNav_.busy());
	fprintf(stderr,
		"\tBackoff Expire: %f\n",
		mhBackoff_.expire());
}


/* ======================================================================
   Packet Headers Routines
   ====================================================================== */
inline int
Mac802_11::hdr_dst(char* hdr, int dst )
{
	struct hdr_mac802_11 *dh = (struct hdr_mac802_11*) hdr;

       if (dst > -2) {
               if (bss_id() == ((int)IBSS_ID)) {
			STORE4BYTE(&dst, (dh->dh_ra));
		} else if ( addr() == bss_id_) {
			if ( find_client(dst) == 1 || (u_int32_t) dst == MAC_BROADCAST) {
				STORE4BYTE(&dst, (dh->dh_ra));
				dh->dh_fc.fc_to_ds      = 0;
				dh->dh_fc.fc_from_ds    = 1;
			} else {
				int dst_broadcast;
				dst_broadcast = MAC_BROADCAST;

				STORE4BYTE(&dst_broadcast, (dh->dh_ra));
				STORE4BYTE(&dst, (dh->dh_3a));
				dh->dh_fc.fc_to_ds      = 1;
				dh->dh_fc.fc_from_ds    = 1;
			}
		} else {
			STORE4BYTE(&bss_id_, (dh->dh_ra));
                        STORE4BYTE(&dst, (dh->dh_3a));
		}
	}
       return (u_int32_t)ETHER_ADDR(dh->dh_ra);
}

inline int
Mac802_11::hdr_src(char* hdr, int src )
{
	struct hdr_mac802_11 *dh = (struct hdr_mac802_11*) hdr;
        if(src > -2)
               STORE4BYTE(&src, (dh->dh_ta));
        return ETHER_ADDR(dh->dh_ta);
}

inline int
Mac802_11::hdr_type(char* hdr, u_int16_t type)
{
	struct hdr_mac802_11 *dh = (struct hdr_mac802_11*) hdr;
	if(type)
		STORE2BYTE(&type,(dh->dh_body));
	return GET2BYTE(dh->dh_body);
}


/* ======================================================================
   Misc Routines
   ====================================================================== */
inline int
Mac802_11::is_idle()
{
	if(rx_state_ != MAC_IDLE)
		return 0;
	if(tx_state_ != MAC_IDLE)
		return 0;
	if(nav_ > Scheduler::instance().clock())
		return 0;

	return 1;
}

void
Mac802_11::discard(Packet *p, const char* why)
{
	hdr_mac802_11* mh = HDR_MAC802_11(p);
	hdr_cmn *ch = HDR_CMN(p);

	/* if the rcvd pkt contains errors, a real MAC layer couldn't
	   necessarily read any data from it, so we just toss it now */
	if(ch->error() != 0) {
		Packet::free(p);
		return;
	}

	switch(mh->dh_fc.fc_type) {
	case MAC_Type_Management:
		switch(mh->dh_fc.fc_subtype) {
		case MAC_Subtype_Auth:
			 if((u_int32_t)ETHER_ADDR(mh->dh_ra) == (u_int32_t)index_) {
				drop(p, why);
				return;
			}
			break;

		case MAC_Subtype_AssocReq:
			if((u_int32_t)ETHER_ADDR(mh->dh_ra) == (u_int32_t)index_) {
				drop(p, why);
				return;
			}
			break;
		case MAC_Subtype_AssocRep:
			break;
		case MAC_Subtype_ProbeReq:
			 if((u_int32_t)ETHER_ADDR(mh->dh_ra) == (u_int32_t)index_) {
				drop(p, why);
				return;
			}
			break;
		case MAC_Subtype_ProbeRep:
			break;
		case MAC_Subtype_80211_Beacon:
			break;
		default:
			fprintf(stderr, "invalid MAC Management subtype\n");
			exit(1);
		}
		break;
	case MAC_Type_Control:
		switch(mh->dh_fc.fc_subtype) {
		case MAC_Subtype_RTS:
			 if((u_int32_t)ETHER_ADDR(mh->dh_ta) ==  (u_int32_t)index_) {
				drop(p, why);
				return;
			}
			/* fall through - if necessary */
		case MAC_Subtype_CTS:
		case MAC_Subtype_ACK:
			if((u_int32_t)ETHER_ADDR(mh->dh_ra) == (u_int32_t)index_) {
				drop(p, why);
				return;
			}
			break;
		default:
			fprintf(stderr, "invalid MAC Control subtype\n");
			exit(1);
		}
		break;
	case MAC_Type_Data:
		switch(mh->dh_fc.fc_subtype) {
		case MAC_Subtype_Data:
			if((u_int32_t)ETHER_ADDR(mh->dh_ra) == \
                           (u_int32_t)index_ ||
                          (u_int32_t)ETHER_ADDR(mh->dh_ta) == \
                           (u_int32_t)index_ ||
                          ((u_int32_t)ETHER_ADDR(mh->dh_ra) == MAC_BROADCAST && mh->dh_fc.fc_to_ds == 0)) {
                                drop(p,why);
                                return;
			}
			break;
		default:
			fprintf(stderr, "invalid MAC Data subtype\n");
			exit(1);
		}
		break;
	default:
		fprintf(stderr, "invalid MAC type (%x)\n", mh->dh_fc.fc_type);
		trace_pkt(p);
		exit(1);
	}
	Packet::free(p);
}

void
Mac802_11::capture(Packet *p)
{
	/*
	 * Update the NAV so that this does not screw
	 * up carrier sense.
	 */
	set_nav(usec(phymib_.getEIFS() + txtime(p)));
	Packet::free(p);
  rxtx_stats_.no_cap_++;
}

void
Mac802_11::collision(Packet *p)
{
	switch(rx_state_) {
	case MAC_RECV:
    setRxState(MAC_COLL, is_jammer_msg(p));
    rxtx_stats_.no_nodes_col_++;
    rxtx_stats_.no_packets_col_++;
		/* fall through */
	case MAC_COLL:
		assert(pktRx_);
		assert(mhRecv_.busy());
		/*
		 *  Since a collision has occurred, figure out
		 *  which packet that caused the collision will
		 *  "last" the longest.  Make this packet,
		 *  pktRx_ and reset the Recv Timer if necessary.
		 */
		if(txtime(p) > mhRecv_.expire()) {
			mhRecv_.stop();
			discard(pktRx_, DROP_MAC_COLLISION);
			pktRx_ = p;
			mhRecv_.start(txtime(pktRx_));
		}
		else {
			discard(p, DROP_MAC_COLLISION);
		}
    rxtx_stats_.no_packets_col_++;
		break;
	default:
		assert(0);
	}
}

void
Mac802_11::tx_resume()
{
	double rTime;
	assert(mhSend_.busy() == 0);
	assert(mhDefer_.busy() == 0);

#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
  fprintf(stderr, "Node: %d (txresume) State: %d time: %2.9f \n", (u_int32_t)index_, perf_stats_.current_mode, Scheduler::instance().clock());
#endif

	if(pktCTRL_) {
		/*
		 *  Need to send a CTS or ACK.
		 */
		mhDefer_.start(phymib_.getSIFS());
	} else if(pktRTS_) {
		if (mhBackoff_.busy() == 0) {
			if (bugFix_timer_) {
				mhBackoff_.start(cw_, is_idle(),
						 phymib_.getDIFS());
			}
			else {
				rTime = (Random::random() % cw_) *
					phymib_.getSlotTime();
				mhDefer_.start( phymib_.getDIFS() + rTime);
			}
		}
	} else if(pktTx_) {
    //fprintf(stderr, "mode: RTS: %d busy?: %d\n", (int) tx_state_ != MAC_RTS ? 0 : 1, mhBackoff_.busy());
		if (mhBackoff_.busy() == 0) {
			hdr_cmn *ch = HDR_CMN(pktTx_);
			struct hdr_mac802_11 *mh = HDR_MAC802_11(pktTx_);

			if ( ( ((u_int32_t) ch->size() < macmib_.getRTSThreshold()) ||
             ((u_int32_t) ETHER_ADDR(mh->dh_ra) == MAC_BROADCAST) )  &&
          (tx_state_ != MAC_RTS) ) {
				if (bugFix_timer_) {
					mhBackoff_.start(cw_, is_idle(), phymib_.getDIFS());
				}	else {
					rTime = (Random::random() % cw_) * phymib_.getSlotTime();
					mhDefer_.start(phymib_.getDIFS() + rTime);
				}
      } else {
			  mhDefer_.start(phymib_.getSIFS());
      }
	  }
	} else if(callback_) {
		Handler *h = callback_;
		callback_ = 0;
		h->handle((Event*) 0);
	}
	setTxState(MAC_IDLE);

}

void
Mac802_11::rx_resume()
{
#ifdef DEBUG_PERFORMANCE_COUNTER_BUG
  fprintf(stderr, "Node: %d (rxresume) State: %d time: %2.9f \n", (u_int32_t)index_, perf_stats_.current_mode, Scheduler::instance().clock());
#endif

	assert(pktRx_ == 0);
	assert(mhRecv_.busy() == 0);
	setRxState(MAC_IDLE, false);
}


/* ======================================================================
   Timer Handler Routines
   ====================================================================== */
void
Mac802_11::backoffHandler()
{
	if(addr() != bss_id_ && infra_mode_ == 1) {
		if(check_pktPROBEREQ() == 0)
			return;
		if(check_pktAUTHENTICATE() == 0)
			return;
		if(check_pktASSOCREQ() == 0)
			return;
	}

	if(pktCTRL_) {
		assert(mhSend_.busy() || mhDefer_.busy());
		return;
	}

	if ( addr() == bss_id_ ) {
		if (pktPROBEREP_ && queue_head->frame_priority == 1) {
			if (check_pktPROBEREP() == 0)
				return;
		} else if (pktBEACON_ && queue_head->frame_priority == 2) {
			if (check_pktBEACON() == 0) {
				return;
			}
		} else if (pktAUTHENTICATE_ && queue_head->frame_priority == 3) {
			if (check_pktAUTHENTICATE() == 0)
				return;
		} else if(pktASSOCREP_ && queue_head->frame_priority == 4) {
			if (check_pktASSOCREP() == 0)
				return;
		}
	}

	if(check_pktRTS() == 0)
		return;
	if(check_pktTx() == 0)
		return;
}

void
Mac802_11::BeaconHandler()
{

	mhBeacon_.start(phymib_.getBeaconInterval());
	sendBEACON(index_);

}


// Probe timer is used for multiple purposes. It can be set to 4 different values
// Probe Delay, MinChannelTime and MaxChannelTime -  for Active Scanning
// ChannelTime - Passive Scanning



void
Mac802_11::ProbeHandler()
{
	if (ScanType_ == ACTIVE) {
		if ( (bss_id_ == (int)IBSS_ID || handoff == 1) && OnMinChannelTime == 0 && Recv_Busy_ == 0 && OnMaxChannelTime == 0) {

			if (probe_delay == 1) {   // Probe delay over - Active Scan starts here, when the ap_table has not been built yet
				sendPROBEREQ(MAC_BROADCAST);
				probe_delay = 0;
				return;
			} else {
				checkAssocAuthStatus();	 // MaxChannelTime Over - Handoff is taking place and ap_table is built, complete authentication and (re)association
				return;
			}
		}
		else if (OnMinChannelTime == 1 && Recv_Busy_ == 1 && OnMaxChannelTime == 0) {
			// MinChannelTime Over - receiver indicated busy before timer expiry, hence Probe timer should be continued for MaxChannelTime, to receive all probe responses
			OnMinChannelTime = 0;
			OnMaxChannelTime = 1;
			mhProbe_.start(macmib_.getMaxChannelTime());
			return;
		}
		else if (OnMinChannelTime == 0 && Recv_Busy_ == 1 && OnMaxChannelTime == 1) {
			// MaxChannelTime Over - Active Scanning is over, start authentication and association
			OnMaxChannelTime = 0;
			Recv_Busy_ = 0;
			if (strongest_ap() > -1)
				active_scan();
			else
				printf("No APs in range\n");
			return;
		}
		else if (OnMinChannelTime == 1 && Recv_Busy_ == 0 && OnMaxChannelTime == 0) {
			//printf("Out of range of any Access Point or channel without APs\n");
			OnMinChannelTime = 0;
			//  MinChannelTime Over
			return;
		}
	} else {
		if (ScanType_ == PASSIVE && bss_id_ == (int)IBSS_ID) {
			 //  ChannelTime Over - Passive Scanning is over, start authentication and association
			passive_scan();	 //
			return;
		}
	}
	if (bss_id_ != (int)IBSS_ID && !mhProbe_.busy()) {
	// Start Authentication and Association
		checkAssocAuthStatus();
	}
}

void
Mac802_11::deferHandler()
{
	assert(pktCTRL_ || pktRTS_ || pktTx_);

	if(check_pktCTRL() == 0)
		return;
	assert(mhBackoff_.busy() == 0);
	if(check_pktRTS() == 0)
		return;
	if(check_pktTx() == 0)
		return;
}

void
Mac802_11::navHandler()
{
	if(is_idle() && mhBackoff_.paused())
		mhBackoff_.resume(phymib_.getDIFS());
}

void
Mac802_11::recvHandler()
{
	recv_timer();
}

void
Mac802_11::sendHandler()
{
	send_timer();
}


void
Mac802_11::txHandler()
{
	if (EOTtarget_) {
		assert(eotPacket_);
		EOTtarget_->recv(eotPacket_, (Handler *) 0);
		eotPacket_ = NULL;
	}
	tx_active_ = 0;
}


/* ======================================================================
   The "real" Timer Handler Routines
   ====================================================================== */
void
Mac802_11::send_timer()
{
	switch(tx_state_) {

	case MAC_MGMT:
		if (pktAUTHENTICATE_) {

			if(addr() == bss_id_ ) {
				if (tx_mgmt_ == 3) {
					assert(pktAUTHENTICATE_);
					Packet::free(pktAUTHENTICATE_);
					pktAUTHENTICATE_ = 0;
					shift_priority_queue();
					assert(mhBackoff_.busy() == 0);
					mhBackoff_.start(cw_, is_idle());
					break;
				}
			} else {
				assert(pktAUTHENTICATE_);
				Packet::free(pktAUTHENTICATE_);
				pktAUTHENTICATE_ = 0;
				checkAssocAuthStatus();
				break;
			}

		}
 		if (pktASSOCREQ_) {
			assert(pktASSOCREQ_);
			Packet::free(pktASSOCREQ_);
			pktASSOCREQ_ = 0;
			checkAssocAuthStatus();
			break;
 		}
		if (pktASSOCREP_) {

			if (tx_mgmt_ == 4) {
				assert(pktASSOCREP_);
				Packet::free(pktASSOCREP_);
				pktASSOCREP_ = 0;
				shift_priority_queue();
				assert(mhBackoff_.busy() == 0);
				mhBackoff_.start(cw_, is_idle());
				update_client_table(associating_node_,1,2);
				break;
			}
		}
		if (pktPROBEREQ_) {
			assert(pktPROBEREQ_);
			Packet::free(pktPROBEREQ_);
			pktPROBEREQ_ = 0;
			break;
 		}
		if (pktPROBEREP_) {
			if (tx_mgmt_ == 1) {
				assert(pktPROBEREP_);
				Packet::free(pktPROBEREP_);
				pktPROBEREP_ = 0;

				shift_priority_queue();
				assert(mhBackoff_.busy() == 0);
				mhBackoff_.start(cw_, is_idle());
				break;
			}
		}
	case MAC_BCN:
		assert(pktBEACON_);
		Packet::free(pktBEACON_);
		pktBEACON_ = 0;
		shift_priority_queue();
		assert(mhBackoff_.busy() == 0);
		mhBackoff_.start(cw_, is_idle());
		break;

	case MAC_RTS:
		RetransmitRTS();
		break;
	/*
	 * Sent a CTS, but did not receive a DATA packet.
	 */
	case MAC_CTS:
		assert(pktCTRL_);
		Packet::free(pktCTRL_);
		pktCTRL_ = 0;
		break;
	/*
	 * Sent DATA, but did not receive an ACK packet.
	 */
	case MAC_SEND:
		RetransmitDATA();
		break;
	/*
	 * Sent an ACK, and now ready to resume transmission.
	 */
	case MAC_ACK:
		assert(pktCTRL_);
		Packet::free(pktCTRL_);
		pktCTRL_ = 0;
		break;
	case MAC_IDLE:
		break;
	default:
		assert(0);
	}
	tx_resume();
}


/* ======================================================================
   Outgoing Packet Routines
   ====================================================================== */
int
Mac802_11::check_pktCTRL()
{
	struct hdr_mac802_11 *mh;
	double timeout;

	if(pktCTRL_ == 0)
		return -1;
	if(tx_state_ == MAC_CTS || tx_state_ == MAC_ACK)
		return -1;

	mh = HDR_MAC802_11(pktCTRL_);

	switch(mh->dh_fc.fc_subtype) {
	/*
	 *  If the medium is not IDLE, don't send the CTS.
	 */
	case MAC_Subtype_CTS:
		if(!is_idle()) {
			discard(pktCTRL_, DROP_MAC_BUSY); pktCTRL_ = 0;
			return 0;
		}
		setTxState(MAC_CTS);
		/*
		 * timeout:  cts + data tx time calculated by
		 *           adding cts tx time to the cts duration
		 *           minus ack tx time -- this timeout is
		 *           a guess since it is unspecified
		 *           (note: mh->dh_duration == cf->cf_duration)
		 */
		 timeout = txtime(phymib_.getCTSlen(), basicRate_)
                        + DSSS_MaxPropagationDelay                      // XXX
                        + sec(mh->dh_duration)
                        + DSSS_MaxPropagationDelay                      // XXX
                       - phymib_.getSIFS()
                       - txtime(phymib_.getACKlen(), basicRate_);
		break;
		/*
		 * IEEE 802.11 specs, section 9.2.8
		 * Acknowledments are sent after an SIFS, without regard to
		 * the busy/idle state of the medium.
		 */
	case MAC_Subtype_ACK:
		setTxState(MAC_ACK);
		timeout = txtime(phymib_.getACKlen(), basicRate_);
		break;
	default:
		fprintf(stderr, "check_pktCTRL:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktCTRL_, timeout);
	return 0;
}

int
Mac802_11::check_pktRTS()
{
	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktRTS_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktRTS_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_RTS:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_RTS);
		timeout = txtime(phymib_.getRTSlen(), basicRate_)
			+ DSSS_MaxPropagationDelay                      // XXX
			+ phymib_.getSIFS()
			+ txtime(phymib_.getCTSlen(), basicRate_)
			+ DSSS_MaxPropagationDelay;
		break;
	default:
		fprintf(stderr, "check_pktRTS:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktRTS_, timeout);


	return 0;
}

int
Mac802_11::check_pktTx()
{

	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktTx_ == 0)
		return -1;

	mh = HDR_MAC802_11(pktTx_);

	if (addr() != bss_id_ && bss_id_ != (int)IBSS_ID) {
		if (handoff == 0)
			STORE4BYTE(&bss_id_, (mh->dh_ra));
	}

	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_Data:
		if(! is_idle()) {
			sendRTS(ETHER_ADDR(mh->dh_ra));
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_SEND);
		if(((u_int32_t)ETHER_ADDR(mh->dh_ra) != MAC_BROADCAST) && (!qos_no_ack(pktTx_)))
                        timeout = txtime(pktTx_)
                                + DSSS_MaxPropagationDelay              // XXX
                               + phymib_.getSIFS()
                               + txtime(phymib_.getACKlen(), basicRate_)
                               + DSSS_MaxPropagationDelay;             // XXX
		else
			timeout = txtime(pktTx_);
		break;
	default:
		fprintf(stderr, "check_pktTx:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktTx_, timeout);
	return 0;
}

/*
 * Low-level transmit functions that actually place the packet onto
 * the channel.
 */
void
Mac802_11::sendRTS(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct rts_frame *rf = (struct rts_frame*)p->access(hdr_mac::offset_);

	assert(pktTx_);
	assert(pktRTS_ == 0);

	/*
	 *  If the size of the packet is larger than the
	 *  RTSThreshold, then perform the RTS/CTS exchange.
	 */
	click_wifi_extra* ceh = getWifiExtra(pktTx_);
	if ((ceh != 0) && ((u_int32_t) dst != MAC_BROADCAST)) {
		// madwifi case
		if (!(ceh->flags & WIFI_EXTRA_DO_RTS_CTS)) {
			Packet::free(p);
			//p = 0;
			return;
		}

	} else {
		// normal case
		if( (u_int32_t) HDR_CMN(pktTx_)->size() < macmib_.getRTSThreshold() ||
            (u_int32_t) dst == MAC_BROADCAST) {
			Packet::free(p);
			return;
		}
	}

	ch->uid() = 0;
	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getRTSlen();
	ch->iface() = -2;
	ch->error() = 0;

  if  ( ceh->power > 0){
    if ( macmib_.getMadwifiTPC() == 0 ) p->txinfo_.setPrLevel(2 * ceh->power);
    else p->txinfo_.setPrLevel(ceh->power);
  } else {
    p->txinfo_.setPrLevel(0);      //set power for rts/cts to 0 -> use default power
  }

  p->txinfo_.setRate(basicRate_);

	bzero(rf, MAC_HDR_LEN);

	rf->rf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	rf->rf_fc.fc_type	= MAC_Type_Control;
 	rf->rf_fc.fc_subtype	= MAC_Subtype_RTS;
	rf->rf_fc.fc_to_ds	= 0;
	rf->rf_fc.fc_from_ds	= 0;
 	rf->rf_fc.fc_more_frag	= 0;
 	rf->rf_fc.fc_retry	= 0;
 	rf->rf_fc.fc_pwr_mgt	= 0;
 	rf->rf_fc.fc_more_data	= 0;
 	rf->rf_fc.fc_wep	= 0;
 	rf->rf_fc.fc_order	= 0;


	//rf->rf_duration = RTS_DURATION(pktTx_);
	STORE4BYTE(&dst, (rf->rf_ra));

	/* store rts tx time */
 	ch->txtime() = txtime(ch->size(), basicRate_);

	STORE4BYTE(&index_, (rf->rf_ta));

	/* calculate rts duration field */
	rf->rf_duration = usec(phymib_.getSIFS()
			       + txtime(phymib_.getCTSlen(), basicRate_)
			       + phymib_.getSIFS()
                               + txtime(pktTx_)
			       + phymib_.getSIFS()
			       + txtime(phymib_.getACKlen(), basicRate_));
	pktRTS_ = p;
}

void
Mac802_11::sendCTS(int dst, double rts_duration)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct cts_frame *cf = (struct cts_frame*)p->access(hdr_mac::offset_);

	assert(pktCTRL_ == 0);

	ch->uid() = 0;
	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getCTSlen();


	ch->iface() = -2;
	ch->error() = 0;
	//ch->direction() = hdr_cmn::DOWN;
  p->txinfo_.setRate(basicRate_);
	p->txinfo_.setPrLevel(0);      //set power for rts/cts to 0 -> use default power

	bzero(cf, MAC_HDR_LEN);

	cf->cf_fc.fc_protocol_version = MAC_ProtocolVersion;
	cf->cf_fc.fc_type	= MAC_Type_Control;
	cf->cf_fc.fc_subtype	= MAC_Subtype_CTS;
 	cf->cf_fc.fc_to_ds	= 0;
	cf->cf_fc.fc_from_ds	= 0;
 	cf->cf_fc.fc_more_frag	= 0;
 	cf->cf_fc.fc_retry	= 0;
 	cf->cf_fc.fc_pwr_mgt	= 0;
 	cf->cf_fc.fc_more_data	= 0;
 	cf->cf_fc.fc_wep	= 0;
 	cf->cf_fc.fc_order	= 0;


	//cf->cf_duration = CTS_DURATION(rts_duration);
	STORE4BYTE(&dst, (cf->cf_ra));

	/* store cts tx time */
	ch->txtime() = txtime(ch->size(), basicRate_);

	/* calculate cts duration */
	cf->cf_duration = usec(sec(rts_duration)
                              - phymib_.getSIFS()
                              - txtime(phymib_.getCTSlen(), basicRate_));



	pktCTRL_ = p;

}

void
Mac802_11::sendACK(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct ack_frame *af = (struct ack_frame*)p->access(hdr_mac::offset_);

	assert(pktCTRL_ == 0);

	ch->uid() = 0;
	ch->ptype() = PT_MAC;
	// CHANGE WRT Mike's code
	ch->size() = phymib_.getACKlen();
	ch->iface() = -2;
	ch->error() = 0;
  p->txinfo_.setRate(basicRate_);
	p->txinfo_.setPrLevel(0);      //set power for ACK to 0 -> use default power

	bzero(af, MAC_HDR_LEN);

	af->af_fc.fc_protocol_version = MAC_ProtocolVersion;
 	af->af_fc.fc_type	= MAC_Type_Control;
 	af->af_fc.fc_subtype	= MAC_Subtype_ACK;
 	af->af_fc.fc_to_ds	= 0;
 	af->af_fc.fc_from_ds	= 0;
 	af->af_fc.fc_more_frag	= 0;
 	af->af_fc.fc_retry	= 0;
 	af->af_fc.fc_pwr_mgt	= 0;
 	af->af_fc.fc_more_data	= 0;
 	af->af_fc.fc_wep	= 0;
 	af->af_fc.fc_order	= 0;

 	//af->af_duration = ACK_DURATION();
	STORE4BYTE(&dst, (af->af_ra));

	/* store ack tx time */
 	ch->txtime() = txtime(ch->size(), basicRate_);

	/* calculate ack duration */
 	af->af_duration = 0;

	pktCTRL_ = p;
}

void
Mac802_11::sendDATA(Packet *p)
{
	hdr_cmn* ch = HDR_CMN(p);
	hdr_raw* rhdr = hdr_raw::access(p);
	struct hdr_mac802_11* dh = HDR_MAC802_11(p);

	u_int32_t dst = ETHER_ADDR(dh->dh_ra);
	assert(pktTx_ == 0);

	/*
	 * Update the MAC header
	 */
	click_wifi_extra* ceh = getWifiExtra(p);
	if (rhdr->subtype == hdr_raw::MADWIFI) {
#ifdef BRN_DEBUG
		printf("SendData: %d %d\n",ch->size(),phymib_.getHdrLen11_click(true));
#endif
		ch->size() += phymib_.getHdrLen11_click(true);
	} else {
#ifdef BRN_DEBUG
		printf("SendData: %d %d\n",ch->size(),phymib_.getHdrLen11());
#endif
		ch->size() += phymib_.getHdrLen11();
	}

	dh->dh_fc.fc_protocol_version = MAC_ProtocolVersion;
	dh->dh_fc.fc_type       = MAC_Type_Data;
	dh->dh_fc.fc_subtype    = MAC_Subtype_Data;
	if ( bss_id_ != (int)IBSS_ID ) {
		if ( index_ != ap_addr ) {
		dh->dh_fc.fc_to_ds	= 1;
		dh->dh_fc.fc_from_ds	= 0;
		}
	} else {
		dh->dh_fc.fc_to_ds	= 0;
		dh->dh_fc.fc_from_ds	= 0;
	}

	dh->dh_fc.fc_more_frag  = 0;
	dh->dh_fc.fc_retry      = 0;
	dh->dh_fc.fc_pwr_mgt    = 0;
	dh->dh_fc.fc_more_data  = 0;
	dh->dh_fc.fc_wep        = 0;
	dh->dh_fc.fc_order      = 0;

#ifdef BRN_DEBUG
	printf("SendDataTxSize: %d\n",ch->size());
#endif
	/* store data tx time */
 	ch->txtime() = txtime(ch->size(), dataRate_);
	p->txinfo_.setPrLevel(0);

	if((dst != MAC_BROADCAST) && (!qos_no_ack(p))) {
		/* store data tx time for unicast packets */
		ch->txtime() = txtime(ch->size(), dataRate_);
		p->txinfo_.setRate(dataRate_);

		// nsmadwifi
		if ( ceh != 0 ){

			if ( ceh->rate > 0){
				double rate = ((short int)(ceh->rate))*500000;
				ch->txtime() = txtime(ch->size(),rate);
				p->txinfo_.setRate(rate);
			}

			if	( ceh->power > 0){
        //fprintf(stderr, "Uni Power: %d\n",ceh->power);
				p->txinfo_.setPrLevel(ceh->power);
        if ( macmib_.getMadwifiTPC() == 0 ) p->txinfo_.setPrLevel(2 * ceh->power);
			}
		}

		dh->dh_duration = usec(txtime(phymib_.getACKlen(), basicRate_)
				       + phymib_.getSIFS());



	} else {
		/* store data tx time for broadcast packets (see 9.6) */
		ch->txtime() = txtime(ch->size(), basicRate_);
		p->txinfo_.setRate(basicRate_);
		p->txinfo_.setPrLevel(0);

    // nsmadwifi
    if ( ceh != 0 ) {
      if ( ceh->rate > 0){
        double rate = ((short int)(ceh->rate))*500000;
        ch->txtime() = txtime(ch->size(),rate);
        p->txinfo_.setRate(rate);
      }
      if  ( ceh->power > 0){
        //fprintf(stderr, "bcast Power: %d\n",ceh->power);
        p->txinfo_.setPrLevel(ceh->power);
        if ( macmib_.getMadwifiTPC() == 0 ) p->txinfo_.setPrLevel(2 * ceh->power);
      }
    }
    dh->dh_duration = 0;

	}

	p->txinfo_.setChannel(0);

	pktTx_ = p;
}

/* ======================================================================
   Retransmission Routines
   ====================================================================== */
void
Mac802_11::RetransmitRTS()
{
	assert(pktTx_);
	assert(pktRTS_);
	assert(mhBackoff_.busy() == 0);
	macmib_.RTSFailureCount++;


	ssrc_ += 1;			// STA Short Retry Count

	bool failure = false;
	double count = ssrc_;

	click_wifi_extra* ceh = getWifiExtra(pktTx_);
	if ((ceh != 0) && (ceh->max_tries != 0) && (tx_control_state_ != TX_CONTROL_ABORT)){
		ceh->retries = ssrc_;
		if (count >= ceh->max_tries){
      failure = true;
		}
	} else {
	  if(ssrc_ >= macmib_.getShortRetryLimit() || (tx_control_state_ == TX_CONTROL_ABORT)) {
			failure = true;
		}
	}

	if(failure){
		discard(pktRTS_, DROP_MAC_RETRY_COUNT_EXCEEDED); pktRTS_ = 0;
		/* tell the callback the send operation failed
		   before discarding the packet */
		hdr_cmn *ch = HDR_CMN(pktTx_);
		if (ch->xmit_failure_) {
                        /*
                         *  Need to remove the MAC header so that
                         *  re-cycled packets don't keep getting
                         *  bigger.
                         */
			ch->size() -= phymib_.getHdrLen11();
                        ch->xmit_reason_ = XMIT_REASON_RTS;
                        ch->xmit_failure_(pktTx_->copy(),
                                          ch->xmit_failure_data_);
                } else {
                  ch->size() -= phymib_.getHdrLen11();
                  ch->xmit_reason_ = XMIT_REASON_RTS;
                }

		if (ceh != 0){
      //TODO: check: set retries to shortRetries?
			ceh->retries--;
      ceh->virt_col = (rts_tx_count << 4) + tx_count;;
      //fprintf(stderr, "Sum long: %d short: %d retries: %d (%d/%d/%d)\n",slrc_, ssrc_, ceh->retries, rts_tx_count, tx_count, ceh->virt_col);
			Packet* p2 = pktTx_->copy();
			click_wifi_extra* ceh2 = getWifiExtra(p2);
			ceh2->flags |= WIFI_EXTRA_TX_FAIL;
			ceh2->flags |= WIFI_EXTRA_TX;
      ceh2->flags |= Click::WIFI_EXTRA_EXT_RETRY_INFO;
			struct hdr_cmn* ch2 = HDR_CMN(p2);
			ch2->direction() = hdr_cmn::UP;
			ch2->txfeedback() = hdr_cmn::YES;
			//printf("(%d)....discarding RTS:%x\n",index_,pktRTS_);

      ch->direction() = hdr_cmn::UP;
      ch->txfeedback() = hdr_cmn::YES;
			discard(pktTx_, DROP_MAC_RETRY_COUNT_EXCEEDED);
			uptarget_->recv(p2, (Handler*) 0);

		} else {
			//printf("(%d)....discarding RTS:%x\n",index_,pktRTS_);
			discard(pktTx_, DROP_MAC_RETRY_COUNT_EXCEEDED);
		}

		pktRTS_ = 0;
		pktTx_ = 0;
		ssrc_ = 0;
		rst_cw();
	} else {
		struct rts_frame *rf;
		rf = (struct rts_frame*)pktRTS_->access(hdr_mac::offset_);
		rf->rf_fc.fc_retry = 1;

		inc_cw();
		mhBackoff_.start(cw_, is_idle());
	}
}

void
Mac802_11::RetransmitDATA()
{
	struct hdr_cmn *ch;
	struct hdr_mac802_11 *mh;
	u_int32_t *rcount, thresh;
	assert(mhBackoff_.busy() == 0);

	assert(pktTx_);
	assert(pktRTS_ == 0);

	ch = HDR_CMN(pktTx_);
	mh = HDR_MAC802_11(pktTx_);

	/*
	 *  Broadcast packets don't get ACKed and therefore
	 *  are never retransmitted.
	 */
	if(((u_int32_t)ETHER_ADDR(mh->dh_ra) == MAC_BROADCAST) || (qos_no_ack(pktTx_))) {
    if ( macmib_.getTXFeedback() == 1 ) {
      Packet* p_feedback = 0; //prepare feedback WIFI_EXTRA_TX
      click_wifi_extra* ceh = getWifiExtra(pktTx_);
      if (ceh != 0){
        p_feedback = pktTx_->copy();
        click_wifi_extra* ceh_feedback = getWifiExtra(p_feedback);
        ceh_feedback->flags |= WIFI_EXTRA_TX;
        ceh_feedback->flags |= Click::WIFI_EXTRA_EXT_RETRY_INFO;
        ceh_feedback->silence = -95;
        ceh_feedback->rssi = 0;
        ceh_feedback->virt_col = (rts_tx_count << 4) + tx_count;
        //fprintf(stderr, "Sum long: %d short: %d retries: %d (%d/%d/%d)\n",slrc_, ssrc_, ceh->retries, rts_tx_count, tx_count, ceh->virt_col);
        struct hdr_cmn* ch_feedback = HDR_CMN(p_feedback);
        ch_feedback->direction() = hdr_cmn::UP;
        ch_feedback->txfeedback() = hdr_cmn::YES;
        uptarget_->recv(p_feedback, (Handler*) 0); // send feedback WIFI_EXTRA_TX
        tx_control_state_ = TX_CONTROL_IDLE;
      }
    }
		Packet::free(pktTx_);
		pktTx_ = 0;

		/*
		 * Backoff at end of TX.
		 */
		rst_cw();
//		printf("CW (retransdata): %d\n", cw_);
		mhBackoff_.start(cw_, is_idle());

		return;
	}

	macmib_.ACKFailureCount++;

  //if ceh (click) then also check set RTS/CTS flag. if so, then use short retrys
  bool ceh_rtscts_flag_is_set = false;
  (void)ceh_rtscts_flag_is_set;

  click_wifi_extra* ceh = getWifiExtra(pktTx_);
  if (ceh != 0) ceh_rtscts_flag_is_set = ((ceh->flags & WIFI_EXTRA_DO_RTS_CTS) != 0);

	/*if(((u_int32_t) ch->size() <= macmib_.getRTSThreshold()) || ceh_rtscts_flag_is_set) {
                rcount = &ssrc_;
               thresh = macmib_.getShortRetryLimit();
        } else {
                rcount = &slrc_;
               thresh = macmib_.getLongRetryLimit();
        }
*/
  rcount = &slrc_;
  thresh = macmib_.getLongRetryLimit();

	(*rcount)++;

// Bug reported by Mayur - Handoff triggered in Ad-hoc mode too. Bug resolved by checking if it is infrastructure mode before handoff trigger.

	if (bss_id_ != (int)IBSS_ID && *rcount == 3 && handoff == 0) {
		//start handoff process
		printf("Client %d: Handoff Attempted\n",index_);
		associated = 0;
		authenticated = 0;
		handoff = 1;
		ScanType_ = ACTIVE;
		sendPROBEREQ(MAC_BROADCAST);
		return;
	}
	bool failure = false;
	int32_t count = *rcount;

	if ((ceh != 0) && (ceh->max_tries != 0) && (tx_control_state_ != TX_CONTROL_ABORT)){
		ceh->retries = (*rcount);

    //fprintf(stderr,"Rateindex: %d\n",count);
		if (count < ceh->max_tries){
			double rate = ((short int)(ceh->rate))*500000;
			ch->txtime() = txtime(ch->size(),rate);
			pktTx_->txinfo_.setRate(rate);
		} else if ( (count-=ceh->max_tries) < ceh->max_tries1 ){
			double rate = ((short int)(ceh->rate1))*500000;
			ch->txtime() = txtime(ch->size(),rate);
			pktTx_->txinfo_.setRate(rate);
			ceh->flags |= WIFI_EXTRA_TX_USED_ALT_RATE;
		} else if ( (count-=ceh->max_tries1) < ceh->max_tries2 ){
			double rate = ((short int)(ceh->rate2))*500000;
			ch->txtime() = txtime(ch->size(),rate);
			pktTx_->txinfo_.setRate(rate);
			ceh->flags |= WIFI_EXTRA_TX_USED_ALT_RATE;
		} else if ( (count-=ceh->max_tries2) < ceh->max_tries3 ){
			double rate = ((short int)(ceh->rate3))*500000;
			ch->txtime() = txtime(ch->size(),rate);
			pktTx_->txinfo_.setRate(rate);
			ceh->flags |= WIFI_EXTRA_TX_USED_ALT_RATE;
		} else {
			failure = true;
		}

	} else {
    if (ceh->max_tries == 0) {
      ceh->retries = (*rcount); //TODO: workaround. fix it (no tries in the case that max_tries == 0 (missing setTXRate in click) and TXfailure
      ceh->virt_col = (rts_tx_count << 4) + tx_count;
    }

		if ((*rcount >= thresh) || (tx_control_state_ == TX_CONTROL_ABORT)) {
			failure = true;
		}
	}

	//if(*rcount >= thresh) {
	if(failure){
		/* IEEE Spec section 9.2.3.5 says this should be greater than
		   or equal */
		macmib_.FailedCount++;
		/* tell the callback the send operation failed
		   before discarding the packet */
		hdr_cmn *ch = HDR_CMN(pktTx_);
		if (ch->xmit_failure_) {
                        ch->size() -= phymib_.getHdrLen11();
			ch->xmit_reason_ = XMIT_REASON_ACK;
                        ch->xmit_failure_(pktTx_->copy(),
                                          ch->xmit_failure_data_);
                } else {
                  ch->size() -= phymib_.getHdrLen11();
                  ch->xmit_reason_ = XMIT_REASON_ACK;
                }

		if (ceh != 0){
			ceh->retries--;
      ceh->virt_col = (rts_tx_count << 4) + tx_count;;
      //fprintf(stderr, "Sum long: %d short: %d retries: %d (%d)\n",slrc_, ssrc_, ceh->retries, slrc_+ssrc_ );
			Packet* p2 = pktTx_->copy();
			click_wifi_extra* ceh2 = getWifiExtra(p2);
			ceh2->flags |= WIFI_EXTRA_TX_FAIL;
			ceh2->flags |= WIFI_EXTRA_TX;
      ceh2->flags |= Click::WIFI_EXTRA_EXT_RETRY_INFO;

			ceh2->silence = -95;
			ceh2->rssi = 0;
			struct hdr_cmn* ch2 = HDR_CMN(p2);
			ch2->direction() = hdr_cmn::UP;
			ch2->txfeedback() = hdr_cmn::YES;
			// drop packet first
			discard(pktTx_, DROP_MAC_RETRY_COUNT_EXCEEDED);
			// send feedback to madwifi
			uptarget_->recv(p2, (Handler*) 0);
			tx_control_state_ = TX_CONTROL_IDLE;
		} else {
			discard(pktTx_, DROP_MAC_RETRY_COUNT_EXCEEDED);
			//printf("(%d)DATA discarded: count exceeded\n",index_);
		}

		pktTx_ = 0;
		*rcount = 0;
		rst_cw();
	}
	else {
		struct hdr_mac802_11 *dh;
		dh = HDR_MAC802_11(pktTx_);
		dh->dh_fc.fc_retry = 1;

		// nletor
		click_wifi* cwh = getWifi(pktTx_);
		if (cwh != 0){
			cwh->i_fc[1] |= WIFI_FC1_RETRY;
		}
		//!nletor

		sendRTS(ETHER_ADDR(mh->dh_ra));
		inc_cw();
		//printf("CW (Retry %d): %d\n",*rcount,cw_);
		mhBackoff_.start(cw_, is_idle());
	}
}

void
Mac802_11::RetransmitPROBEREP()
{
double rTime;

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());


		}

	}

}

void
Mac802_11::RetransmitAUTHENTICATE()
{
double rTime;

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());


		}

	}

}

void
Mac802_11::RetransmitASSOCREP()
{
double rTime;

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());

				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());

		}

	}

}
/* ======================================================================
   Incoming Packet Routines
   ====================================================================== */
void
Mac802_11::send(Packet *p, Handler *h)
{
  uint8_t macbcast[] = {255,255,255,255,255,255};

	double rTime;
	struct hdr_mac802_11* dh = HDR_MAC802_11(p);
  uint8_t *dst_mac = dh->dh_ra;

	EnergyModel *em = netif_->node()->energy_model();
	if (em && em->sleep()) {
		em->set_node_sleep(0);
		em->set_node_state(EnergyModel::INROUTE);
	}

	tx_count = 0;
  rts_tx_count = 0;

	click_wifi_extra* ceh = getWifiExtra(p);

	if ( ceh != 0 ) {
	    u_int8_t queue = (ceh->flags >> 18) & 3;
	    queue_index_ = queue;
      //printf("Queue: %d\n",queue);
	    rst_cw();
      dst_mac = &((uint8_t*)&ceh[1])[4];
      ceh->retries = 0;  //reset retries
      ceh->virt_col = 0; //reset virt_col
      ceh->pad = 0;      //reset pad

      struct hdr_cmn *ch = HDR_CMN(p);

      if (ceh->flags & WIFI_EXTRA_DO_RTS_CTS) {
        macmib_.setRTSThreshold((u_int32_t)ch->size() + 1);
      } else {
        macmib_.setRTSThreshold((u_int32_t)ch->size() - 1);
      }
	}

	if ( memcmp(dst_mac,macbcast,6) != 0 ) {
	    memcpy(tx_target_mac_, dst_mac, 6);
	    memcpy(tx_source_mac_, &((uint8_t*)&ceh[1])[10], 6);
	}

	tx_control_state_ = TX_CONTROL_BUSY;

//      printf("CW (pre send): %d\n",cw_);

	callback_ = h;

	p->txinfo_.setChannel(0);

	sendDATA(p);
	sendRTS(ETHER_ADDR(dh->dh_ra));

	/*
	 * Assign the data packet a sequence number.
	 */
	dh->dh_scontrol = sta_seqno_++;

	/*
	 *  If the medium is IDLE, we must wait for a DIFS
	 *  Space before transmitting.
	 */

//      printf("CW: %d\n",cw_);
	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
					 mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());
		}
	}

}

void
Mac802_11::recv(Packet *p, Handler *h)
{
	struct hdr_cmn *hdr = HDR_CMN(p);
	/*
	 * Sanity Check
	 */
	assert(initialized());

	/*
	 *  Handle outgoing packets.
	 */
	if(hdr->direction() == hdr_cmn::DOWN) {
                send(p, h);
                return;
        }
	/*
	 *  Handle incoming packets.
	 *
	 *  We just received the 1st bit of a packet on the network
	 *  interface.
	 *
	 */

	click_wifi_extra* rceh = getWifiExtra(p);

	/*
	 *  If the interface is currently in transmit mode, then
	 *  it probably won't even see this packet.  However, the
	 *  "air" around me is BUSY so I need to let the packet
	 *  proceed.  Just set the error flag in the common header
	 *  to that the packet gets thrown away.
	 */
	if(tx_active_ && hdr->error() == 0) {
		hdr->error() = 1;
		if(rceh != 0 && hdr->error()){
			rceh->flags |= WIFI_EXTRA_RX_ERR;
		}
	}

	if(rx_state_ == MAC_IDLE) {
		setRxState(MAC_RECV, is_jammer_msg(p));
		pktRx_ = p;
		/*
		 * Schedule the reception of this packet, in
		 * txtime seconds.
		 */
		if (mhProbe_.busy() && OnMinChannelTime) {
			Recv_Busy_ = 1;  // Receiver busy indication for Probe Timer
		}


		mhRecv_.start(txtime(p));
	} else {
		/*
		 *  If the power of the incoming packet is smaller than the
		 *  power of the packet currently being received by at least
                 *  the capture threshold, then we ignore the new packet.
		 */
		if(pktRx_->txinfo_.RxPr / p->txinfo_.RxPr >= p->txinfo_.CPThresh) {
			capture(p);
		} else {
			collision(p);
		}
	}
}

void
Mac802_11::recv_timer()
{
	u_int32_t src;
	hdr_cmn *ch = HDR_CMN(pktRx_);
	hdr_mac802_11 *mh = HDR_MAC802_11(pktRx_);
	u_int32_t dst = ETHER_ADDR(mh->dh_ra);
	u_int32_t ap_dst = ETHER_ADDR(mh->dh_3a);
	u_int8_t  type = mh->dh_fc.fc_type;
	u_int8_t  subtype = mh->dh_fc.fc_subtype;

	assert(pktRx_);
	assert(rx_state_ == MAC_RECV || rx_state_ == MAC_COLL);


        /*
         *  If the interface is in TRANSMIT mode when this packet
         *  "arrives", then I would never have seen it and should
         *  do a silent discard without adjusting the NAV.
         */
        if(tx_active_) {
                Packet::free(pktRx_);
                goto done;
        }

	/*
	 * Handle collisions.
	 */
	if(rx_state_ == MAC_COLL) {
		discard(pktRx_, DROP_MAC_COLLISION);
		set_nav(usec(phymib_.getEIFS()));
		goto done;
	}

	/*
	 * Check to see if this packet was received with enough
	 * bit errors that the current level of FEC still could not
	 * fix all of the problems - ie; after FEC, the checksum still
	 * failed.
	 */
	if( ch->error() ) {
		Packet::free(pktRx_);
		set_nav(usec(phymib_.getEIFS()));
		goto done;
	}

	/*
	 * IEEE 802.11 specs, section 9.2.5.6
	 *	- update the NAV (Network Allocation Vector)
	 */

  //fprintf(stderr, "dst: %d index: %d dur: %d err: %d rate: %f\n", dst, (u_int32_t) index_, mh->dh_duration, ch->error(), dataRate_);

	if(dst != (u_int32_t)index_) {
		set_nav(mh->dh_duration);
	}

        /* tap out - */
        if (tap_ && type == MAC_Type_Data &&
            MAC_Subtype_Data == subtype ) {
		if (!tap_filterown_ ||
		    ((dst != (u_int32_t)index_) && (dst != MAC_BROADCAST))) {
			tap_->tap(pktRx_);
		}
	}
	/*
	 * Adaptive Fidelity Algorithm Support - neighborhood infomation
	 * collection
	 *
	 * Hacking: Before filter the packet, log the neighbor node
	 * I can hear the packet, the src is my neighbor
	 */
	if (netif_->node()->energy_model() &&
	    netif_->node()->energy_model()->adaptivefidelity()) {
		src = ETHER_ADDR(mh->dh_ta);
		netif_->node()->energy_model()->add_neighbor(src);
	}
	/*
	 * Address Filtering
	 */
//	fprintf(stderr,"Promisc: %d\n", macmib_.getPromisc());
	if(dst != (u_int32_t)index_ && dst != MAC_BROADCAST && macmib_.getPromisc() == 0) {
//	        fprintf(stderr,"Discard Packet\n");
		/*
		 *  We don't want to log this event, so we just free
		 *  the packet instead of calling the drop routine.
		 */
		discard(pktRx_, "---");
		goto done;
	}


	if ( dst == MAC_BROADCAST && mh->dh_fc.fc_to_ds == 1 && mh->dh_fc.fc_from_ds == 1) {
		if (addr() != bss_id_) {
 			discard(pktRx_, "---");
 			goto done;
		}
		if (addr() == bss_id_ && find_client(ap_dst) == 0) {
			discard(pktRx_, "---");
 			goto done;
		}
 	}


	if ( addr() == bss_id_ && subtype == MAC_Subtype_80211_Beacon) {
		discard(pktRx_, "---");
 			goto done;
	}


	if ( addr() == bss_id_ && subtype == MAC_Subtype_80211_Beacon) {
		discard(pktRx_, "---");
 			goto done;
	}



	if ( addr() != bss_id_ && subtype == MAC_Subtype_ProbeReq) {
		discard(pktRx_, "---");
 			goto done;
	}

	switch(type) {

	case MAC_Type_Management:
		switch(subtype) {
		case MAC_Subtype_80211_Beacon:
			recvBEACON(pktRx_);
			break;
		case MAC_Subtype_Auth:
			recvAUTHENTICATE(pktRx_);
			break;
		case MAC_Subtype_AssocReq:
			recvASSOCREQ(pktRx_);
			break;
		case MAC_Subtype_AssocRep:
			recvASSOCREP(pktRx_);
			break;
		case MAC_Subtype_ProbeReq:
			recvPROBEREQ(pktRx_);
			break;
		case MAC_Subtype_ProbeRep:
			recvPROBEREP(pktRx_);
			break;
		default:
			fprintf(stderr,"recvTimer1:Invalid MAC Management Subtype %x\n",
				subtype);
			exit(1);
		}
		break;
	case MAC_Type_Control:
		switch(subtype) {
		case MAC_Subtype_RTS:
			recvRTS(pktRx_);
			break;
		case MAC_Subtype_CTS:
			recvCTS(pktRx_);
			break;
		case MAC_Subtype_ACK:
			recvACK(pktRx_);
			break;
		default:
			fprintf(stderr,"recvTimer2:Invalid MAC Control Subtype %x\n",
				subtype);
			exit(1);
		}
		break;
	case MAC_Type_Data:
		switch(subtype) {
		case MAC_Subtype_Data:
			recvDATA(pktRx_);
			break;
		default:
			fprintf(stderr, "recv_timer3:Invalid MAC Data Subtype %x\n",
				subtype);
			exit(1);
		}
		break;
	default:
		fprintf(stderr, "recv_timer4:Invalid MAC Type %x\n", subtype);
		exit(1);
	}
 done:
	pktRx_ = 0;
	rx_resume();

}


void
Mac802_11::recvRTS(Packet *p)
{
	struct rts_frame *rf = (struct rts_frame*)p->access(hdr_mac::offset_);

	if(tx_state_ != MAC_IDLE) {
		discard(p, DROP_MAC_BUSY);
		return;
	}

  hdr_mac802_11 *mh = HDR_MAC802_11(p);
  u_int32_t dst = ETHER_ADDR(mh->dh_ra);

  if (macmib_.getControlFrames() == 1) {

    //fprintf(stderr,"Pommes Mode\n");
    Packet* p_rts = Packet::alloc((int)(sizeof(click_wifi_extra) + sizeof(struct rts_frame_no_fcs)));
    unsigned char *rts_data = p_rts->accessdata();
    click_wifi_extra* ceh_rts = (click_wifi_extra*)(p_rts->accessdata());
    memset((unsigned char*)ceh_rts,0,sizeof(click_wifi_extra));
    if (ceh_rts != 0){
      ceh_rts->magic = WIFI_EXTRA_MAGIC;
      ceh_rts->flags = 0;
      ceh_rts->rate = 2;
      ceh_rts->silence = -95;
      ceh_rts->rssi = p->txinfo_.RxPrMadwifi;
      ceh_rts->len = 10;

      struct rts_frame_no_fcs *af_recv_rts = (struct rts_frame_no_fcs*)p->access(hdr_mac::offset_);
      memcpy(&(rts_data[sizeof(click_wifi_extra)]),af_recv_rts,sizeof(struct rts_frame_no_fcs));
      af_recv_rts = (struct rts_frame_no_fcs*)&(rts_data[sizeof(click_wifi_extra)]);
      af_recv_rts->type = 0xb4;
      af_recv_rts->ctrl = 0;
      af_recv_rts->rf_ra[5] = af_recv_rts->rf_ra[3];
      af_recv_rts->rf_ra[4] = af_recv_rts->rf_ra[2];
      af_recv_rts->rf_ra[3] = af_recv_rts->rf_ra[1];
      af_recv_rts->rf_ra[2] = af_recv_rts->rf_ra[0];
      af_recv_rts->rf_ra[1] = 0;
      af_recv_rts->rf_ra[0] = 0;

      af_recv_rts->rf_ra[5]++;
      if ( af_recv_rts->rf_ra[5] == 0 ) af_recv_rts->rf_ra[4]++;

      af_recv_rts->rf_ta[5] = af_recv_rts->rf_ta[3];
      af_recv_rts->rf_ta[4] = af_recv_rts->rf_ta[2];
      af_recv_rts->rf_ta[3] = af_recv_rts->rf_ta[1];
      af_recv_rts->rf_ta[2] = af_recv_rts->rf_ta[0];
      af_recv_rts->rf_ta[1] = 0;
      af_recv_rts->rf_ta[0] = 0;

      af_recv_rts->rf_ta[5]++;
      if ( af_recv_rts->rf_ta[5] == 0 ) af_recv_rts->rf_ta[4]++;

      struct hdr_cmn* ch_rts = HDR_CMN(p_rts);
      ch_rts->direction() = hdr_cmn::UP;
      ch_rts->txfeedback() = hdr_cmn::NO;
      ch_rts->ptype() = PT_RAW;
      hdr_raw* rhdr = hdr_raw::access(p_rts);	
      rhdr->ns_type = PT_RAW;
      rhdr->subtype = hdr_raw::MADWIFI;

      uptarget_->recv(p_rts, (Handler*) 0); // promisc

      if (dst != (u_int32_t)index_) {
        Packet::free(p);
        return;
      }
    }
  }

  if (dst != (u_int32_t)index_) {
    Packet::free(p);
    return;
  }

	/*
	 *  If I'm responding to someone else, discard this RTS.
	 */
	if(pktCTRL_) {
		discard(p, DROP_MAC_BUSY);
		return;
	}

  rxtx_stats_.rx_no_rts_++;
	sendCTS(ETHER_ADDR(rf->rf_ta), rf->rf_duration);

	/*
	 *  Stop deferring - will be reset in tx_resume().
	 */
	if(mhDefer_.busy()) mhDefer_.stop();

	tx_resume();

	mac_log(p);
}

/*
 * txtime()	- pluck the precomputed tx time from the packet header
 */
double
Mac802_11::txtime(Packet *p)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	double t = ch->txtime();
	if (t < 0.0) {
		drop(p, "XXX");
 		exit(1);
	}
	return t;
}


/*
 * txtime()	- calculate tx time for packet of size "psz" bytes
 *		  at rate "drt" bps
 */
double
Mac802_11::txtime(double psz, double drt)
{
	double dsz = psz - phymib_.getPLCPhdrLen();
        int plcp_hdr = phymib_.getPLCPhdrLen() << 3;
	int datalen = (int)dsz << 3;
	double t = (((double)plcp_hdr)/phymib_.getPLCPDataRate())
                                       + (((double)datalen)/drt);
	return(t);
}



void
Mac802_11::recvCTS(Packet *p)
{
  hdr_mac802_11 *mh = HDR_MAC802_11(p);
  u_int32_t dst = ETHER_ADDR(mh->dh_ra);

  /* Create CTS frame for upper layer */
  if ( macmib_.getControlFrames() == 1 ) {
    //fprintf(stderr,"Pommes Mode\n");
    Packet* p_cts = Packet::alloc((int)(sizeof(click_wifi_extra) + sizeof(struct cts_frame_no_fcs)));
    unsigned char *cts_data = p_cts->accessdata();
    click_wifi_extra* ceh_cts = (click_wifi_extra*)(p_cts->accessdata());
    memset((unsigned char*)ceh_cts,0,sizeof(click_wifi_extra));
    if (ceh_cts != 0){
      ceh_cts->magic = WIFI_EXTRA_MAGIC;
      ceh_cts->flags = 0;
      ceh_cts->rate = 2;
      ceh_cts->silence = -95;
      ceh_cts->rssi = p->txinfo_.RxPrMadwifi;
      ceh_cts->len = 10;

      struct cts_frame_no_fcs *af_recv_cts = (struct cts_frame_no_fcs*)p->access(hdr_mac::offset_);
      memcpy(&(cts_data[sizeof(click_wifi_extra)]),af_recv_cts,sizeof(struct cts_frame_no_fcs));
      af_recv_cts = (struct cts_frame_no_fcs*)&(cts_data[sizeof(click_wifi_extra)]);
      af_recv_cts->type = 0xc4;
      af_recv_cts->ctrl = 0;
      af_recv_cts->cf_ra[5] = af_recv_cts->cf_ra[3];
      af_recv_cts->cf_ra[4] = af_recv_cts->cf_ra[2];
      af_recv_cts->cf_ra[3] = af_recv_cts->cf_ra[1];
      af_recv_cts->cf_ra[2] = af_recv_cts->cf_ra[0];
      af_recv_cts->cf_ra[1] = 0;
      af_recv_cts->cf_ra[0] = 0;

      af_recv_cts->cf_ra[5]++;
      if ( af_recv_cts->cf_ra[5] == 0 ) af_recv_cts->cf_ra[4]++;

      struct hdr_cmn* ch_cts = HDR_CMN(p_cts);
      ch_cts->direction() = hdr_cmn::UP;
      ch_cts->txfeedback() = hdr_cmn::NO;
      ch_cts->ptype() = PT_RAW;
      hdr_raw* rhdr = hdr_raw::access(p_cts);	
      rhdr->ns_type = PT_RAW;
      rhdr->subtype = hdr_raw::MADWIFI;

      uptarget_->recv(p_cts, (Handler*) 0); // send feedback WIFI_EXTRA_TX

      if (dst != (u_int32_t)index_) {
        Packet::free(p);
        return;
      }
    }
  }

  if (dst != (u_int32_t)index_) {
    Packet::free(p);
    return;
  }

  //fprintf(stderr, "mode: RTS: %d\n", (int) tx_state_ != MAC_RTS ? 0 : 1);

	if(tx_state_ != MAC_RTS) {
		discard(p, DROP_MAC_INVALID_STATE);
		return;
	}

	rxtx_stats_.rx_no_cts_++;

	assert(pktRTS_);
	Packet::free(pktRTS_); pktRTS_ = 0;

	assert(pktTx_);
	mhSend_.stop();

	/*
	 * The successful reception of this CTS packet implies
	 * that our RTS was successful.
	 * According to the IEEE spec 9.2.5.3, you must
	 * reset the ssrc_, but not the congestion window.
	 */
	ssrc_ = 0;
	tx_resume();

	mac_log(p);
}

void
Mac802_11::recvDATA(Packet *p)
{
	struct hdr_mac802_11 *dh = HDR_MAC802_11(p);
	u_int32_t dst, src, size;
	struct hdr_cmn *ch = HDR_CMN(p);

	dst = ETHER_ADDR(dh->dh_ra);
	src = ETHER_ADDR(dh->dh_ta);
	size = ch->size();
	/*
	 * Adjust the MAC packet size - ie; strip
	 * off the mac header
	 */
	click_wifi_extra* rceh = getWifiExtra(p);
#ifdef BRN_DEBUG
	printf("hdr11: %d %d %lu\n", phymib_.getPLCPhdrLen(), offsetof(struct hdr_mac802_11, dh_body[0]), ETHER_FCS_LEN);
	printf("Size: %d -> Size_madwifi: %ld\n",phymib_.getHdrLen11(), sizeof(click_wifi_extra));
#endif
	//ch->size() -= phymib_.getHdrLen11();
	ch->num_forwards() += 1;

	/*
	 *  If we sent a CTS, clean up...
	 */

	if((dst != MAC_BROADCAST) && (dst == (u_int32_t)index_) && (!qos_no_ack(p))) {
		//if(size >= macmib_.getRTSThreshold()) {
		if ( 	(!rceh && size >= macmib_.getRTSThreshold()) ||
			 	( rceh && pktCTRL_)){
			if (tx_state_ == MAC_CTS) {
				assert(pktCTRL_);
				Packet::free(pktCTRL_); pktCTRL_ = 0;
				mhSend_.stop();
				/*
				 * Our CTS got through.
				 */
			} else {
				discard(p, DROP_MAC_BUSY);
				return;
			}

			sendACK(src);
			tx_resume();
		} else {
			/*
			 *  We did not send a CTS and there's no
			 *  room to buffer an ACK.
			 */
			if(pktCTRL_) {
				discard(p, DROP_MAC_BUSY);
				return;
			}

			sendACK(src);
			if(mhSend_.busy() == 0)
				tx_resume();
		}
	}

	/* ============================================================
	   Make/update an entry in our sequence number cache.
	   ============================================================ */

	/* Changed by Debojyoti Dutta. This upper loop of if{}else was
	   suggested by Joerg Diederich <dieder@ibr.cs.tu-bs.de>.
	   Changed on 19th Oct'2000 */

	if(dst != MAC_BROADCAST && dst == (u_int32_t)index_) {
                if (src < (u_int32_t) cache_node_count_) {
                        Host *h = &cache_[src];

                        if(h->seqno && h->seqno == dh->dh_scontrol && macmib_.getFilterDub() == 1) {
                                discard(p, DROP_MAC_DUPLICATE);
                                return;
                        }
                        h->seqno = dh->dh_scontrol;
                } else {
			static int count = 0;
			if (++count <= 10) {
				printf ("MAC_802_11: accessing MAC cache_ array out of range (src %u, dst %u, size %d)!\n", src, dst, cache_node_count_);
				if (count == 10)
					printf ("[suppressing additional MAC cache_ warnings]\n");
			};
		};
	}

	/*
	 *  Pass the packet up to the link-layer.
	 *  XXX - we could schedule an event to account
	 *  for this processing delay.
	 */

	/* in BSS mode, if a station receives a packet via
	 * the AP, and higher layers are interested in looking
	 * at the src address, we might need to put it at
	 * the right place - lest the higher layers end up
	 * believing the AP address to be the src addr! a quick
	 * grep didn't turn up any higher layers interested in
	 * the src addr though!
	 * anyway, here if I'm the AP and the destination
	 * address (in dh_3a) isn't me, then we have to fwd
	 * the packet; we pick the real destination and set
	 * set it up for the LL; we save the real src into
	 * the dh_3a field for the 'interested in the info'
	 * receiver; we finally push the packet towards the
	 * LL to be added back to my queue - accomplish this
	 * by reversing the direction!*/


  if (dst == MAC_BROADCAST) {
    rxtx_stats_.rx_no_data_++;
    rxtx_stats_.rx_no_bcast_++;
  } else if (dst == (u_int32_t)index_) {
    rxtx_stats_.rx_no_data_++;
    rxtx_stats_.rx_no_unic_++;
  }

	if ((bss_id() == addr()) && ((u_int32_t)ETHER_ADDR(dh->dh_ra)!= MAC_BROADCAST) && ((u_int32_t)ETHER_ADDR(dh->dh_3a) != ((u_int32_t)addr())) && dh->dh_fc.fc_from_ds == 0) {
		struct hdr_cmn *ch = HDR_CMN(p);
		ch->txfeedback() = hdr_cmn::NO;


		u_int32_t dst = ETHER_ADDR(dh->dh_3a);
		u_int32_t src = ETHER_ADDR(dh->dh_ta);
		/* if it is a broadcast pkt then send a copy up
		 * my stack also
		 */

		if (dst == MAC_BROADCAST) {
      if ( ! is_jammer_msg(p) ) uptarget_->recv(p->copy(), (Handler*) 0);
		}

		ch->next_hop() = dst;

		if (find_client(dst) == 1 || dst == MAC_BROADCAST) {
			STORE4BYTE(&src, (dh->dh_3a));
		} else {
			STORE4BYTE(&src, (dh->dh_4a));
		}

		ch->addr_type() = NS_AF_ILINK;
		ch->direction() = hdr_cmn::DOWN;
		ch->txfeedback() = hdr_cmn::NO;

	}

 	if ((bss_id() == addr()) && dh->dh_fc.fc_to_ds == 1 && dh->dh_fc.fc_from_ds == 1) {
 		u_int32_t dst = ETHER_ADDR(dh->dh_3a);
 		u_int32_t src = ETHER_ADDR(dh->dh_4a);
		/*if (find_client(src)) {
			update_client_table(src,0,0);   // If the source is from another BSS and it is found in this AP's table, delete the node from the table
		}*/

 		ch->next_hop() = dst;
 		STORE4BYTE(&src, (dh->dh_3a));

 		ch->addr_type() = NS_AF_ILINK;
 		ch->direction() = hdr_cmn::DOWN;
		ch->txfeedback() = hdr_cmn::NO;

 	}

  if ( ! is_jammer_msg(p) ) uptarget_->recv(p, (Handler*) 0);
  else discard(p, DROP_MAC_BUSY); //TODO: is that the right way to discard (kill) the jammer msg

}


void
Mac802_11::recvACK(Packet *p)
{
  hdr_mac802_11 *mh = HDR_MAC802_11(p);
  u_int32_t dst = ETHER_ADDR(mh->dh_ra);

  if (tx_state_ == MAC_MGMT) {
		mhSend_.stop();
		if (addr() == bss_id_) {
			if (pktASSOCREP_ && tx_mgmt_ == 4) {
				Packet::free(pktASSOCREP_);
				pktASSOCREP_ = 0;
				update_client_table(associating_node_,1,1);
			}
			if (pktPROBEREP_ && tx_mgmt_ == 1) {
				Packet::free(pktPROBEREP_);
				pktPROBEREP_ = 0;
			}
			if (pktAUTHENTICATE_ && tx_mgmt_ == 3) {
				Packet::free(pktAUTHENTICATE_);
				pktAUTHENTICATE_ = 0;
				update_client_table(authenticating_node_,1,0);
			}
			shift_priority_queue();
			assert(mhBackoff_.busy() == 0);
			mhBackoff_.start(cw_, is_idle());
		}
		tx_resume();
		mac_log(p);
		return;
	}

  if (dst != (u_int32_t)index_) {

    if ( (macmib_.getControlFrames() == 1) && ( macmib_.getPromisc() == 1 ) ) {
      //fprintf(stderr,"Pommes Mode, recv ack\n");
      Packet* p_ack = Packet::alloc((int)(sizeof(click_wifi_extra) + sizeof(struct ack_frame_no_fcs)));
      unsigned char *ack_data = p_ack->accessdata();
      click_wifi_extra* ceh_ack = (click_wifi_extra*)(p_ack->accessdata());
      memset((unsigned char*)ceh_ack,0,sizeof(click_wifi_extra));
      if (ceh_ack != 0){
        ceh_ack->magic = WIFI_EXTRA_MAGIC;
        ceh_ack->flags = 0;
        ceh_ack->rate = 2;
        ceh_ack->silence = -95;
        ceh_ack->rssi = p->txinfo_.RxPrMadwifi;
        ceh_ack->len = 10;

        struct ack_frame_no_fcs *af_recv_ack = (struct ack_frame_no_fcs*)p->access(hdr_mac::offset_);
        memcpy(&(ack_data[sizeof(click_wifi_extra)]),af_recv_ack,sizeof(struct ack_frame_no_fcs));
        af_recv_ack = (struct ack_frame_no_fcs*)&(ack_data[sizeof(click_wifi_extra)]);
        af_recv_ack->type = 0xd4;
        af_recv_ack->ctrl = 1;
        af_recv_ack->af_ra[5] = af_recv_ack->af_ra[3];
        af_recv_ack->af_ra[4] = af_recv_ack->af_ra[2];
        af_recv_ack->af_ra[3] = af_recv_ack->af_ra[1];
        af_recv_ack->af_ra[2] = af_recv_ack->af_ra[0];
        af_recv_ack->af_ra[1] = 0;
        af_recv_ack->af_ra[0] = 0;

        af_recv_ack->af_ra[5]++;
        if ( af_recv_ack->af_ra[5] == 0 ) af_recv_ack->af_ra[4]++;

        struct hdr_cmn* ch_ack = HDR_CMN(p_ack);
        ch_ack->direction() = hdr_cmn::UP;
        ch_ack->txfeedback() = hdr_cmn::NO;
        ch_ack->ptype() = PT_RAW;
        hdr_raw* rhdr = hdr_raw::access(p_ack);	
        rhdr->ns_type = PT_RAW;
        rhdr->subtype = hdr_raw::MADWIFI;
        struct hdr_cmn* ch_p = HDR_CMN(p);
        ch_p->size() = 38; // TODO: sizeof(struct ack_frame_no_fcs); ?
        ch_ack->size() = ch_p->size();
        ch_ack->txtime() = ch_p->txtime();

        uptarget_->recv(p_ack, (Handler*) 0); // send feedback WIFI_EXTRA_TX

      }
    }
    Packet::free(p);
    return;
  } else if (tx_state_ != MAC_SEND) {
		//fprintf(stderr,"No TXPacket. Discard Ack\n");
		discard(p, DROP_MAC_INVALID_STATE);
		return;
	}

  assert(pktTx_);

	// nletor
	Packet* p2 = 0;	//prepare feedback WIFI_EXTRA_TX
	click_wifi_extra* ceh = getWifiExtra(pktTx_);
	if (ceh != 0){
		p2 = pktTx_->copy();
		click_wifi_extra* ceh2 = getWifiExtra(p2);
		ceh2->flags |= WIFI_EXTRA_TX;
		ceh2->flags |= Click::WIFI_EXTRA_EXT_RETRY_INFO;
		ceh2->silence = -95;
		ceh2->rssi = p->txinfo_.RxPrMadwifi;
		ceh2->virt_col = (rts_tx_count << 4) + tx_count;
		//fprintf(stderr, "Sum long: %d short: %d retries: %d (%d/%d/%d)\n",slrc_, ssrc_, ceh2->retries, rts_tx_count, tx_count, ceh2->virt_col);

		struct hdr_cmn* ch2 = HDR_CMN(p2);
		ch2->direction() = hdr_cmn::UP;
		ch2->txfeedback() = hdr_cmn::YES;
	}

  //fprintf(stderr,"Create Ack Size: %d\n",sizeof(struct ack_frame_no_fcs));

  rxtx_stats_.rx_no_ack_++;

  Packet* p_ack = 0;  //prepare ack

  if (macmib_.getControlFrames() == 1) {
	  if (ceh != 0){
      //fprintf(stderr,"Call copy\n");
      //fprintf(stderr,"recv ack\n");
		  p_ack = pktTx_->copy();
		  click_wifi_extra* ceh_ack = getWifiExtra(p_ack);
      ceh_ack->magic = WIFI_EXTRA_MAGIC;
      ceh_ack->flags = 0;
      ceh_ack->rate = 2;
      ceh_ack->silence = -95;
      ceh_ack->rssi = p->txinfo_.RxPrMadwifi;
      ceh_ack->len = 10;

      struct ack_frame_no_fcs *af_recv_ack = (struct ack_frame_no_fcs*)p->access(hdr_mac::offset_);
      PacketData *ack_data = new PacketData(sizeof(click_wifi_extra) + sizeof(struct ack_frame_no_fcs));
      memcpy(&(ack_data->data()[sizeof(click_wifi_extra)]),af_recv_ack,sizeof(struct ack_frame_no_fcs));
      af_recv_ack = (struct ack_frame_no_fcs*)&(ack_data->data()[sizeof(click_wifi_extra)]);
      af_recv_ack->type = 0xd4;
      af_recv_ack->ctrl = 0;
      af_recv_ack->af_ra[5] = af_recv_ack->af_ra[3];
      af_recv_ack->af_ra[4] = af_recv_ack->af_ra[2];
      af_recv_ack->af_ra[3] = af_recv_ack->af_ra[1];
      af_recv_ack->af_ra[2] = af_recv_ack->af_ra[0];
      af_recv_ack->af_ra[1] = 0;
      af_recv_ack->af_ra[0] = 0;

      af_recv_ack->af_ra[5]++;
      if ( af_recv_ack->af_ra[5] == 0 ) af_recv_ack->af_ra[4]++;

      memcpy(ack_data->data(),ceh_ack,sizeof(click_wifi_extra));
      p_ack->setdata(ack_data);

      struct hdr_cmn* ch_ack = HDR_CMN(p_ack);
      ch_ack->direction() = hdr_cmn::UP;
      ch_ack->txfeedback() = hdr_cmn::NO;
      //ch_ack->size() = 38; // TODO: sizeof(struct ack_frame_no_fcs); ?

      struct hdr_cmn* ch_p = HDR_CMN(p);
      ch_ack->size() = ch_p->size();
      ch_ack->txtime() = ch_p->txtime();
    }
  }
	//!nletor
	mhSend_.stop();

	/*
	 * The successful reception of this ACK packet implies
	 * that our DATA transmission was successful.  Hence,
	 * we can reset the Short/Long Retry Count and the CW.
	 *
	 * need to check the size of the packet we sent that's being
	 * ACK'd, not the size of the ACK packet.
	 */
	if((u_int32_t) HDR_CMN(pktTx_)->size() <= macmib_.getRTSThreshold())
		ssrc_ = 0;
	else
		slrc_ = 0;
	rst_cw();
	Packet::free(pktTx_);
	pktTx_ = 0;

	/*
	 * Backoff before sending again.
// 	 */

	assert(mhBackoff_.busy() == 0);
	mhBackoff_.start(cw_, is_idle());

	tx_resume();

	if (p_ack == 0 ) mac_log(p);

  tx_control_state_ = TX_CONTROL_IDLE;
  //uptarget_->recv(p->copy(), (Handler*) 0);
	// nletor
  if (p_ack != 0 ){
    //fprintf(stderr, "send up ack\n");
    uptarget_->recv(p_ack, (Handler*) 0); // send ack
  }
  if (p2 != 0 ){
		uptarget_->recv(p2, (Handler*) 0);	// send feedback WIFI_EXTRA_TX
	}

	//!nletor
}


/* AP's association table funtions
*/
void Mac802_11::update_client_table(int num, int auth_status, int assoc_status) {

	std::list<client_table>::iterator it;
	for (it=client_list1.begin(); it != client_list1.end(); it++) {
		if ((*it).client_id == num) {
			(*it).auth_status = auth_status;
			(*it).assoc_status = assoc_status;
			break;
		}
;
	}

	if (it == client_list1.end()) {
		client_list = (struct client_table*)malloc(sizeof(struct client_table));
		client_list->client_id=num;
		client_list->auth_status=auth_status;
		client_list->assoc_status=assoc_status;

		client_list1.push_front(*client_list);

		free(client_list);

	}

//	printf("Client List for AP %d at %f\n",index_,NOW);



//	for (it=client_list1.begin(); it != client_list1.end(); it++) {
//		printf("Client %d: Authenticated = %d Associated = %d at %f\n", (*it).client_id,(*it).auth_status,(*it).assoc_status,NOW);
//		temp=temp->next;
//	}
//	printf("\n");

}


int Mac802_11::find_client(int num) {
	std::list<client_table>::iterator it;

	for (it=client_list1.begin(); it != client_list1.end(); it++) {
		if ((*it).client_id == num) {
			return 1;
		}

	}

	return 0;
}


/* Beacon send and Receive functions
*/
void
Mac802_11::sendBEACON(int src)
{
	Packet *p = Packet::alloc();

	hdr_cmn* ch = HDR_CMN(p);
	struct beacon_frame *bf = (struct beacon_frame*)p->access(hdr_mac::offset_);
	double rTime;
	pktBEACON_ = 0;

	ch->uid() = 0;


	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getBEACONlen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(bf, MAC_HDR_LEN);

/* Note: I had to give a different name for MAC_Subtype_80211_Beacon as MAC_Subtype_80211_Beacon as MAC_Subtype_80211_Beacon is already defined for 802.15.4 with a different value!! (See cmu-trace.cc).
*/

	bf->bf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	bf->bf_fc.fc_type	= MAC_Type_Management;
 	bf->bf_fc.fc_subtype	= MAC_Subtype_80211_Beacon;
 	bf->bf_fc.fc_to_ds	= 0;
 	bf->bf_fc.fc_from_ds	= 0;
 	bf->bf_fc.fc_more_frag	= 0;
 	bf->bf_fc.fc_retry	= 0;
 	bf->bf_fc.fc_pwr_mgt	= 0;
 	bf->bf_fc.fc_more_data	= 0;
 	bf->bf_fc.fc_wep	= 0;
 	bf->bf_fc.fc_order	= 0;

	int  dst = MAC_BROADCAST;
	STORE4BYTE(&dst, (bf->bf_ra));
	STORE4BYTE(&src, (bf->bf_ta));
	STORE4BYTE(&ap_addr, (bf->bf_3a));

	bf->bf_timestamp = Scheduler::instance().clock();
	bf->bf_bcninterval = phymib_.getBeaconInterval();

	/* store beacon tx time */

 	ch->txtime() = txtime(ch->size(), basicRate_);

	/* calculate beacon duration??? */
 	bf->bf_duration = 0;

  p->txinfo_.setChannel(0);

  pktBEACON_ = p;

	BeaconTxtime_ = txtime(phymib_.getBEACONlen(), basicRate_);

	add_priority_queue(2);

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {

				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */

			mhBackoff_.start(cw_, is_idle());

		}
	}

}

int
Mac802_11::check_pktBEACON()
{
	struct beacon_frame *bf = (struct beacon_frame*)pktBEACON_->access(hdr_mac::offset_);
	bf->bf_timestamp = Scheduler::instance().clock();

	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktBEACON_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktBEACON_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_80211_Beacon:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_BCN);
		timeout = txtime(phymib_.getBEACONlen(), basicRate_);
		break;
	default:
		fprintf(stderr, "check_pktBEACON:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktBEACON_, timeout);

	return 0;
}


void
Mac802_11::recvBEACON(Packet *p)
{
	struct beacon_frame *bf = (struct beacon_frame*)p->access(hdr_mac::offset_);

	if(tx_state_ != MAC_IDLE) {
		discard(p, DROP_MAC_BUSY);
		return;
	}
	u_int32_t /*bss_id,*/ src;

	//bss_id = ETHER_ADDR(bf->bf_3a);
 	src = ETHER_ADDR(bf->bf_ta);
	infra_mode_ = 1;

	Pr = p->txinfo_.RxPr;
	if ( addr() != ap_addr && ScanType_ == PASSIVE) {
		if (authenticated == 0 && associated == 0) {
			if (find_ap(src,Pr) != 1) {
				update_ap_table(src,Pr);
			}
 		}
	}

	mac_log(p);
}

void
Mac802_11::passive_scan()
{
	if ( addr() != ap_addr && ScanType_ == PASSIVE) {
		if (authenticated == 0 && associated == 0) {
			ap_temp = strongest_ap();
			if (!pktAUTHENTICATE_)
				sendAUTHENTICATE(ap_temp);
		}

	}
}
void
Mac802_11::active_scan()
{

	if ( addr() != ap_addr && ScanType_ == ACTIVE) {
		if (authenticated == 0 && associated == 0) {
			ap_temp = strongest_ap();
			sendAUTHENTICATE(ap_temp);
		}

	}
}


// Association functions

void
Mac802_11::sendASSOCREQ(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct assocreq_frame *acrqf =(struct assocreq_frame*)p->access(hdr_mac::offset_);

	double rTime;
	pktASSOCREQ_ = 0;

	ch->uid() = 0;


	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getASSOCREQlen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(acrqf, MAC_HDR_LEN);

	acrqf->acrqf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	acrqf->acrqf_fc.fc_type	= MAC_Type_Management;
 	acrqf->acrqf_fc.fc_subtype	= MAC_Subtype_AssocReq;
 	acrqf->acrqf_fc.fc_to_ds	= 0;
 	acrqf->acrqf_fc.fc_from_ds	= 0;
 	acrqf->acrqf_fc.fc_more_frag= 0;
 	acrqf->acrqf_fc.fc_retry	= 0;
 	acrqf->acrqf_fc.fc_pwr_mgt	= 0;
 	acrqf->acrqf_fc.fc_more_data= 0;
 	acrqf->acrqf_fc.fc_wep	= 0;
 	acrqf->acrqf_fc.fc_order	= 0;

	STORE4BYTE(&dst, (acrqf->acrqf_ra));
	STORE4BYTE(&index_, (acrqf->acrqf_ta));
	STORE4BYTE(&dst, (acrqf->acrqf_3a));


	ch->txtime() = txtime(ch->size(), basicRate_);
 	acrqf->acrqf_duration = 0;

	p->txinfo_.setChannel(0);

	pktASSOCREQ_ = p;

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());
		}
	}
}

int
Mac802_11::check_pktASSOCREQ()
{
	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktASSOCREQ_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktASSOCREQ_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_AssocReq:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_MGMT);
		timeout = txtime(phymib_.getASSOCREQlen(), basicRate_)
			+ DSSS_MaxPropagationDelay
			+ macmib_.getMaxChannelTime()
			+ txtime(phymib_.getASSOCREPlen(), basicRate_)
			+ DSSS_MaxPropagationDelay;
		break;
	default:
		fprintf(stderr, "check_pktASSOCREQ:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktASSOCREQ_, timeout);


	return 0;
}

void
Mac802_11::sendASSOCREP(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct assocrep_frame *acrpf =(struct assocrep_frame*)p->access(hdr_mac::offset_);
	double rTime;
	pktASSOCREP_ = 0;

	ch->uid() = 0;


	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getASSOCREPlen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(acrpf, MAC_HDR_LEN);

	acrpf->acrpf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	acrpf->acrpf_fc.fc_type	= MAC_Type_Management;
 	acrpf->acrpf_fc.fc_subtype	= MAC_Subtype_AssocRep;
 	acrpf->acrpf_fc.fc_to_ds	= 0;
 	acrpf->acrpf_fc.fc_from_ds	= 0;
 	acrpf->acrpf_fc.fc_more_frag= 0;
 	acrpf->acrpf_fc.fc_retry	= 0;
 	acrpf->acrpf_fc.fc_pwr_mgt	= 0;
 	acrpf->acrpf_fc.fc_more_data= 0;
 	acrpf->acrpf_fc.fc_wep	= 0;
 	acrpf->acrpf_fc.fc_order	= 0;

	STORE4BYTE(&dst, (acrpf->acrpf_ra));
	STORE4BYTE(&index_, (acrpf->acrpf_ta));
	STORE4BYTE(&index_, (acrpf->acrpf_3a));

	acrpf->acrpf_statuscode = 0;
	ch->txtime() = txtime(ch->size(), basicRate_);
 	acrpf->acrpf_duration = 0;

	p->txinfo_.setChannel(0);

 	pktASSOCREP_ = p;


	add_priority_queue(4);

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());

		}


	}
}

int
Mac802_11::check_pktASSOCREP()
{
 	struct hdr_mac802_11 *mh;
 	double timeout;

 	assert(mhBackoff_.busy() == 0);
 	if(pktASSOCREP_ == 0)
  		return -1;
	mh = HDR_MAC802_11(pktASSOCREP_);
  	switch(mh->dh_fc.fc_subtype) {
 	case MAC_Subtype_AssocRep:
 		if(!is_idle()) {
 			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
 		}

 		setTxState(MAC_MGMT);
		tx_mgmt_ = 4;
 		timeout = txtime(phymib_.getASSOCREPlen(), basicRate_)
			+ DSSS_MaxPropagationDelay
			+ phymib_.getSIFS()
			+ txtime(phymib_.getACKlen(), basicRate_)
			+ DSSS_MaxPropagationDelay;

 		break;
	default:
 		fprintf(stderr, "check_pktASSOCREP:Invalid MAC Control subtype\n");
 		exit(1);
	}
 	transmit(pktASSOCREP_, timeout);

 	return 0;
}

void
Mac802_11::recvASSOCREQ(Packet *p)
{
	struct assocreq_frame *acrqf = (struct assocreq_frame*)p->access(hdr_mac::offset_);

	if(tx_state_ != MAC_IDLE) {
		discard(p, DROP_MAC_BUSY);
		return;
	}
	u_int32_t /*bss_id,*/ src;

	//bss_id = ETHER_ADDR(acrqf->acrqf_3a);
 	src = ETHER_ADDR(acrqf->acrqf_ta);

	if (!pktASSOCREP_) {
		sendASSOCREP(src);
		associating_node_ = src;
	} else {
		discard(p, DROP_MAC_BUSY);
		return;
	}
	tx_resume();

	mac_log(p);
}

void
Mac802_11::recvASSOCREP(Packet *p)
{
	struct assocrep_frame *acrpf = (struct assocrep_frame*)p->access(hdr_mac::offset_);

	assert(pktASSOCREQ_);
	Packet::free(pktASSOCREQ_);
	pktASSOCREQ_ = 0;
	mhSend_.stop();

	u_int32_t src;
	u_int16_t statuscode;

 	src = ETHER_ADDR(acrpf->acrpf_ta);
	statuscode = acrpf->acrpf_statuscode;

	if (statuscode == 0) {
		associated = 1;
		deletelist();
		if (handoff) {
			handoff = 0; //handoff completed
		}
		sendACK(src);

	}
	if(mhSend_.busy() == 0)
		tx_resume();

	mac_log(p);


}

//Authentication functions

void
Mac802_11::sendAUTHENTICATE(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct auth_frame *authf =(struct auth_frame*)p->access(hdr_mac::offset_);

	double rTime;
	pktAUTHENTICATE_ = 0;

	ch->uid() = 0;


	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getAUTHENTICATElen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(authf, MAC_HDR_LEN);

	authf->authf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	authf->authf_fc.fc_type	= MAC_Type_Management;
 	authf->authf_fc.fc_subtype	= MAC_Subtype_Auth;
 	authf->authf_fc.fc_to_ds	= 0;
 	authf->authf_fc.fc_from_ds	= 0;
 	authf->authf_fc.fc_more_frag= 0;
 	authf->authf_fc.fc_retry	= 0;
 	authf->authf_fc.fc_pwr_mgt	= 0;
 	authf->authf_fc.fc_more_data= 0;
 	authf->authf_fc.fc_wep	= 0;
 	authf->authf_fc.fc_order	= 0;


	STORE4BYTE(&dst, (authf->authf_ra));
	STORE4BYTE(&index_, (authf->authf_ta));
	if (addr() != bss_id_)
		STORE4BYTE(&dst, (authf->authf_3a));
	else
		STORE4BYTE(&index_, (authf->authf_3a));
	authf->authf_algono = 0; //Open system authentication

	if (addr() != bss_id_) {
		authf->authf_seqno = 1;
	} else {
		authf->authf_seqno = 2;
		authf->authf_statuscode = 0;
	}

	ch->txtime() = txtime(ch->size(), basicRate_);
	if (addr() == bss_id_) {
 		authf->authf_duration = usec(txtime(phymib_.getACKlen(), basicRate_)
				       + phymib_.getSIFS());
	} else {
		authf->authf_duration = 0;
	}


	pktAUTHENTICATE_ = p;

	if (addr() == bss_id_) {
		add_priority_queue(3);
	}


	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());

		}
	}

}

int
Mac802_11::check_pktAUTHENTICATE()
{
	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktAUTHENTICATE_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktAUTHENTICATE_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_Auth:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_MGMT);
		if (addr() != bss_id_) {
			timeout = txtime(phymib_.getAUTHENTICATElen(), basicRate_)
				+ DSSS_MaxPropagationDelay                      // XXX
				+ macmib_.getMaxChannelTime()
				+ txtime(phymib_.getAUTHENTICATElen(), basicRate_)
				+ DSSS_MaxPropagationDelay;
		} else {
			timeout = txtime(phymib_.getAUTHENTICATElen(), basicRate_)
				+ DSSS_MaxPropagationDelay                      // XXX
				+ phymib_.getSIFS()
				+ txtime(phymib_.getACKlen(), basicRate_)
				+ DSSS_MaxPropagationDelay;
			tx_mgmt_ = 3;
		}
		break;
	default:
		fprintf(stderr, "check_pktAUTHENTICATE:Invalid MAC Control subtype\n");
		exit(1);
	}

	transmit(pktAUTHENTICATE_, timeout);

	return 0;
}

void
Mac802_11::recvAUTHENTICATE(Packet *p)
{
	struct auth_frame *authf = (struct auth_frame*)p->access(hdr_mac::offset_);

	if (addr() != bss_id_) {
			assert(pktAUTHENTICATE_);
			Packet::free(pktAUTHENTICATE_);
			pktAUTHENTICATE_ = 0;
			mhSend_.stop();
	} else {
		if ( tx_state_ != MAC_IDLE) {
		discard(p, DROP_MAC_BUSY);
		return;
		}
	}

	u_int32_t src;

 	src = ETHER_ADDR(authf->authf_ta);

	if (addr() == ap_addr) {
		if (authf->authf_seqno == 1) {
			if (!pktAUTHENTICATE_) {// AP is not currently involved in Authentication with any other STA
				sendAUTHENTICATE(src);
				authenticating_node_ = src;
			} else {
				discard(p, DROP_MAC_BUSY);
				return;
			}
		} else
			printf("Out of sequence\n");
	} else if (authf->authf_seqno == 2 && authf->authf_statuscode == 0) {
		authenticated = 1;
		if (bss_id_ != ETHER_ADDR(authf->authf_3a) && handoff == 1) {
			printf("Client %d: Handoff from AP %d to AP %d\n",index_, bss_id_,ETHER_ADDR(authf->authf_3a));
		}
		bss_id_ = ETHER_ADDR(authf->authf_3a);
		sendACK(src);
		mhProbe_.start(phymib_.getSIFS() + txtime(phymib_.getACKlen(), basicRate_) + macmib_.getMaxChannelTime());

	}

	if(mhSend_.busy() == 0)
		tx_resume();

	mac_log(p);

}

// Active Scanning Functions
void
Mac802_11::sendPROBEREQ(int dst)
{
	Packet *p = Packet::alloc();
	hdr_cmn* ch = HDR_CMN(p);
	struct probereq_frame *prrqf =(struct probereq_frame*)p->access(hdr_mac::offset_);

	double rTime;
	pktPROBEREQ_ = 0;

	ch->uid() = 0;


	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getPROBEREQlen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(prrqf, MAC_HDR_LEN);

	prrqf->prrqf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	prrqf->prrqf_fc.fc_type	= MAC_Type_Management;
 	prrqf->prrqf_fc.fc_subtype	= MAC_Subtype_ProbeReq;
 	prrqf->prrqf_fc.fc_to_ds	= 0;
 	prrqf->prrqf_fc.fc_from_ds	= 0;
 	prrqf->prrqf_fc.fc_more_frag= 0;
 	prrqf->prrqf_fc.fc_retry	= 0;
 	prrqf->prrqf_fc.fc_pwr_mgt	= 0;
 	prrqf->prrqf_fc.fc_more_data= 0;
 	prrqf->prrqf_fc.fc_wep	= 0;
 	prrqf->prrqf_fc.fc_order	= 0;


	STORE4BYTE(&dst, (prrqf->prrqf_ra));
	STORE4BYTE(&index_, (prrqf->prrqf_ta));
	STORE4BYTE(&dst, (prrqf->prrqf_3a));


	ch->txtime() = txtime(ch->size(), basicRate_);
 	prrqf->prrqf_duration = 0;


	pktPROBEREQ_ = p;

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());

		}
	}
}


int
Mac802_11::check_pktPROBEREQ()
{
	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktPROBEREQ_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktPROBEREQ_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_ProbeReq:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_MGMT);
		timeout = txtime(phymib_.getPROBEREQlen(), basicRate_)
			+ DSSS_MaxPropagationDelay;                      // XXX
		break;
	default:
		fprintf(stderr, "check_pktPROBEREQ:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktPROBEREQ_, timeout);
	mhProbe_.start(macmib_.getMinChannelTime());
	OnMinChannelTime = 1;
	return 0;
}



void
Mac802_11::sendPROBEREP(int dst)
{
	Packet *p = Packet::alloc();

	hdr_cmn* ch = HDR_CMN(p);
	struct proberep_frame *prrpf = (struct proberep_frame*)p->access(hdr_mac::offset_);

	pktPROBEREP_ = 0;

	ch->uid() = 0;

	double rTime;

	ch->ptype() = PT_MAC;
	ch->size() = phymib_.getPROBEREPlen();
	ch->iface() = -2;
	ch->error() = 0;

	bzero(prrpf, MAC_HDR_LEN);

	prrpf->prrpf_fc.fc_protocol_version = MAC_ProtocolVersion;
 	prrpf->prrpf_fc.fc_type	= MAC_Type_Management;
 	prrpf->prrpf_fc.fc_subtype	= MAC_Subtype_ProbeRep;
 	prrpf->prrpf_fc.fc_to_ds	= 0;
 	prrpf->prrpf_fc.fc_from_ds	= 0;
 	prrpf->prrpf_fc.fc_more_frag= 0;
 	prrpf->prrpf_fc.fc_retry	= 0;
 	prrpf->prrpf_fc.fc_pwr_mgt	= 0;
 	prrpf->prrpf_fc.fc_more_data= 0;
 	prrpf->prrpf_fc.fc_wep	= 0;
 	prrpf->prrpf_fc.fc_order	= 0;

	STORE4BYTE(&dst, (prrpf->prrpf_ra));
	STORE4BYTE(&index_, (prrpf->prrpf_ta));
	STORE4BYTE(&index_, (prrpf->prrpf_3a));

	prrpf->prrpf_bcninterval = phymib_.getBeaconInterval();

	ch->txtime() = txtime(ch->size(), basicRate_);
 	prrpf->prrpf_duration = 0;

	pktPROBEREP_ = p;

	add_priority_queue(1);

	if(mhBackoff_.busy() == 0) {
		if(is_idle()) {
			if (mhDefer_.busy() == 0) {
				/*
				 * If we are already deferring, there is no
				 * need to reset the Defer timer.
				 */
				if (bugFix_timer_) {
				 	mhBackoff_.start(cw_, is_idle(),
							  phymib_.getDIFS());
				}
				else {
					rTime = (Random::random() % cw_)
						* (phymib_.getSlotTime());
					mhDefer_.start(phymib_.getDIFS() +
						       rTime);
				}
			}
		} else {
			/*
			 * If the medium is NOT IDLE, then we start
			 * the backoff timer.
			 */
			mhBackoff_.start(cw_, is_idle());

		}

	}

}

int
Mac802_11::check_pktPROBEREP()
{
	struct proberep_frame *prrpf = (struct proberep_frame*)pktPROBEREP_->access(hdr_mac::offset_);
	prrpf->prrpf_timestamp = Scheduler::instance().clock();

	struct hdr_mac802_11 *mh;
	double timeout;

	assert(mhBackoff_.busy() == 0);

	if(pktPROBEREP_ == 0)
 		return -1;
	mh = HDR_MAC802_11(pktPROBEREP_);

 	switch(mh->dh_fc.fc_subtype) {
	case MAC_Subtype_ProbeRep:
		if(! is_idle()) {
			inc_cw();
			mhBackoff_.start(cw_, is_idle());
			return 0;
		}
		setTxState(MAC_MGMT);
		tx_mgmt_ = 1;
		timeout = txtime(phymib_.getPROBEREPlen(), basicRate_)
			+ DSSS_MaxPropagationDelay                     // XXX
			+ phymib_.getSIFS()
			+ txtime(phymib_.getACKlen(), basicRate_)
			+ DSSS_MaxPropagationDelay;
		break;
	default:
		fprintf(stderr, "check_pktPROBEREP:Invalid MAC Control subtype\n");
		exit(1);
	}
	transmit(pktPROBEREP_, timeout);


	return 0;
}


void
Mac802_11::recvPROBEREQ(Packet *p)
{
	struct probereq_frame *prrqf = (struct probereq_frame*)p->access(hdr_mac::offset_);

	if(tx_state_ != MAC_IDLE) {
		discard(p, DROP_MAC_BUSY);
		return;
	}
	u_int32_t /*bss_id,*/ src;
	//bss_id = ETHER_ADDR(prrqf->prrqf_3a);
 	src = ETHER_ADDR(prrqf->prrqf_ta);

	if (!pktPROBEREP_) {
		sendPROBEREP(src);
	} else {
		discard(p, DROP_MAC_BUSY);
		return;
	}


	tx_resume();

	mac_log(p);
}

void
Mac802_11::recvPROBEREP(Packet *p)
{
	struct proberep_frame *prrpf = (struct proberep_frame*)p->access(hdr_mac::offset_);

	u_int32_t /*bss_id,*/ src;

	Pr = p->txinfo_.RxPr;
 	src = ETHER_ADDR(prrpf->prrpf_ta);
	//bss_id = ETHER_ADDR(prrpf->prrpf_ta);

	update_ap_table(src,Pr);

	sendACK(src);


	if(mhSend_.busy() == 0)
		tx_resume();

	mac_log(p);


}

void Mac802_11::checkAssocAuthStatus() {
	if ( addr() != bss_id_ ) {
		if (authenticated == 0 && associated == 0) {

			sendAUTHENTICATE(strongest_ap());
		}
		if (authenticated == 1 && associated == 0) {
			sendASSOCREQ(bss_id_);
		}
	}
}

/* STA's beacon power table funtions
*/
void Mac802_11::update_ap_table(int num, double power) {

	std::list<ap_table>::iterator it;

		ap_list = (struct ap_table*)malloc(sizeof(struct ap_table));
		ap_list->ap_id=num;
		ap_list->ap_power =power;

		ap_list1.push_front(*ap_list);

		free(ap_list);


}


int Mac802_11::strongest_ap() {

	std::list<ap_table>::iterator it;
	it=ap_list1.begin();
	double max_power;
	int ap = (*(ap_list1.begin())).ap_id;
	max_power = 0;
	if (it == ap_list1.end()) {
		return -1;

	}

	for (it=ap_list1.begin(); it != ap_list1.end(); it++) {
		if ((*it).ap_power > max_power) {
			max_power = (*it).ap_power;
			ap = (*it).ap_id;
		}



	}

	return ap;


}


int Mac802_11::find_ap(int num, double power) {

	std::list<ap_table>::iterator it;

	for (it=ap_list1.begin(); it != ap_list1.end(); it++) {
		if ((*it).ap_id  ==  num) {
			if ((*it).ap_power != power) {
				(*it).ap_power = power;
			}
			return 1;
		}

	}

	return 0;

}

void Mac802_11::deletelist() {
	ap_list1.clear();

}


void Mac802_11::add_priority_queue(int num)
{
	if (queue_head == NULL) {
		queue_head = (struct priority_queue*)malloc(sizeof(struct priority_queue));
		queue_head->frame_priority = num;
		queue_head->next = NULL;
	} else if (queue_head->next == NULL && queue_head->frame_priority == 0) {
		 queue_head->frame_priority = num;
	} else {
		push_priority(num);
	}


}

void Mac802_11::push_priority(int num) {
	struct priority_queue *temp;
	temp = queue_head;
	while (temp->next != NULL) {
		temp=temp->next;
	}
	temp->next = (struct priority_queue*)malloc(sizeof(struct priority_queue));
	temp->next->frame_priority = num;
	temp->next->next = NULL;
}

void Mac802_11::delete_lastnode() {
	struct priority_queue *temp;
	temp = queue_head;
	if (queue_head == NULL) {
		return;
	}
	if (queue_head->next == NULL) {
		return;
	}

	while (temp->next->next != NULL) {

		temp = temp->next;
	}

	temp->next = NULL;
	free(temp->next);
}

void Mac802_11::shift_priority_queue()
{
	struct priority_queue *temp;

	if (queue_head == NULL) {
		return;
	}
	if (queue_head->next == NULL) {
		queue_head->frame_priority = 0;
		goto done;
	}

	temp =  queue_head;
	while(temp->next != NULL) {
		temp->frame_priority = temp->next->frame_priority;
		temp = temp->next;
	}

	delete_lastnode();

done:
	temp = queue_head;

}
