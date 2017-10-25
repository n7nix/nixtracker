/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */

/* dantracker aprs tracker
 *
 * Copyright 2012 Dan Smith <dsmith@danplanet.com>
 */

#ifdef HAVE_AX25_TRUE

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netdb.h>
#include <netax25/ax25.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_packet.h>
#include <netax25/axlib.h>
#include <netax25/axconfig.h>

#include <stdbool.h>
#include <byteswap.h>

#ifndef CONSOLE_SPY
#include <json/json.h>
#endif /* NOT CONSOLE_SPY */

#define DEBUG 1

#include "aprs.h"
#include "aprs-is.h"
#include "aprs-ax25.h"
#include "monixd.h"
#include "util.h"
#include "aprs.h"

#ifdef B2F_DECODE
#include "buffer.h"
struct buffer *b2f_decode(struct state *state, char *dumpbuf, uint8_t *rawbuf, int size);
#endif /* B2F_DECODE */

int ui_connect(struct state *state);

/* KISS parameters from listen/kissdump.c */
#define	PARAM_DATA	0
#define	PARAM_TXDELAY	1
#define	PARAM_PERSIST	2
#define	PARAM_SLOTTIME	3
#define	PARAM_TXTAIL	4
#define	PARAM_FULLDUP	5
#define	PARAM_HW	6
#define	PARAM_RETURN	15	/* Should be 255, but is ANDed with 0x0F */

bool bspysock_enable = true;


void dump_hex(unsigned char *buf, int len)
{
        int i;
        unsigned char *pBuf;
        pBuf = buf;

        for(i=0; i < len; i++) {
                printf("%02x ", *pBuf++);
        }
        printf("\n");
}

bool packet_qualify(struct state *state, struct sockaddr *sa, unsigned char *buf, int size)
{
        bool packet_status = true;


        if (state->ax25_recvproto == ETH_P_ALL) {       /* promiscuous mode? */
#ifdef USE_SOCK_PACKET
                struct ifreq ifr;

                strncpy(ifr.ifr_name, sa->sa_data, IF_NAMESIZE);

                if (ioctl(state->tncfd, SIOCGIFHWADDR, &ifr) < 0
                    || ifr.ifr_hwaddr.sa_family != AF_AX25) {
#if 0 /* too verbose */
                        pr_debug("%s:%s(): qualify fail due to protocol family: 0x%02x expected 0x%02x\n",
                                 __FILE__, __FUNCTION__, ifr.ifr_hwaddr.sa_family, AF_AX25);
#endif
                        return false;          /* not AX25 so ignore this packet */
                }
#else
                struct  sockaddr_ll *sll = (struct sockaddr_ll *)sa;

                if(sll->sll_protocol != htons(ETH_P_AX25)) {
                        return false;
                }

#endif
        }
#ifdef USE_SOCK_PACKET
        if ( (state->conf.ax25_pkt_device_filter == AX25_PKT_DEVICE_FILTER_ON) &&
             (state->ax25_dev != NULL) &&
             (strcmp(state->ax25_dev, sa->sa_data) != 0) ) {
                pr_debug("%s:%s(): qualify fail due to device %s\n", __FILE__, __FUNCTION__, sa->sa_data);
                packet_status = false;
        }
#endif

        if( (size > 512) && packet_status) {
                printf("%s:%s() Received oversize AX.25 frame %d bytes\n",
                       __FILE__, __FUNCTION__, size);
                packet_status = false;
        }
        if(!ISAX25ADDR(buf[0] >> 1)) {
                pr_debug("%s:%s(): qualify fail due first char not a valid AX.25 address char 0x%02x 0x%02x 0x%02x\n",
                         __FILE__, __FUNCTION__, buf[0], buf[1], buf[2] );
                packet_status = false;
        }

        return(packet_status);
}

void fap_conversion_debug(char *rxbuf, char *buffer, int len)
{
        unsigned char *qBuf, *sBuf;
        int i;

        rxbuf[len] = '\0';
        printf("ax25frame: len=%d, rxbuf: %s\n",
               len, rxbuf);

        if(len && buffer != NULL) {
                printf("fap: %s\n", buffer);
        }

        /* dump hex */
        printf("hex:\n");
        dump_hex((unsigned char *)rxbuf, len);

        /* dump ascii */
        printf("ascii:\n");
        qBuf = (unsigned char *)rxbuf;

        for(i=0; i < len; i++) {
                printf("%2c ", *qBuf++);
        }
        printf("\n");

        /* Dump ascii with APRS converstion */
        printf("shifted:\n");
        sBuf = (unsigned char *)rxbuf;

        i = 0;
        while( ((*sBuf & 0x01) == 0) && (i++ < len) ) {
                printf("%2c ", *sBuf >> 1);
                sBuf++;
        }
        printf("\n");
}

