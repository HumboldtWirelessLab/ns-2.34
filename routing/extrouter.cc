/*
 * XXX Insert CU copyright stuff here...
 * 
 * This defines the interface used by all external raw packet routing
 * modules bolted on to ns.
 *
 */

#include "packet.h"
#include "extrouter.h"

// Just here so we get virtual destructors
ExtRouter::~ExtRouter() {
}

