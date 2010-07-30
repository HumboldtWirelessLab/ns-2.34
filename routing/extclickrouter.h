/*
 * XXX Insert CU copyright stuff here...
 * 
 * This defines the interface used by all external raw packet routing
 * modules bolted on to ns.
 *
 */

#ifndef __ns_extclickrouter_h__
#define __ns_extclickrouter_h__

class ExtClickRouter
{

 public:
  ExtClickRouter();
  virtual ~ExtClickRouter();
  virtual int recv(Packet* p);

  void* SetClickRouterPtr(void* crtptr);

 private:
  void* clickrouter_;
  
};

#endif