#ifndef CONSOLE_SPY
int get_ax25packet(struct state *state, char *buffer, unsigned int *len)
{
        char rxbuf[1500];
        struct sockaddr_storage sa;
        socklen_t sa_len = sizeof(sa);
        int rx_socket = state->tncfd;
        int ret = 0;
        int size = 0;

        buffer[0] = '\0';

        if ((size = recvfrom(rx_socket, rxbuf, sizeof(rxbuf), 0, (struct sockaddr *)&sa, &sa_len)) == -1) {
                perror("AX25 Socket recvfrom error");
                printf("%s:%s(): socket %d\n", __FILE__, __FUNCTION__, rx_socket);
                fflush(stdout);
        } else {
                if (packet_qualify(state, (struct sockaddr *)&sa, (unsigned char *)rxbuf+1, size-1)) {

                        if( (ret = fap_ax25_to_tnc2(rxbuf+1, size-1, buffer, len))) {
                                if(state->debug.verbose_level > 2) {
                                        printf("[%d/%u]: %s\n", size, *len, buffer);
                                        fflush(stdout);
                                } else {
                                        printf("."); fflush(stdout);
                                }
                        } else {
                                fap_conversion_debug(rxbuf+1, buffer, *len);
                        }
                }
        }
        return ret;
}

int send_ui_sock(struct state *state, char *msg)
{
        int ret;
        int len;
        int *fd = &state->dspfd;

        /* If socket is not valid try connecting */
        if (*fd <= 0) {
                *fd = ui_connect(state);
        }

        /* check for a valid socket */
        if( *fd > 0) {
                len=strlen(msg);

                ret = send(*fd, msg, len, MSG_NOSIGNAL);
                if (ret < 0) {
                        printf("%s:%s(): closing file descriptor %d due to error\n",
                               __FILE__, __FUNCTION__, *fd);

                        close(*fd);
                        *fd = -1;
                }
        }

        return ret;
}

#endif /* NOT CONSOLE_SPY */

