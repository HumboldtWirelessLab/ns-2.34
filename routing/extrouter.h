/*
 * XXX Insert CU copyright stuff here...
 * 
 * This defines the interface used by all external raw packet routing
 * modules bolted on to ns.
 *
 */

#ifndef __ns_extrouter_h__
#define __ns_extrouter_h__

class ExtRouter
{

public:
  enum {
    IFID_NONE = -1,
    IFID_KERNELTAP = 0,
    IFID_FIRSTIF = 1,
    IFID_LASTIF = 32,
    IFID_FIRSTIFDROP = 33,
    IFID_LASTIFDROP = 64
  };
  virtual ~ExtRouter();
  virtual int route(Packet* p) = 0;
  
};

#endif
