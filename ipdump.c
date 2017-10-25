/* IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 * used for ax25spyd. dg9ep, 1999
 * $id$
 */

#include <stdio.h>
#include "monixd.h"

#define IPLEN           20

#define MF              0x2000
#define DF              0x4000
#define CE              0x8000

#define IPIPNEW_PTCL    4
#define IPIPOLD_PTCL    94
#define TCP_PTCL        6
#define UDP_PTCL        17
#define ICMP_PTCL       1
#define AX25_PTCL       93
#define RSPF_PTCL       73

int
   get16(unsigned char *cp)
{
	int x;

	x  = *cp++;   x <<= 8;
	x |= *cp++;

	return(x);
}

void
ip_dump(char *dispbuf, unsigned char *data, int length, int dumpstyle)
{
        int hdr_length;
        int tos;
        int ip_length;
        int id;
        int fl_offs;
        int flags;
        int offset;
        int ttl;
        int protocol;
        unsigned char *source, *dest;

	/* Protocol already displayed from pid2str
        mprintf(dispbuf, T_PROTOCOL, "IP:"); */
        /* Sneak peek at IP header and find length */
        hdr_length = (data[0] & 0xf) << 2;

        if (hdr_length < IPLEN) {
                mprintf(dispbuf, T_ERROR, " bad IP header\n");
                data_dump(dispbuf, data, length, HEX);
                return;
        }

        tos       = data[1];
        ip_length = get16(data + 2);
        id        = get16(data + 4);
        fl_offs   = get16(data + 6);
        flags     = fl_offs & 0xE000;
        offset    = (fl_offs & 0x1FFF) << 3;
        ttl       = data[8];
        protocol  = data[9];
        source    = data + 12;
        dest      = data + 16;
        /*-----------------------------------------------*/
        mprintf(dispbuf, T_ADDR, " %d.%d.%d.%d",source[0], source[1], source[2], source[3]);
        mprintf(dispbuf, T_ZIERRAT, "->");
        mprintf(dispbuf, T_ADDR, "%d.%d.%d.%d",dest[0], dest[1], dest[2], dest[3]);

        mprintf(dispbuf, T_IPHDR, " len:");
        mprintf(dispbuf, T_HDRVAL, "%d", ip_length);

        /* show ip-headerlen only if not standard */
        if( hdr_length != 20 ) {
            mprintf(dispbuf, T_IPHDR,  " ihl:");
            mprintf(dispbuf, T_HDRVAL, "%d", hdr_length);
        }
        mprintf(dispbuf, T_IPHDR,  " ttl:");
        mprintf(dispbuf, T_HDRVAL, "%d", ttl);

        if (tos != 0)
                mprintf(dispbuf, T_IPHDR, " tos: %d", tos);

        if (offset != 0 || (flags & MF))
                mprintf(dispbuf, T_IPHDR, " id:%d offs:%d", id, offset);

        if (flags & DF) mprintf(dispbuf, T_HDRVAL, " DF");
        if (flags & MF) mprintf(dispbuf, T_HDRVAL, " MF");
        if (flags & CE) mprintf(dispbuf, T_HDRVAL, " CE");

        data   += hdr_length;
        length -= hdr_length;

        if (offset != 0) {
                mprintf(dispbuf, T_IPHDR, "\n");
                if (length > 0)
                        data_dump(dispbuf, data, length, dumpstyle);
                return;
        }

        switch (protocol) {
        case TCP_PTCL:
                mprintf(dispbuf, T_PROTOCOL, " TCP\n");
#if 0
		tcp_dump(data, length, dumpstyle);
#endif
                break;
        case UDP_PTCL:
                mprintf(dispbuf, T_PROTOCOL, " UDP\n");
#if 0
		udp_dump(data, length, dumpstyle);
#endif
                break;
        case ICMP_PTCL:
		mprintf(dispbuf, T_PROTOCOL, " ICMP\n");
#if 0
		icmp_dump(data, length, dumpstyle);
#endif
                break;
        case AX25_PTCL:
                mprintf(dispbuf, T_PROTOCOL, " AX25\n");
                /* $TODO  ax25_dump(data, length, dumpstyle); */
                ai_dump(dispbuf, data, length);
                break;
        case RSPF_PTCL:
                mprintf(dispbuf, T_PROTOCOL, " RSPF\n");
#if 0
		rspf_dump(data, length);
#endif
                break;
        case IPIPOLD_PTCL:
        case IPIPNEW_PTCL:
                mprintf(dispbuf, T_PROTOCOL, " IP\n");
                ip_dump(dispbuf, data, length, dumpstyle);
                break;
        default:
                mprintf(dispbuf, T_PROTOCOL, " prot %d\n", protocol);
                data_dump(dispbuf, data, length, dumpstyle);
                break;
        }
}