int aprsax25_connect(struct state *state)
{
        int rx_sock;
        unsigned int tx_src_addrlen;   /* Length of addresses */
        char *ax25dev;
        struct full_sockaddr_ax25 sockaddr_ax25_dest;
        struct full_sockaddr_ax25 sockaddr_ax25_src;
        char *portcallsign, *src_addr;

        char *ax25port = state->conf.ax25_port;

        /* Protocol to use for receive, either ETH_P_ALL or ETH_P_AX25 */
        /* Choosing ETH_P_ALL would not only pick outbound packets, but all of
         *  the ethernet traffic..
         * ETH_P_AX25 picks only inbound-at-ax25-devices packets. */
        state->ax25_recvproto = ETH_P_AX25;
        if (state->debug.console_spy_enable) {
                state->ax25_recvproto = ETH_P_ALL;
        }

        if (ax25_config_load_ports() == 0) {
                fprintf(stderr, "%s:%s() no AX.25 port data configured\n", __FILE__, __FUNCTION__);
                /* Fix this */
                return(-1);
        }


        /* This function maps the port name onto the device
         * name of the port. See /etc/ax25/ax25ports */
        if ((ax25dev = ax25_config_get_dev(ax25port)) == NULL) {
                fprintf(stderr, "%s:%s(): invalid port name - %s\n",
                          __FILE__, __FUNCTION__, ax25port);
                /* Fix this */
                return(-1);
        }

        /* Save the ax25 device name to be used by packet_qualify */
        state->ax25_dev = ax25dev;

        /*
         * Open up a receive & transmit socket to the AX.25 stack
         */
#if 0 /* why doesn' this work */
        if ( (rx_sock = socket(AF_AX25, SOCK_RAW, htons(state->ax25_recvproto))) == -1) {
                perror("receive AX.25 socket");
                return(-1);
        }
#endif
#ifdef USE_SOCK_PACKET /* older systems */
        if ( (rx_sock = socket(AF_INET, SOCK_PACKET, htons(state->ax25_recvproto))) == -1) {
                perror("receive AX.25 socket");
                return(-1);
        }
#else  /* for newer systems */
        if ( (rx_sock = socket(PF_PACKET, SOCK_RAW, htons(state->ax25_recvproto))) == -1) {
                perror("receive AX.25 socket");
                return(-1);
        }
#endif
        /*
         * Choice of which source call sign to use
         * - config'ed in /etc/ax25/axports for the ax25 stack
         * - config'ed in aprs.ini as station:mycall
         */
        /* Get the port callsign from the ax25 stack */
        if ((portcallsign = ax25_config_get_addr(ax25port)) == NULL) {
                perror("ax25_config_get_addr");
                return -1;
        }

        printf("%s: Using callsign %s with ax.25port %s\n", __FUNCTION__, portcallsign, ax25port);
        /*
         * Check if the call sign in the ini file is the same as what's
         * config'ed for the AX.25 stack in /etc/ax25/axports.
         * If they are different set up source address for AX.25 stack.
         */
        if (state->mycall != NULL && strcmp(state->mycall, portcallsign) != 0) {
                printf("Note: identifying callsign (%s) and ax25 callsign (%s) are different\n",
                       state->mycall, portcallsign);
                if ((src_addr = (char *) malloc(strlen(state->mycall) + 1 + strlen(portcallsign) + 1)) == NULL)
                        return -1;
                sprintf(src_addr, "%s %s", state->mycall, portcallsign);
        } else {
                if ((src_addr = strdup(portcallsign)) == NULL)
                        return -1;
        }

        /* save source address (your call sign & ax.25 call sign) for convenience.
         * Displayed as source callsign for messaging on web interface
         */
        state->ax25_srcaddr = src_addr;
        pr_debug("%s(): Port %s is using device %s, callsign %s\n",
                 __FUNCTION__, ax25port, ax25dev, src_addr);

        /* Convert to AX.25 addresses */
        memset((char *)&sockaddr_ax25_src, 0, sizeof(sockaddr_ax25_src));
        memset((char *)&sockaddr_ax25_dest, 0, sizeof(sockaddr_ax25_dest));

        if ((tx_src_addrlen = ax25_aton(state->ax25_srcaddr, &sockaddr_ax25_src)) == -1) {
                perror("ax25_config_get_addr src");
                return -1;
        }

#ifndef CONSOLE_SPY
        {
                unsigned int tx_dest_addrlen;   /* Length of addresses */
                char *aprspath = state->conf.aprs_path;
                int tx_sock;


        if(aprspath == NULL || strlen(aprspath) == 0 ) {
                fprintf(stderr, "%s:%s() no APRS path configured\n", __FILE__, __FUNCTION__);
                /* Fix this */
                return(-1);
        }

        if ((tx_dest_addrlen = ax25_aton(aprspath, &sockaddr_ax25_dest)) == -1) {
                perror("ax25_config_get_addr dest");
                return -1;
        }

                /*
         * Could use socket type SOCK_RAW or SOCK_DGRAM?
         * htons(ETH_P_AX25)
         */
        if ( (tx_sock = socket(AF_AX25, SOCK_DGRAM, 0)) == -1) {
                perror("transmit AX.25 socket");
                return(-1);
        }

        /* Bind the tx socket to the source address */
        if (bind(tx_sock, (struct sockaddr *)&sockaddr_ax25_src, tx_src_addrlen) == -1) {
                perror("bind");
                return 1;
        }

        state->ax25_tx_sock = tx_sock;

        pr_debug("%s(): Connected to AX.25 stack on rx socket %d, tx socket %d\n",
                 __FUNCTION__, rx_sock, tx_sock);
        }
#endif /* CONSOLE_SPY */
        fflush(stdout);

        return(rx_sock);
}

#ifndef CONSOLE_SPY

