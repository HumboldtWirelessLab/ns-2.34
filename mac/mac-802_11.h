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
 * $Header: /cvsroot/nsnam/ns-2/mac/mac-802_11.h,v 1.29 2008/12/13 23:22:58 tom_henderson Exp $
 *
 * Ported from CMU/Monarch's code, nov'98 -Padma.
 * wireless-mac-802_11.h
 */

#ifndef ns_mac_80211_h
#define ns_mac_80211_h

// Added by Sushmita to support event tracing (singal@nunki.usc.edu)
#include "address.h"
#include "ip.h"
#include "mac-timers.h"
#include "marshall.h"
#include <math.h>
#include <stddef.h>
#include <list>
#include <backoffscheme.h>

#include <click/../../elements/brn/wifi/brnwifi.hh>

class EventTrace;

#define GET_ETHER_TYPE(x)		GET2BYTE((x))
#define SET_ETHER_TYPE(x,y)            {u_int16_t t = (y); STORE2BYTE(x,&t);}

/* ======================================================================
   Frame Formats
   ====================================================================== */

#define	MAC_ProtocolVersion	0x00

#define MAC_Type_Management	0x00
#define MAC_Type_Control	0x01
#define MAC_Type_Data		0x02
#define MAC_Type_Reserved	0x03

#define MAC_Subtype_RTS		0x0B
#define MAC_Subtype_CTS		0x0C
#define MAC_Subtype_ACK		0x0D
#define MAC_Subtype_Data	0x00

#define MAC_Subtype_80211_Beacon	0x08
#define MAC_Subtype_AssocReq	0x00
#define MAC_Subtype_AssocRep	0x01
#define MAC_Subtype_Auth	0x0C
#define MAC_Subtype_ProbeReq	0x04
#define MAC_Subtype_ProbeRep	0x05
#define MAC_Subtype_ProbeRep    0x05
#define MAC_Subtype_QOS         0x80
#define MAC_Subtype_QOS_NULL    0xc0

#define MAC_Max_HW_Queue	4

struct frame_control {
	u_char		fc_subtype		: 4;
	u_char		fc_type			: 2;
	u_char		fc_protocol_version	: 2;

	u_char		fc_order		: 1;
	u_char		fc_wep			: 1;
	u_char		fc_more_data		: 1;
	u_char		fc_pwr_mgt		: 1;
	u_char		fc_retry		: 1;
	u_char		fc_more_frag		: 1;
	u_char		fc_from_ds		: 1;
	u_char		fc_to_ds		: 1;
};

struct rts_frame {
	struct frame_control	rf_fc;
	u_int16_t		rf_duration;
	u_char			rf_ra[ETHER_ADDR_LEN];
	u_char			rf_ta[ETHER_ADDR_LEN];
	u_char			rf_fcs[ETHER_FCS_LEN];
};

struct rts_frame_no_fcs {
  u_int8_t    type;
  u_int8_t    ctrl;
  u_int16_t   rf_duration;
  u_char      rf_ra[ETHER_ADDR_LEN];
  u_char      rf_ta[ETHER_ADDR_LEN];
};

struct cts_frame {
	struct frame_control	cf_fc;
	u_int16_t		cf_duration;
	u_char			cf_ra[ETHER_ADDR_LEN];
	u_char			cf_fcs[ETHER_FCS_LEN];
};

struct cts_frame_no_fcs {
  u_int8_t    type;
  u_int8_t    ctrl;
  u_int16_t   cf_duration;
  u_char      cf_ra[ETHER_ADDR_LEN];
};

struct ack_frame {
	struct frame_control	af_fc;
	u_int16_t		af_duration;
	u_char			af_ra[ETHER_ADDR_LEN];
	u_char			af_fcs[ETHER_FCS_LEN];
};

struct ack_frame_no_fcs {
  u_int8_t    type;
  u_int8_t    ctrl;
  u_int16_t   af_duration;
  u_char      af_ra[ETHER_ADDR_LEN];
};

