#ifndef PACKET_ANNO_H
#define PACKET_ANNO_H

/* Define macros for declaring packed structures. */
#ifdef __GNUC__
#define CLICK_PACKED_STRUCTURE(open, close) open close __attribute__((packed))
#define CLICK_SIZE_PACKED_STRUCTURE(open, close) open close __attribute__((packed))
#define CLICK_SIZE_PACKED_ATTRIBUTE __attribute__((packed))
#else
#define CLICK_PACKED_STRUCTURE(open, close) _Cannot_pack_structure__Use_GCC
#define CLICK_SIZE_PACKED_STRUCTURE(open, close) open close
#define CLICK_SIZE_PACKED_ATTRIBUTE
#endif

#include <clicknet/wifi.h>

#endif /* PACKET_ANNO_H */