bool send_ax25_beacon(struct state *state, char *packet)
{
        char buf[512];
        char destcall[8];
        char destpath[32];
        char *pkt_start;
        int ret;
        unsigned int len = sizeof(buf);
        unsigned int tx_dest_addrlen;   /* Length of APRS path */
        char *aprspath = state->conf.aprs_path;
        struct full_sockaddr_ax25 sockaddr_ax25_dest;

        printf("%s: \n", __FUNCTION__);

        buf[0]='\0';
        strcpy(buf, packet);
        len=strlen(buf);

        if(strlen(aprspath) == 0 ) {
                fprintf(stderr, "%s:%s() no APRS path configured\n", __FILE__, __FUNCTION__);
                /* Fix this */
                return false;
        }
        /* set the aprs path */
        aprspath = state->conf.aprs_path;
        pkt_start = buf;

        /*
         * Mic-E is different as its data can be stored in the AX.25
         * destination address field
         *
         * The Mic-E data is passed to this routine as part of the
         * packet and here some of the Mic-E data is stripped out & put
         * in the destination address field.
         * Note: Currently only using 6 of the 7 available AX.25 destination
         * address field bytes.
         * */
        if(packet[0] == APRS_DATATYPE_CURRENT_MIC_E_DATA) {
                strncpy(destcall, &packet[1], MIC_E_DEST_ADDR_LEN - 1);
                destcall[MIC_E_DEST_ADDR_LEN - 1]='\0';
                sprintf(destpath, "%s via %s", destcall, state->conf.digi_path);
                aprspath = destpath;
                buf[MIC_E_DEST_ADDR_LEN-1] = APRS_DATATYPE_CURRENT_MIC_E_DATA;
                pkt_start = &buf[MIC_E_DEST_ADDR_LEN - 1];

                printf("MIC-E TEST: total pkt len: %d, pkt data len: %d, pkt data: %s\n",
                       len, len - (MIC_E_DEST_ADDR_LEN - 1), pkt_start);

                len -= (MIC_E_DEST_ADDR_LEN - 1);
        }
        memset((char *)&sockaddr_ax25_dest, 0, sizeof(sockaddr_ax25_dest));

        if ((tx_dest_addrlen = ax25_aton(aprspath, &sockaddr_ax25_dest)) == -1) {
                perror("ax25_config_get_addr dest");
                return false;
        }
#ifdef DEBUG_1
        {
                int i;
                unsigned char *pBuf =(unsigned char *) &sockaddr_ax25_dest;

                printf("send_ax25_beacon: tx addr len %d\n", tx_dest_addrlen);
                for (i = 0; i < sizeof(sockaddr_ax25_dest); i++) {
                        printf("0x%02x ", *(pBuf+i));
                }
                printf("\n");

        }
#endif /* DEBUG */

        if ((ret=sendto(state->ax25_tx_sock, pkt_start, len, 0, (struct sockaddr *)&sockaddr_ax25_dest, tx_dest_addrlen)) == -1) {
                pr_debug("%s:%s(): after sendto sock msg len= %d, ret= 0x%02x\n",  __FILE__, __FUNCTION__, len, ret);
                perror("sendto");
                return false;
        }

        return (ret == len);
}
#endif /* CONSOLE_SPY */


#if 0 /* =============== */
void handle_ax25_pkt(struct state *state, unsigned char *rxbuf, int size)
{
        char packet[512];
        unsigned int len = sizeof(packet);
        struct sockaddr sa;

        strcpy(sa.sa_data, "File");

        if (packet_qualify(state, &sa, rxbuf, size)) {

                memset(packet, 0, len);

                if(fap_ax25_to_tnc2((char *)rxbuf, size, packet, &len)) {
                        printf("[%d] - %s\n", size, packet);
                        parse_incoming_packet(state, packet, len, 1);

                } else {
                        printf("************************\n");
                        fap_conversion_debug((char *)rxbuf, packet, len);
                }
        }

        return;
}

#else /* ========================== */