struct beacon_frame {
	struct frame_control	bf_fc;
	u_int16_t		bf_duration;
	u_char			bf_ra[ETHER_ADDR_LEN];
	u_char			bf_ta[ETHER_ADDR_LEN];
	u_char			bf_3a[ETHER_ADDR_LEN];
	u_int16_t		bf_scontrol;
	double			bf_timestamp;
	double			bf_bcninterval;
	u_int8_t		bf_datarates[1];
	u_char			bf_fcs[ETHER_FCS_LEN];
};

struct assocreq_frame {
	struct frame_control	acrqf_fc;
	u_int16_t		acrqf_duration;
	u_char			acrqf_ra[ETHER_ADDR_LEN];
	u_char			acrqf_ta[ETHER_ADDR_LEN];
	u_char			acrqf_3a[ETHER_ADDR_LEN];
	u_int16_t		acrqf_scontrol;
	u_char			acrqf_fcs[ETHER_FCS_LEN];
};

struct assocrep_frame {
	struct frame_control	acrpf_fc;
	u_int16_t		acrpf_duration;
	u_char			acrpf_ra[ETHER_ADDR_LEN];
	u_char			acrpf_ta[ETHER_ADDR_LEN];
	u_char			acrpf_3a[ETHER_ADDR_LEN];
	u_int16_t		acrpf_scontrol;
	u_int16_t		acrpf_statuscode;
	u_char			acrpf_fcs[ETHER_FCS_LEN];
};

struct auth_frame {
	struct frame_control	authf_fc;
	u_int16_t		authf_duration;
	u_char			authf_ra[ETHER_ADDR_LEN];
	u_char			authf_ta[ETHER_ADDR_LEN];
	u_char			authf_3a[ETHER_ADDR_LEN];
	u_int16_t		authf_scontrol;
	u_int16_t		authf_algono;
	u_int16_t		authf_seqno;
	u_int16_t		authf_statuscode;
	u_char			authf_fcs[ETHER_FCS_LEN];
};

struct probereq_frame {
	struct frame_control	prrqf_fc;
	u_int16_t		prrqf_duration;
	u_char			prrqf_ra[ETHER_ADDR_LEN];
	u_char			prrqf_ta[ETHER_ADDR_LEN];
	u_char			prrqf_3a[ETHER_ADDR_LEN];
	u_int16_t		prrqf_scontrol;
	u_char			prrqf_fcs[ETHER_FCS_LEN];
};

struct proberep_frame {
	struct frame_control	prrpf_fc;
	u_int16_t		prrpf_duration;
	u_char			prrpf_ra[ETHER_ADDR_LEN];
	u_char			prrpf_ta[ETHER_ADDR_LEN];
	u_char			prrpf_3a[ETHER_ADDR_LEN];
	u_int16_t		prrpf_scontrol;
	double			prrpf_timestamp;
	double			prrpf_bcninterval;
	u_int8_t		prrpf_datarates[1];
	u_char			prrpf_fcs[ETHER_FCS_LEN];
};



// XXX This header does not have its header access function because it shares
// the same header space with hdr_mac.
struct hdr_mac802_11 {
	struct frame_control	dh_fc;
	u_int16_t		dh_duration;
	u_char                  dh_ra[ETHER_ADDR_LEN];
        u_char                  dh_ta[ETHER_ADDR_LEN];
        u_char                  dh_3a[ETHER_ADDR_LEN];
	u_char			dh_4a[ETHER_ADDR_LEN];
	u_int16_t		dh_scontrol;
	u_char			dh_body[1]; // size of 1 for ANSI compatibility
};

// XXX This header does not have its header access function because it shares
// the same header space with hdr_mac.
struct hdr_mac802_11_qos {
    struct frame_control    dh_fc;
    u_int16_t       dh_duration;
    u_char          dh_ra[ETHER_ADDR_LEN];
    u_char          dh_ta[ETHER_ADDR_LEN];
    u_char          dh_3a[ETHER_ADDR_LEN];
    u_int16_t       dh_scontrol;
    u_int16_t       dh_qos;
    u_char          dh_body[1]; // size of 1 for ANSI compatibility
};

struct client_table {
	int client_id;
	int auth_status;
	int assoc_status;
};

struct ap_table {
	int ap_id;
	double ap_power;
};

struct priority_queue {
	int frame_priority;
	struct priority_queue *next;
};

