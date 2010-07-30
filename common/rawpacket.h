/*
 * rawpacket.h
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

/*
 * Raw packet type. 
 */
struct hdr_raw {
  /*
   * This indicates the actual type of the stuff in the
   * packet. The actual packet stuff is pointed to by 
   * the data thing.
   */
  int subtype;

  /*
   * Not many raw subtypes defined so far.
   */
  enum {
    NONE,
    PSTRING,
    IP,
    ETHERNET,
    MADWIFI
  };

  /*
   * This is the equivalent packet type in ns-2. Sometimes we
   * want to maintain the raw packet data _and_ the ns-2 headers
   * for that particular type in parallel, e.g. so we can use
   * the existing ns-2 trace printing routines. However, we
   * still want to keep the packet type as PT_RAW, so we store
   * the ns-2 type in this field.
   */
  int ns_type;
 
  /* Packet header access functions */
  static int offset_;
  inline static int& offset() { return offset_; }
  inline static hdr_raw* access(const Packet* p) {
    return (hdr_raw*) p->access(offset_);
  }
};

/*
 * The base RawAgent class
 */
class RawAgent : public Agent {
 public:
  RawAgent();
  int command(int argc,const char*const* argv);
  void recv(Packet*, Handler*);
  virtual void sendmsg(int nbytes, const char *flags = 0);
 protected:
  void send_udp_str(u_long saddr,u_short sport,u_long daddr,u_short dport,
		    const char* payload);
  void send_udp(u_long saddr,u_short sport,u_long daddr,u_short dport,
		const char* payload,int paylen);

  u_int16_t ipseq_;
  u_long srcip_;
  u_short srcport_;
  u_long destip_;
  u_short destport_;
};