void handle_ax25_pkt(struct state *state, struct sockaddr *sa, unsigned char *buf, int size)
{
        struct t_ax25packet ax25packet;
        struct timezone tmzone;
        struct ifreq ifr;
        char portname[16];

        if (packet_qualify(state, sa, buf, size)) {

        /* Fix me: first byte in the receive buffer is KISS command
         * From AX25 first byte specifies:
         * 0 = packet
         * non 0 = parameters
         */
                memset( &ax25packet, 0, sizeof(ax25packet) );
                time(&ax25packet.time);
                gettimeofday(&ax25packet.timeval, &tmzone);

#ifdef  USE_SOCK_PACKET
                if ((ax25packet.port = ax25_config_get_name(sa->sa_data)) == NULL)
                        ax25packet.port = sa->sa_data;
#else
                struct  sockaddr_ll *sll = (struct sockaddr_ll *)sa;
                memset((char *)&ifr, 0, sizeof(ifr));
                ifr.ifr_ifindex = sll->sll_ifindex;

                if(ioctl(state->tncfd, SIOCGIFNAME, &ifr) >= 0) {
                        strncpy(portname,ifr.ifr_name, sizeof(ifr.ifr_name));
                        ax25packet.port = portname;
                }

#endif
#ifndef MAIN
                if(state->debug.display_fap_enable) {

                        /* begin fap dump */
                        char packet[512];
                        unsigned int len = sizeof(packet);

                        if(state->debug.verbose_level > 3) {

                                printf("%s %s: ",
                                       time2str(&ax25packet.time, 0),
                                       ax25packet.port);
                        } else {
                                printf(".");
                        }
                        fflush(stdout);

                        memset(packet, 0, len);

                        if(fap_ax25_to_tnc2((char *)buf, size, packet, &len)) {
                                if(state->debug.verbose_level > 3) {
                                        printf("[%d] - %s\n", size, packet);
                                }
                                parse_incoming_packet(state, packet, len, 1);
                        } else {
                                printf("************************\n");
                                fap_conversion_debug((char *)buf, packet, len);
                                printf("========================\n");
                                ax25_decode( &ax25packet, buf, size );
                                ax25packet_dump( &ax25packet, READABLE, state->conf.spy_format);
                                printf("========================\n");
                        }
                }  /* end fap dump */
#endif /* not MAIN */
                if(state->debug.display_spy_enable) {
                        /* Begin spy dump */
                        char  *dumpbuf = NULL;
#ifdef B2F_DECODE
                        struct buffer *decompbuf = NULL;
#endif /* B2F_DECODE */
                        ax25_decode( &ax25packet, buf, size );
                        dumpbuf = ax25packet_dump( &ax25packet, READABLE, state->conf.spy_format);

#ifdef B2F_DECODE
                        if(state->debug.b2f_decode_enable && ax25packet.datalength > 0) {
                                decompbuf = b2f_decode(state, dumpbuf, (uint8_t *)ax25packet.pdata, ax25packet.datalength);
                        }
#endif /* B2F_DECODE */
#ifdef WEBAPP
                        if (bspysock_enable) {
                                json_object *spy_object;
                                char  *strpktcount = NULL;
                                static unsigned long pktcount = 0;

                                pktcount++;
                                asprintf( &strpktcount, "%lu", pktcount);

                                spy_object = json_object_new_object();
                                json_object_object_add(spy_object, "spy", json_object_new_string(dumpbuf));
                                json_object_object_add(spy_object, "count", json_object_new_string(strpktcount));
#ifdef B2F_DECODE
                                if (decompbuf != NULL) {

                                        send_ui_sock(state, (char *)json_object_to_json_string(spy_object));
                                        json_object_put(spy_object);

                                        spy_object = json_object_new_object();
                                        json_object_object_add(spy_object, "spy", json_object_new_string((char *)decompbuf->data));
                                        json_object_object_add(spy_object, "count", json_object_new_string(strpktcount));

                                        buffer_free(decompbuf);
                                        state->prop_ctrl.decomp = NULL;
                                }

#endif /* B2F_DECODE */
#ifdef TEST_TCPSTREAM
                                /* concatenate received packets */
                                {
                                        char *lastpkt, *currpkt;
                                        int index_pktarray=0;

                                        if(index_pktarray == 0) {
                                                lastpkt = (char *)json_object_to_json_string(spy_object);
                                                index_pktarray++;
                                        } else {
                                                char * thispkt = (char *)json_object_to_json_string(spy_object);
                                                index_pktarray = 0;
                                                currpkt=malloc(strlen(lastpkt) + strlen(thispkt) + 1);
                                                strcpy(currpkt, lastpkt);
                                                strcat(currpkt, thispkt);
                                                send_ui_sock(state, currpkt);

                                                json_object_put(spy_object);
                                        }
                                }
#else /* NOT TEST_TCPSTREAM */
                                send_ui_sock(state, (char *)json_object_to_json_string(spy_object));
                                json_object_put(spy_object);

#endif  /* TEST_TCPSTREAM */

                                free(strpktcount);
                        }
#endif /*WEBAPP */
                        if (state->debug.console_spy_enable) {
#ifdef B2F_DECODE
                                if(decompbuf != NULL) {
                                        printf("%s", decompbuf->data);
                                }
#endif /* B2F_DECODE */
                                if(dumpbuf == NULL) {
                                        ax25_decode( &ax25packet, buf, size );
                                        dumpbuf = ax25packet_dump( &ax25packet, READABLE, state->conf.spy_format);
                                }
                                printf("%s %s: ",
                                       time2str(&ax25packet.time, 0),
                                       ax25packet.port);
                                printf("%s", dumpbuf);
                                fflush(stdout);
                        }
                        if(dumpbuf) {
                                free(dumpbuf);
                        }
                } /* end spy dump */
        }
}
#endif /* ========================== */

#ifdef MAIN

#include "util.h"
#include "aprs.h"

#ifndef CONSOLE_SPY
void ui_sock_cfg(struct state *state)
{
        /* If a hostname or ip address was supplied using the -d command
         * line argument then use AF_INET socket family.  Otherwise use
         * the AF_UNIX family with socket path parsed from ini file.
         */
        if(state->conf.ui_host_name != NULL) {
                printf("%s: Using protocol AF_INET, host name %s\n", __FUNCTION__, state->conf.ui_host_name);
                if(lookup_host(state)) {
                        printf("Failure connecting to display device\n");
                        exit(1);
                }

        } else {
                printf("%s: protocol AF_UNIX\n", __FUNCTION__);
                state->conf.display_to.afinet.sa_family = AF_UNIX;
                strcpy((&state->conf.display_to.afunix)->sun_path,
                       state->conf.ui_sock_path);
        }
}