#define PERFORMANCE_MODE_IDLE 0
#define PERFORMANCE_MODE_BUSY 1
#define PERFORMANCE_MODE_RX   2
#define PERFORMANCE_MODE_TX   3

#define CYCLES_PER_SECONDS (double)44000000.0

struct performance_stats {
  int current_mode;
  bool jam;
  double ts_mode_start;

  double cylce_counter;
  double busy_counter;
  double rx_counter;
  double tx_counter;
};

/* ======================================================================
   Definitions
   ====================================================================== */

/* Must account for propagation delays added by the channel model when
 * calculating tx timeouts (as set in tcl/lan/ns-mac.tcl).
 *   -- Gavin Holland, March 2002
 */
#define DSSS_MaxPropagationDelay        0.000002        // 2us   XXXX

class PHY_MIB {
public:
	PHY_MIB(Mac802_11 *parent);

	inline u_int32_t getCWMin() { return(CWMin[0]); }
        inline u_int32_t getCWMax() { return(CWMax[0]); }
	inline u_int32_t getCWMin(u_int32_t q) { return(CWMin[q]); }
        inline u_int32_t getCWMax(u_int32_t q) { return(CWMax[q]); }
	inline double getSlotTime() { return(SlotTime); }
	inline double getBeaconInterval() { return(BeaconInterval); }
	inline double getSIFS() { return(SIFSTime); }
	inline double getPIFS() { return(SIFSTime + SlotTime); }
	inline double getDIFS() { return(SIFSTime + 2 * SlotTime); }
	inline double getEIFS() {
		// see (802.11-1999, 9.2.10)
		return(SIFSTime + getDIFS()
                       + (8 *  getACKlen())/PLCPDataRate);
	}
	inline u_int32_t getPreambleLength() { return(PreambleLength); }
	inline double getPLCPDataRate() { return(PLCPDataRate); }


	inline u_int32_t getPLCPhdrLen() {
		return((PreambleLength + PLCPHeaderLength) >> 3);
	}

	inline u_int32_t getHdrLen11() {
#ifdef BRN_DEBUG
		printf("PLCP: %d Offset: %d  FCS: %d\n", getPLCPhdrLen(), offsetof(struct hdr_mac802_11, dh_body[0]),
		                                         ETHER_FCS_LEN);
#endif
		return(getPLCPhdrLen() + offsetof(struct hdr_mac802_11, dh_body[0])
                       + ETHER_FCS_LEN);
	}

	inline u_int32_t getHdrLen11_click(bool set_fcs) {
		return(getPLCPhdrLen() + sizeof(click_wifi) + (set_fcs?ETHER_FCS_LEN:0));
	}

	inline u_int32_t getRTSlen() {
		return(getPLCPhdrLen() + sizeof(struct rts_frame));
	}

	inline u_int32_t getCTSlen() {
		return(getPLCPhdrLen() + sizeof(struct cts_frame));
	}

	inline u_int32_t getACKlen() {
		return(getPLCPhdrLen() + sizeof(struct ack_frame));
	}
	inline u_int32_t getBEACONlen() {
		return(getPLCPhdrLen() + sizeof(struct beacon_frame));
	}
	inline u_int32_t getASSOCREQlen() {
		return(getPLCPhdrLen() + sizeof(struct assocreq_frame));
	}
	inline u_int32_t getASSOCREPlen() {
		return(getPLCPhdrLen() + sizeof(struct assocrep_frame));
	}
	inline u_int32_t getAUTHENTICATElen() {
		return(getPLCPhdrLen() + sizeof(struct auth_frame));
	}
	inline u_int32_t getPROBEREQlen() {
		return(getPLCPhdrLen() + sizeof(struct probereq_frame));
	}
	inline u_int32_t getPROBEREPlen() {
		return(getPLCPhdrLen() + sizeof(struct proberep_frame));
	}

	void setBackoffQueueInfo(int *boq);
	void getBackoffQueueInfo(int *boq);

 private:


	u_int32_t	CWMin[MAC_Max_HW_Queue];
	u_int32_t	CWMax[MAC_Max_HW_Queue];
	double		SlotTime;
	double		SIFSTime;
	double		BeaconInterval;
	u_int32_t	PreambleLength;
	u_int32_t	PLCPHeaderLength;
	double		PLCPDataRate;