void ui_unix_sock_wait(struct state *state)
{
        struct stat sts;
        int retcode;

        /* get rid of the Unix socket */
        unlink(state->conf.ui_sock_path);
        /* verify Unix socket removal was a success */
        retcode = stat(state->conf.ui_sock_path, &sts);
        if(retcode != -1 || errno != ENOENT) {
                printf("Problem deleting Unix socket %s, stat returned an errno of 0x%02x %s\n",
                       state->conf.ui_sock_path, errno, strerror(errno));
        }

        /* Wait for the socket to be created by the node script */
        printf("Waiting for node script to create a Unix socket\n");
        while (stat(state->conf.ui_sock_path, &sts) == -1 && errno == ENOENT) {
                sleep(1);
        }
        sleep(2);
}

void ui_net_sock_avail(struct state *state, int sockfd)
{
        struct sockaddr_in portchk;
        int on;

        printf("size check sockaddr %zu, sockaddr_in %zu\n",
        sizeof(struct sockaddr), sizeof(struct sockaddr_in));

        memcpy(&portchk, &state->conf.display_to.afinet, sizeof(portchk));

        portchk.sin_addr.s_addr = 0;

        {
                int i;
                uint8_t *buf = (uint8_t *)&portchk;

                printf("binding to address: ");
                for (i = 0; i < sizeof(portchk); i++) {
                        printf(" %02x", *(buf+i));
                }

                printf("\n");
        }

        /* Enable address reuse */
        on = 1;
        if(setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) < 0) {
                fprintf(stderr, "%s: Error setting socket option: %s\n",
                          __FUNCTION__,  strerror(errno) );
                return;
        }

        if (bind(sockfd, (struct sockaddr *) &portchk, sizeof(portchk)) < 0) {

                if( errno == EADDRINUSE ) {
                        printf("Port %d is not available.\n", ntohs(portchk.sin_port));
                } else {
                        printf("could not bind to process (%d) %s\n", errno, strerror(errno));
                }
        } else {
                printf("Socket %d is available\n", state->conf.ui_inet_port);
        }
#if 0
        if (close (sockfd) < 0 ) {
                printf("did not close fd: %s\n", strerror(errno));
        }
#endif
}

int ui_connect(struct state *state)
{
        int sock;
        char buf[32];

        struct sockaddr *dest =  &state->conf.display_to.afinet;
        unsigned int dest_len = sizeof(struct sockaddr);

        ui_sock_cfg(state);
        printf("%s: ui_sock_cfg\n", __FUNCTION__);

        if(state->conf.display_to.afinet.sa_family == AF_UNIX) {
                /* clean-up previous socket */
                ui_unix_sock_wait(state);
                dest_len = sizeof(struct sockaddr_un);
        }

        sock = socket(dest->sa_family, SOCK_STREAM, 0);
        if (sock < 0) {
                perror("socket");
                return -errno;
        }

        if(state->conf.display_to.afinet.sa_family == AF_INET) {

                /* Check if socket is already in use! */
                ui_net_sock_avail(state, sock);
        }

        if (connect(sock, dest, dest_len)) {
                fprintf(stderr, "%s: Failed to connect to UI socket %d: %s on \n",
                          __FUNCTION__, sock, strerror(errno));
                close(sock);
                return -errno;
        } else {
                sprintf(buf, "%d", state->conf.ui_inet_port);
                printf("UI Socket %s, connected with sock %d\n",
                       state->conf.display_to.afinet.sa_family == AF_UNIX ?
                       state->conf.ui_sock_path : buf,
                       sock);
        }

        return sock;
}
#endif /* NOT CONSOLE_SPY */

void handle_kiss_param(int type, int val) {

        printf("KISS Command: ");
        switch (type) {
                case PARAM_TXDELAY:
                        printf("TX Delay: %lu ms\n", val * 10L);
                        break;
                case PARAM_PERSIST:
                        printf("Persistence: %u/256\n", val + 1);
                        break;
                case PARAM_SLOTTIME:
                        printf("Slot time: %lu ms\n", val * 10L);
                        break;
                case PARAM_TXTAIL:
                        printf("TX Tail time: %lu ms\n", val * 10L);
                        break;
                case PARAM_FULLDUP:
                        printf("Duplex: %s\n",
                                  val == 0 ? "Half" : "Full");
                        break;
                case PARAM_HW:
                        printf("Hardware %u\n", val);
                        break;
                case PARAM_RETURN:
                        printf("RETURN\n");
                        break;
                default:
                        printf("Unhandled kiss command: %u arg %u\n", type, val);
                        break;
        }
}

/*
 * For Demo or debug replay packets previously saved to a file
 */
int canned_packets(struct state *state)
{
        unsigned char inbuf[1500];
        int pkt_size;
        int bytes_read = 1;
        int cnt_pkts = 0;
        int cnt_bytes = 0;
        recpkt_t recpkt;
        time_t last_time, curr_time, wait_time;
        struct tm *tm;
        struct sockaddr sa;
        FILE *fsrecpkt;
        bool bswapon = false;
#ifndef CONSOLE_SPY
        int *uifd = &state->dspfd;
#endif /*  CONSOLE_SPY */

        /* open play back file */
        fsrecpkt = fopen(state->debug.playback_pkt_filename, "rb");
        if (fsrecpkt == NULL) {
                perror(state->debug.playback_pkt_filename);
                return errno;
        }
#ifndef CONSOLE_SPY
        if(*uifd <= 0) {
                *uifd=ui_connect(state);
        }
#endif /*  CONSOLE_SPY */

        printf("Playing back packets from file %s\n", state->debug.playback_pkt_filename);

        /* read the starting time of the captured packets */
        if(fread(&recpkt, sizeof(recpkt_t), 1, fsrecpkt) != 1) {
                fprintf(stderr, "Problem reading first bytes of playback file\n");
                return 1;
        }
        rewind(fsrecpkt);

        /* Initialize  reference time */
        last_time = (time_t)recpkt.currtime;

        /* get broken down time from first recorded packet structure */
        tm = localtime(&last_time);
        /* qualify time */
        printf("Size: %d, First time read: %ld, year: %d, %s, asctime: %s\n",
               recpkt.size,
               last_time,
               tm->tm_year + 1900,
               time2str(&last_time, 0),
               asctime(tm)
              );
        if(tm->tm_year > (3000-1900) ||recpkt.size > 100000) {
                printf("Detected wrong endianess or corrupt file\n");
                last_time = __bswap_32(last_time);
                tm = localtime(&last_time);
                printf("CHECK: time-t: %u, Size: %d, First time read: %ld, year: %d, %s, asctime: %s\n",
                       (unsigned int)sizeof(time_t),
                       __bswap_16(recpkt.size),
                       (time_t)__bswap_32(last_time),
                       tm->tm_year + 1900,
                       time2str(&last_time, 0),
                       asctime(tm)
                      );
                bswapon = true;
        }
        /* Initialize  reference time */
        last_time = (time_t) (bswapon ? __bswap_32(recpkt.currtime) : recpkt.currtime);

        strcpy(sa.sa_data, "File");

        while(bytes_read > 0) {
                if ((bytes_read = fread(&recpkt, sizeof(recpkt_t), 1, fsrecpkt)) != 1) {
                        if(bytes_read == 0) {
                                printf("\nFinished reading all %d packets, %zu bytes\n\n",
                                       cnt_pkts, cnt_bytes + cnt_pkts*sizeof(recpkt_t));
                                /* continuously display packets, so rewind & continue */
                                rewind(fsrecpkt);
                                if ((bytes_read = fread(&recpkt, sizeof(recpkt_t), 1, fsrecpkt)) != 1) {
                                        printf("Failed to rewind file, exiting\n");
                                        break;
                                }
                        } else {
                        fprintf(stderr, "%s: Error reading file: 0x%02x %s %s\n",
                                  __FUNCTION__, errno, strerror(errno), state->debug.playback_pkt_filename);
                        return errno;
                        }
                }

                pkt_size = bswapon ? __bswap_16(recpkt.size) : recpkt.size;
                curr_time =  bswapon ? __bswap_32(recpkt.currtime) : recpkt.currtime;

                bytes_read = fread(inbuf, 1, pkt_size, fsrecpkt);
                if(bytes_read != pkt_size) {
                        printf("Read error expected %d, read %d\n",
                               pkt_size, bytes_read);
                        break;
                }

                cnt_pkts++;
                cnt_bytes += bytes_read;

                /* pace the packets */
                wait_time = curr_time - last_time;

                if(state->debug.verbose_level > 3) {
                        printf("Pkt count %d, size %d, time %s ... ",
                               cnt_pkts, pkt_size, time2str((time_t *)&curr_time, 0));

                        printf("Wait %lu secs", wait_time);
                }
                /* Pace packets quicker than reality */
                if(wait_time > 0)
                        wait_time = wait_time / state->debug.playback_time_scale;

                if(state->debug.verbose_level > 3) {
                        printf(", now %lu secs\n", wait_time);
                        fflush(stdout);
                }

                if(wait_time < 0 || wait_time > 5*60) {
                        printf("Wait time seems unreasonable %lu\n", wait_time);
                        wait_time = 60;
                }
                last_time = curr_time;

                sleep(wait_time);

#ifndef CONSOLE_SPY
                handle_ax25_pkt(state, &sa, &inbuf[1], bytes_read-1);
#endif
        }
        return 0;
}