	u_int32_t	NoHwQueues;
};


/*
 * IEEE 802.11 Spec, section 11.4.4.2
 *      - default values for the MAC Attributes
 */
#define MAC_FragmentationThreshold	2346		// bytes
#define MAC_MaxTransmitMSDULifetime	512		// time units
#define MAC_MaxReceiveLifetime		512		// time units




class MAC_MIB {
public:

	MAC_MIB(Mac802_11 *parent);

private:
	u_int32_t	RTSThreshold;
  u_int32_t RTSThresholdDefault;
	u_int32_t	ShortRetryLimit;
	u_int32_t	LongRetryLimit;
	u_int32_t	ScanType;
	double		ProbeDelay;
	double		MaxChannelTime;
	double		MinChannelTime;
	double		ChannelTime;
	u_int32_t	Promisc;
	u_int32_t TXFeedback;
	u_int32_t FilterDub;
  u_int32_t ControlFrames;
  u_int32_t MadwifiTPC;

public:
	u_int32_t	FailedCount;
	u_int32_t	RTSFailureCount;
	u_int32_t	ACKFailureCount;
 public:
       inline u_int32_t getRTSThreshold() { return(RTSThreshold);}
       inline void setRTSThreshold(u_int32_t newRTSTreshold) { RTSThreshold = newRTSTreshold;}
       inline void setRTSThresholdDefault(u_int32_t newRTSTresholdDefault) { RTSThresholdDefault = newRTSTresholdDefault;}
       inline void resetRTSThreshold() { RTSThreshold = RTSThresholdDefault;}
       inline u_int32_t getShortRetryLimit() { return(ShortRetryLimit);}
       inline u_int32_t getLongRetryLimit() { return(LongRetryLimit);}
       inline u_int32_t getScanType() { return(ScanType);}
       inline double getProbeDelay() { return(ProbeDelay);}
       inline double getMaxChannelTime() { return(MaxChannelTime);}
       inline double getMinChannelTime() { return(MinChannelTime);}
       inline double getChannelTime() { return(ChannelTime);}
       inline u_int32_t getPromisc() { return(Promisc);}
       inline u_int32_t getFilterDub() { return(FilterDub);}
       inline u_int32_t getControlFrames() { return(ControlFrames);}
       inline u_int32_t getTXFeedback() { return(TXFeedback);}
       inline u_int32_t getMadwifiTPC() { return(MadwifiTPC);}
};


/* ======================================================================
   The following destination class is used for duplicate detection.
   ====================================================================== */
class Host {
public:
	LIST_ENTRY(Host) link;
	u_int32_t	index;
	u_int32_t	seqno;
};


/* ======================================================================
   The actual 802.11 MAC class.
   ====================================================================== */
class Mac802_11 : public Mac {
	friend class DeferTimer;

	friend class BeaconTimer;
	friend class ProbeTimer;
	friend class BackoffTimer;
	friend class IFTimer;
	friend class NavTimer;
	friend class RxTimer;
	friend class TxTimer;
public:
	Mac802_11();
	void		recv(Packet *p, Handler *h);
	inline int	hdr_dst(char* hdr, int dst = -2);
	inline int	hdr_src(char* hdr, int src = -2);
	inline int	hdr_type(char* hdr, u_int16_t type = 0);

	inline int bss_id() { return bss_id_; }

	// Added by Sushmita to support event tracing
        void trace_event(char *, Packet *);
        EventTrace *et_;

        void getPerformanceCounter(int *perf_count);
        void setBackoffQueueInfo(int *boq);
        void getBackoffQueueInfo(int *boq);
        void handleTXControl(char *txc);
        void getRxTxStats(void *rxtx_stats);

protected:
	void	backoffHandler(void);
	void	deferHandler(void);
	void	BeaconHandler(void);
	void	ProbeHandler(void);
	void	navHandler(void);
	void	recvHandler(void);
	void	sendHandler(void);
	void	txHandler(void);

private:
	void	update_client_table(int num, int auth_status, int assoc_status);
	int	find_client(int num);
	void	update_ap_table(int num, double power);
	int 	strongest_ap();
	int	find_ap(int num, double power);
	void 	deletelist();
	void	passive_scan();
	void	active_scan();
	void	checkAssocAuthStatus();
	int	command(int argc, const char*const* argv);