int live_packets(struct state *state)
{
        unsigned char inbuf[1500];
        struct sockaddr_storage sa;
        socklen_t sa_len = sizeof sa;
        recpkt_t recpkt;
        FILE *fsrecpkt;
        char *fname;
        int size = 0;
#ifndef CONSOLE_SPY
        int *fd = &state->dspfd;
#else
        state->conf.ax25_port = NULL;
#endif /*  CONSOLE_SPY */

        /* Live packet handling */
        state->tncfd = aprsax25_connect(state);
        if (state->tncfd < 0) {
                return -1;
        }

#ifndef CONSOLE_SPY
        if(*fd <= 0) {
                *fd = ui_connect(state);
        }
#endif /*  CONSOLE_SPY */

        /*
         * Check if recording packets is enabled
         *  - grab a file descriptor
         */
        if(state->debug.record_pkt_filename != NULL) {
                time_t currtime;
                currtime = time(NULL);

                asprintf(&fname, "%s_%s.log",
                         state->debug.record_pkt_filename,
                         time2str(&currtime, 1));

                /* open a record file */
                fsrecpkt = fopen(fname, "wb");
                if (fsrecpkt == NULL) {
                        perror(fname);
                        return errno;
                }
                printf("Recording packets to file %s\n", fname);
        }

        printf("%s: where's the packets\n", __FUNCTION__);

        while ((size = recvfrom(state->tncfd, inbuf, sizeof(inbuf), 0, (struct sockaddr *)&sa, &sa_len))) {

                /* is recording packets enabled ? */
                if(state->debug.record_pkt_filename != NULL) {
                        recpkt.currtime = time(NULL);
                        recpkt.size = size;
                        fwrite(&recpkt, sizeof(recpkt_t), 1, fsrecpkt);

                        if( fwrite(inbuf, size, 1, fsrecpkt) != 1) {
                                fflush(fsrecpkt);
                                fclose(fsrecpkt);
                                printf("Error %s writing to %s",
                                       strerror(errno), fname);
                                return errno;
                        }
                        fflush(fsrecpkt);
                }


                if (size >= 8) {
                        size--;
                        handle_ax25_pkt(state, (struct sockaddr *)&sa, &inbuf[1], size);
                } else if (size > 0) {
                        handle_kiss_param(inbuf[0] & 0x0f, inbuf[1]);
                } else {
                        perror("AX25 Socket recvfrom error");
                        printf("%s:%s(): socket %d\n", __FILE__, __FUNCTION__, state->tncfd);
                        break;
                }
        }
        return 1;
}

int main(int argc, char **argv)
{
        char *program_name;
        struct state state;

        if ( (program_name=strrchr(argv[0], '/'))!=NULL) {  /* Get root program name */
                program_name++;
        } else {
                program_name = argv[0];
        }

        memset(&state, 0, sizeof(state));
#ifndef CONSOLE_SPY
        fap_init();

        if (parse_opts(argc, argv, &state)) {
                printf("Invalid option(s)\n");
                exit(1);
        }

        if (parse_ini(state.conf.config ? state.conf.config : "aprs.ini", &state)) {
                printf("Invalid config\n");
                exit(1);
        }
# else
        printf("%s v%d.%02d(%d)\n", program_name,
               TRACKER_MAJOR_VERSION,
               TRACKER_MINOR_VERSION,
               BUILD);
#endif /* NOT CONSOLE_SPY */
        /* Force spy display on */
        state.debug.display_spy_enable = true;
        state.debug.console_spy_enable = true;
        /* disable b2f packet decode */
        state.debug.b2f_decode_enable = false;

        /*
         * Check if playback packets is enabled
         */
        if(state.debug.playback_pkt_filename != NULL) {
                canned_packets(&state);
        } else {
                live_packets(&state);
        }
        return 0;
}

#endif /* #ifdef MAIN */
#endif /* #ifdef HAVE_AX25_TRUE */