	void 	add_priority_queue(int num);
	void 	push_priority(int num);
	void 	delete_lastnode();
	void	shift_priority_queue();



	/* In support of bug fix described at
	 * http://www.dei.unipd.it/wdyn/?IDsezione=2435
	 */
	int bugFix_timer_;
	int infra_mode_;
	double BeaconTxtime_;
	int associated;
	int authenticated;
	int handoff;
	double Pr;
	int ap_temp;
	int ap_addr;
	int tx_mgmt_;
	int associating_node_;
	int authenticating_node_;
	int ScanType_;
	int OnMinChannelTime;
	int OnMaxChannelTime;
	int Recv_Busy_;
	int probe_delay;
	/*
	 * Called by the timers.
	 */
	void		recv_timer(void);
	void		send_timer(void);
	int		check_pktCTRL();
	int		check_pktRTS();
	int		check_pktTx();
	int		check_pktASSOCREQ();
	int		check_pktASSOCREP();
	int		check_pktBEACON();
	int		check_pktAUTHENTICATE();
	int		check_pktPROBEREQ();
	int		check_pktPROBEREP();

	/*
	 * Packet Transmission Functions.
	 */
	void	send(Packet *p, Handler *h);
	void 	sendRTS(int dst);
	void	sendCTS(int dst, double duration);
	void	sendACK(int dst);
	void	sendDATA(Packet *p);
	void	sendBEACON(int src);
	void	sendASSOCREQ(int dst);
	void	sendASSOCREP(int dst);
	void	sendPROBEREQ(int dst);
	void	sendPROBEREP(int dst);
	void	sendAUTHENTICATE(int dst);
	void	RetransmitRTS();
	void	RetransmitDATA();
	void	RetransmitASSOCREP();
	void	RetransmitPROBEREP();
	void	RetransmitAUTHENTICATE();

	/*
	 * Packet Reception Functions.
	 */
	void	recvRTS(Packet *p);
	void	recvCTS(Packet *p);
	void	recvACK(Packet *p);
	void	recvDATA(Packet *p);
	void	recvBEACON(Packet *p);
	void	recvASSOCREQ(Packet *p);
	void	recvASSOCREP(Packet *p);
	void	recvPROBEREQ(Packet *p);
	void	recvPROBEREP(Packet *p);
	void	recvAUTHENTICATE(Packet *p);

	void		capture(Packet *p);
	void		collision(Packet *p);
	void		discard(Packet *p, const char* why);
	void		rx_resume(void);
	void		tx_resume(void);

	inline int	is_idle(void);

	/*
	 * Debugging Functions.
	 */
	void		trace_pkt(Packet *p);
	void		dump(char* fname);

	inline int initialized() {
		return (cache_ && logtarget_
                        && Mac::initialized());
	}

	inline void mac_log(Packet *p) {
                logtarget_->recv(p, (Handler*) 0);
        }

	double txtime(Packet *p);
	double txtime(double psz, double drt);
	double txtime(int bytes) { /* clobber inherited txtime() */ abort(); return 0;}

	inline void transmit_abort(Packet *p, double timeout);
	inline void transmit(Packet *p, double timeout);
	inline void checkBackoffTimer(void);
	inline void postBackoff(int pri);
  inline void setRxState(MacState newState, bool jam);
	inline void setTxState(MacState newState);

	inline void inc_cw() {
    retry_number_++;
    if ( bo_scheme_ == NULL ) {
      cw_ = (cw_ << 1) + 1;
      if (cw_ > phymib_.getCWMax(queue_index_)) cw_ = phymib_.getCWMax(queue_index_);
    } else {
      cw_ = bo_scheme_->inc_cw(phymib_.getCWMin(queue_index_),phymib_.getCWMax(queue_index_));
    }
    //fprintf(stderr,"CW: %d\n",cw_);
	}
	inline void rst_cw() {
    retry_number_ = 0;
    if ( bo_scheme_ == NULL ) {
      cw_ = phymib_.getCWMin(queue_index_);
    } else {
      cw_ = bo_scheme_->reset_cw(phymib_.getCWMin(queue_index_),phymib_.getCWMax(queue_index_));
    }
  }

	inline double sec(double t) { return(t *= 1.0e-6); }
	inline u_int16_t usec(double t) {
		u_int16_t us = (u_int16_t)floor((t *= 1e6) + 0.5);
		return us;
	}
	inline void set_nav(u_int16_t us) {
		double now = Scheduler::instance().clock();
		double t = us * 1e-6;
		//fprintf(stderr, "now: %f us: %d t+now: %f nav: %f\n", now, us, t+now, nav_);
		if((now + t) > nav_) {
			nav_ = now + t;
			if(mhNav_.busy())
				mhNav_.stop();
			mhNav_.start(t);
		}
	}

protected:
	PHY_MIB         phymib_;
        MAC_MIB         macmib_;

       /* the macaddr of my AP in BSS mode; for IBSS mode
        * this is set to a reserved value IBSS_ID - the
        * MAC_BROADCAST reserved value can be used for this
        * purpose
        */
       int     bss_id_;
    int MadwifiTPC_;
       enum    {IBSS_ID=MAC_BROADCAST};
       enum    {
		PASSIVE = 0,
       		ACTIVE = 1
		};

private:
	double		basicRate_;
 	double		dataRate_;
	struct client_table	*client_list;
	struct ap_table	*ap_list;
	struct priority_queue *queue_head;

	/*
	 * Mac Timers
	 */
	IFTimer		mhIF_;		// interface timer
	NavTimer	mhNav_;		// NAV timer
	RxTimer		mhRecv_;		// incoming packets
	TxTimer		mhSend_;		// outgoing packets

	DeferTimer	mhDefer_;	// defer timer
	BackoffTimer	mhBackoff_;	// backoff timer
	BeaconTimer	mhBeacon_;	// Beacon Timer
	ProbeTimer	mhProbe_;	//Probe timer,

	/* ============================================================
	   Internal MAC State
	   ============================================================ */
	double		nav_;		// Network Allocation Vector

	MacState	rx_state_;	// incoming state (MAC_RECV or MAC_IDLE)
	MacState	tx_state_;	// outgoint state
	int		tx_active_;	// transmitter is ACTIVE

	Packet          *eotPacket_;    // copy for eot callback

	Packet		*pktRTS_;	// outgoing RTS packet
	Packet		*pktCTRL_;	// outgoing non-RTS packet
	Packet		*pktBEACON_;	//outgoing Beacon Packet
	Packet		*pktASSOCREQ_;	//Association request
	Packet		*pktASSOCREP_;	// Association response
	Packet		*pktAUTHENTICATE_; //Authentication
	Packet		*pktPROBEREQ_;	//Probe Request
	Packet		*pktPROBEREP_;	//Probe Response

  BackoffScheme *bo_scheme_;
  u_int32_t	cw_;		// Contention Window
  u_int32_t retry_number_;
	u_int32_t	queue_index_;
	u_int32_t	ssrc_;		// STA Short Retry Count
	u_int32_t	slrc_;		// STA Long Retry Count
  u_int32_t tx_count;    // STA Long Retry Count
  u_int32_t rts_tx_count;

	int		min_frame_len_;

	NsObject*	logtarget_;
	NsObject*       EOTtarget_;     // given a copy of packet at TX end


	/* ============================================================
	   Duplicate Detection state
	   ============================================================ */
	u_int16_t	sta_seqno_;	// next seqno that I'll use
	int		cache_node_count_;
	Host		*cache_;


	std::list<struct client_table> client_list1;
	std::list<struct ap_table> ap_list1;

  struct performance_stats perf_stats_;

  uint8_t tx_source_mac_[6];
  uint8_t tx_target_mac_[6];
  enum    {TX_CONTROL_IDLE = 0, TX_CONTROL_BUSY = 1, TX_CONTROL_ABORT = 2};
  uint8_t tx_control_state_;

  struct rx_tx_stats rxtx_stats_;
};

#endif /* __mac_80211_h__ */

