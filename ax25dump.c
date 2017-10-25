/* ax25dump.c
 * AX25 header tracing
 * Copyright 1991 Phil Karn, KA9Q
 * Extended for listen() by ???
 * 07.02.99 Changed for monixd by dg9ep
 *
 * $Id: ax25dump.c $
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>

#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/param.h> /* for max & min macros */

#define MAIN
#include "aprs.h"
#include "monixd.h"
#include "util.h"

#define	NRDESTPERPACK	11
#define	NR3NODESIG	0xFF

#define LAPB_UNKNOWN	0
#define LAPB_COMMAND	1
#define LAPB_RESPONSE	2

#define SEG_FIRST	0x80
#define SEG_REM 	0x7F
#define SIZE_DISPLAY_BUF (8*1024)

/* unsigned char typeconv[14] =
 * { S, RR, RNR, REJ, U,
 *  SABM, SABME, DISC, DM, UA,
 *  FRMR, UI, PF, EPF
 * };
 */


#define MMASK		7

#define HDLCAEB 	0x01
#define SSID		0x1E
#define REPEATED	0x80
#define C		0x80
#define SSSID_SPARE	0x40
#define ESSID_SPARE	0x20

#define ALEN		6
#define AXLEN		7

#define W		1
#define X		2
#define Y		4
#define Z		8

#ifdef B2F_DECODE
#include "buffer.h"

/* lzh lempel-Ziv Huffman decompressor (b2f/winlink decompression) */
struct buffer *  version_1_Decode(struct buffer *inbuf);
#endif /* B2F_DECODE */

static int  ftype(unsigned char *, int *, int *, int *, int *, int);
static char *decode_type(int);
int mem2ax25call(struct t_ax25call*, unsigned char*);
void data_dump(char *, unsigned char *data, int length, int dumpstyle);
void ai_dump(char *,unsigned char *data, int length);
char *compression_check(struct state *state, char *dumpbuf);

#define NDAMA_STRING ""
#define DAMA_STRING "[DAMA] "
int current_size_display_buf = 0;

/*----------------------------------------------------------------------*/
char* ax25call2bufEnd(struct t_ax25call *pax25call, char *buf)
/* same as ax25call2str(), but caller have to provide own buffer
 * returns pointer to END of used buffer */
{
	if( pax25call->ssid == 0)
		return( buf+sprintf(buf,"%s",   pax25call->sCall                 ) );
	else
		return( buf+sprintf(buf,"%s-%d",pax25call->sCall, pax25call->ssid) );
}

char* ax25call2str(struct t_ax25call *pax25call)
{
	static char res[9+1]; /* fixme */
	ax25call2bufEnd(pax25call,res);
	return res;
}

char* pid2str(int pid)
{
	static char str[20];
	switch( pid ) {
		case PID_NO_L3:    return "Text";
		case PID_VJIP06:   return "IP-VJ.6";
		case PID_VJIP07:   return "IP-VJ.7";
		case PID_SEGMENT:  return "Segment";
		case PID_IP:	   return "IP";
		case PID_ARP:	   return "ARP";
		case PID_NETROM:   return "NET/ROM";
		case PID_X25:	   return "X.25";
		case PID_TEXNET:   return "Texnet";
		case PID_FLEXNET:  return "Flexnet";
		case PID_PSATFT:   return "PacSat FT";
		case PID_PSATPB:   return "PacSat PB";
		case -1: /*no PID*/return "";
		default:	   sprintf(str,"pid=0x%x",pid);
				   return str;
	}
}

void mprintf(char *accumStr, int dtype, char *fmt, ...)
{
	va_list args;
	char *pStr;
	int ret;
	size_t required_size_display_buf;

	va_start(args, fmt);
	ret = vasprintf(&pStr, fmt, args);
	va_end(args);

	/* Check for sufficient memory from vasprintf call */
	if(ret != -1) {

		required_size_display_buf = strlen(pStr) + strlen(accumStr);
		if( required_size_display_buf > current_size_display_buf) {
			printf("%s: Display buffer too small %d, need %zu\n",
			       __FUNCTION__, current_size_display_buf, required_size_display_buf);
			accumStr = realloc(accumStr, required_size_display_buf + 8);
			if (accumStr == NULL) {
				printf("%s: malloc error: %s\n",
				       __FUNCTION__, strerror(errno));
				exit (ENOMEM);
			}
		}
		strcat(accumStr, pStr);
		free(pStr);
	}
}
#if 0 /* for reference only, should never be called */
char * lprintf(int dtype, char *fmt, ...)
{
	va_list args;
	char str[1024];

	va_start(args, fmt);
	vsnprintf(str, 1024, fmt, args);
	va_end(args);

	printf("%s", str);
	return(str);
}
#endif /* DEBUG */

/* FlexNet header compression display by Thomas Sailer sailer@ife.ee.ethz.ch */
/* Dump an AX.25 packet header */
struct t_ax25packet *
   ax25_decode( struct t_ax25packet *ax25packet , unsigned char *data, int length)
{
	int ctlen;
	int eoa;

	ax25packet->pid = -1;

	ax25packet->totlength = length;

	/* check for FlexNet compressed header first; FlexNet header
	 * compressed packets are at least 8 bytes long  */
	if (length < 8) {
		/* Something wrong with the header */
		return ax25packet;
	}
	if (data[1] & HDLCAEB) {
		/* char tmp[15]; */
		/* this is a FlexNet compressed header	$TODO */
		/*		 tmp[6] = tmp[7] = ax25packet->fExtseq = 0;
		 *		 tmp[0] = ' ' + (data[2] >> 2);
		 *		 tmp[1] = ' ' + ((data[2] << 4) & 0x30) + (data[3] >> 4);
		 *		 tmp[2] = ' ' + ((data[3] << 2) & 0x3c) + (data[4] >> 6);
		 *		 tmp[3] = ' ' + (data[4] & 0x3f);
		 *		 tmp[4] = ' ' + (data[5] >> 2);
		 *		 tmp[5] = ' ' + ((data[5] << 4) & 0x30) + (data[6] >> 4);
		 *		 if (data[6] & 0xf)
		 *			 sprintf(tmp+7, "-%d", data[6] & 0xf);
		 *		 lprintf(T_ADDR, "%d->%s%s",
		 *			 (data[0] << 6) | ((data[1] >> 2) & 0x3f),
		 *			 strtok(tmp, " "), tmp+7);
		 *		 ax25packet->cmdrsp = (data[1] & 2) ? LAPB_COMMAND : LAPB_RESPONSE;
		 *		 dama = NDAMA_STRING;
		 *		 data	+= 7;
		 *		 length -= 7;
		 */
		return ax25packet; /* $TODO */
	} else {
		/* Extract the address header */
		if (length < (AXLEN + AXLEN + 1)) {
			/* length less then fmcall+tocall+ctlbyte */
			/* Something wrong with the header */
			return ax25packet;
		}

		ax25packet->fExtseq = (
				       (data[AXLEN + ALEN] & SSSID_SPARE) != SSSID_SPARE
				      );

		ax25packet->fDama = ((data[AXLEN + ALEN] & ESSID_SPARE) != ESSID_SPARE);

		mem2ax25call( &ax25packet->tocall, data 	);
		mem2ax25call( &ax25packet->fmcall, data + AXLEN );

		ax25packet->cmdrsp = LAPB_UNKNOWN;
		if ((data[ALEN] & C) && !(data[AXLEN + ALEN] & C))
			ax25packet->cmdrsp = LAPB_COMMAND;

		if ((data[AXLEN + ALEN] & C) && !(data[ALEN] & C))
			ax25packet->cmdrsp = LAPB_RESPONSE;

		eoa = (data[AXLEN + ALEN] & HDLCAEB);

		data   += (AXLEN + AXLEN);
		length -= (AXLEN + AXLEN);

		ax25packet->ndigi = 0;
		ax25packet->ndigirepeated = -1;

		while (!eoa && (ax25packet->ndigi < MAX_AX25_DIGIS + 1) && (length > 0) ) {
			mem2ax25call( &ax25packet->digi[ax25packet->ndigi], data );
			if( data[ALEN] & REPEATED )
				ax25packet->ndigirepeated = ax25packet->ndigi;

			eoa = (data[ALEN] & HDLCAEB);
			ax25packet->ndigi++;
			data   += AXLEN;
			length -= AXLEN;
		}
	}

	ax25packet->fFmCallIsLower = (
				      memcmp( &ax25packet->fmcall,
					      &ax25packet->tocall, sizeof(struct t_ax25call)
					    ) < 0
				     );

	ax25packet->pdata = (char *)data;
	ax25packet->datalength = length;

	if (length == 0) {
		ax25packet->valid = 1;
		return ax25packet;
	}

	ctlen = ftype(data, &ax25packet->frametype, &ax25packet->ns, &ax25packet->nr, &ax25packet->pf, ax25packet->fExtseq);

	data   += ctlen;
	length -= ctlen;

	ax25packet->pdata = (char *)data;
	ax25packet->datalength = length;

	if ( ax25packet->frametype == I || ax25packet->frametype == UI ) {
		/* Decode I field */
		if ( length > 0 ) {	   /* Get pid */
			ax25packet->pid = *data++;
			length--;

			/*  $TODO		if (ax25packet->pid == PID_SEGMENT) {
			 *				 seg = *data++;
			 *				 length--;
			 *				 lprintf(T_AXHDR, "%s remain %u", seg & SEG_FIRST ? " First seg;" : "", seg & SEG_REM);
			 *
			 *				 if (seg & SEG_FIRST) {
			 *					 ax25packet->pid = *data++;
			 *					 length--;
			 *				 }
			 *			 }
			 */
			ax25packet->pdata = (char *)data;
			ax25packet->datalength = length;
			ax25packet->datacrc = calc_abincrc((char *)data, length, 0x5aa5 );
		}
	} else if (ax25packet->frametype == FRMR && length >= 3) {
		/* FIX ME XXX
		lprintf(T_AXHDR, ": %s", decode_type(ftype(data[0])));
*/
		/*		  lprintf(T_AXHDR, ": %02X", data[0]);
		 *  $TODO	 lprintf(T_AXHDR, " Vr = %d Vs = %d",
		 *			 (data[1] >> 5) & MMASK,
		 *			 (data[1] >> 1) & MMASK);
		 *		 if(data[2] & W)
		 *			 lprintf(T_ERROR, " Invalid control field");
		 *		 if(data[2] & X)
		 *			 lprintf(T_ERROR, " Illegal I-field");
		 *		 if(data[2] & Y)
		 *			 lprintf(T_ERROR, " Too-long I-field");
		 *		 if(data[2] & Z)
		 *			 lprintf(T_ERROR, " Invalid seq number");
		 *		 lprintf(T_AXHDR,"%s\n", dama);
		 */
	} else if ((ax25packet->frametype == SABM || ax25packet->frametype == UA) && length >= 2) {
		/* FlexNet transmits the QSO "handle" for header
		 * compression in SABM and UA frame data fields
		 */
		/*		  lprintf(T_AXHDR," [%d]%s\n", (data[0] << 8) | data[1], dama);
		 */
	}
	/* ok, all fine */
	ax25packet->valid = 1;
	return ax25packet;
}

char * ax25packet_dump(struct t_ax25packet *pax25packet, int dumpstyle, int displayformat)
{
	char *dama;
	char *dumpbuf;

	dumpbuf = malloc(SIZE_DISPLAY_BUF);
	if (dumpbuf == NULL) {
		printf("%s: malloc error: %s\n",
		       __FUNCTION__, strerror(errno));
		exit (ENOMEM);
	}
	current_size_display_buf = SIZE_DISPLAY_BUF;
	dumpbuf[0]='\0';

	/* First field displayed is a time stamp */
	if(displayformat & SPY_FORMAT_TIME_ENABLE) {
		mprintf(dumpbuf, T_TIMESTAMP, "%s ",
			  mtime2str(&pax25packet->timeval,
				    displayformat & SPY_FORMAT_TIME_RESOLUTION ? true : false) );
	}


	/* if( pax25packet->pQSO )
	 *   lprintf(T_ADDR,  "(%d) ",  pax25packet->pQSO->qsoid);
	 */

	/* Second field displayed is port name */
	if(displayformat & SPY_FORMAT_PORT_ENABLE) {
		mprintf(dumpbuf, T_PORT, "%s ", pax25packet->port);
	}

	/* Nobody will need this:
	 *   if( pax25packet->fcrc )	   lprintf(T_PORT, "CRC-");
	 *   if( pax25packet->fflexcrc )   lprintf(T_PORT, "flex:");
	 *   if( pax25packet->fsmack )	   lprintf(T_PORT, "smack:");
	 */
	/* ------------------------------------- */

	if( !pax25packet->valid ) {
		/* Something wrong with the header */
		mprintf(dumpbuf, T_ERROR, "AX25: bad header! data length: %d\n", pax25packet->datalength);

		data_dump(dumpbuf, (unsigned char *)pax25packet->pdata, pax25packet->datalength, HEX);
		return dumpbuf;
	}
	/* ONLY looking at AX25 packets so display length instead */
#if 0
	if (pax25packet->fExtseq) {
		lprintf(T_PROTOCOL, "EAX25: ");
	} else {
		lprintf(T_PROTOCOL, "AX25: ");
	}
#else
	/* Display packet length */
	if(displayformat & SPY_FORMAT_PKTLEN_ENABLE) {
		mprintf(dumpbuf, T_PROTOCOL, "%d - ", pax25packet->totlength);
	}
#endif

	if(pax25packet->fDama) {
		dama = DAMA_STRING;
	} else {
		dama = NDAMA_STRING;
	}

	/* Start displaying actual packet */
	mprintf(dumpbuf, T_FMCALL, "%s", ax25call2str(&pax25packet->fmcall));
	mprintf(dumpbuf, T_ZIERRAT, "->");
	mprintf(dumpbuf, T_TOCALL, "%s", ax25call2str(&pax25packet->tocall));

	if( pax25packet->ndigi > 0 ) {
		int i;

		mprintf(dumpbuf, T_AXHDR, " v");
		for(i=0;i<pax25packet->ndigi;i++) {
			mprintf(dumpbuf, T_VIACALL," %s%s", ax25call2str(&pax25packet->digi[i]),
				  pax25packet->ndigirepeated == i ? "*" : "");
		}
	}
	/* Warning: using less than character, an HTML reserved
	 * character */
	mprintf(dumpbuf, T_AXHDR, " <%s", decode_type(pax25packet->frametype));

	switch (pax25packet->cmdrsp) {
		case LAPB_COMMAND:
			mprintf(dumpbuf, T_AXHDR, " C");
			if( pax25packet->pf ) {
				mprintf(dumpbuf, T_AXHDR, "+");
			} else {
				mprintf(dumpbuf, T_AXHDR, "^");
			}
			break;
		case LAPB_RESPONSE:
			mprintf(dumpbuf, T_AXHDR, " R");
			if( pax25packet->pf ) {
				mprintf(dumpbuf, T_AXHDR, "-");
			} else {
				mprintf(dumpbuf, T_AXHDR, "v");
			}
			break;
		default:
			break;
	}

	if (pax25packet->ns > -1 )
		mprintf(dumpbuf, T_AXHDR, " S%d", pax25packet->ns);
	if (pax25packet->nr > -1 )
		mprintf(dumpbuf, T_AXHDR, " R%d", pax25packet->nr);
	/* Warning: using greater than character, an HTML reserved
	 * character */
	mprintf(dumpbuf, T_AXHDR, ">");

	if (pax25packet->frametype == I || pax25packet->frametype == UI) {
		/* Decode I field */
		/* Took out \n after %s%s for a one line display */
		mprintf(dumpbuf, T_PROTOCOL," %s%s:",dama, pid2str(pax25packet->pid));

		switch (pax25packet->pid) {
			case PID_NO_L3: /* the most likly case */
				ai_dump(dumpbuf, (unsigned char *)pax25packet->pdata, pax25packet->datalength);
				break;
			case PID_VJIP06:
			case PID_VJIP07:
			case PID_SEGMENT:
				data_dump(dumpbuf, (unsigned char *)pax25packet->pdata, pax25packet->datalength, dumpstyle);
				break;
#if 0
			case PID_ARP:
				arp_dump(pax25packet->pdata, pax25packet->datalength);
				break;
			case PID_X25:
				rose_dump(pax25packet->pdata, pax25packet->datalength, dumpstyle);
				break;
			case PID_TEXNET:
				data_dump(pax25packet->pdata, pax25packet->datalength, dumpstyle);
				break;
			case PID_FLEXNET:
				flexnet_dump(pax25packet->pdata, pax25packet->datalength, dumpstyle);
				break;
#else
			case PID_IP:
				ip_dump(dumpbuf,
					  (unsigned char *)pax25packet->pdata,
					  pax25packet->datalength,
					  dumpstyle);
				break;
			case PID_NETROM:
				netrom_dump(dumpbuf,
					    (unsigned char *)pax25packet->pdata,
					    pax25packet->datalength,
					    dumpstyle,
					    pax25packet->frametype);
				break;

			case PID_ARP:
			case PID_X25:
			case PID_TEXNET:
			case PID_FLEXNET:
				printf("Unhandled PID found 0x%02x\n", pax25packet->pid);
#endif
			default:
				ai_dump(dumpbuf, (unsigned char *)pax25packet->pdata, pax25packet->datalength);
				break;
		}
	} else if (pax25packet->frametype == FRMR && pax25packet->datalength >= 3) {
		/* FIX ME XXX
		lprintf(T_AXHDR, ": %s", decode_type(ftype(data[0])));
*/
		mprintf(dumpbuf, T_AXHDR, ": %02X", pax25packet->pdata);
		mprintf(dumpbuf, T_AXHDR, " Vr = %d Vs = %d",
			  (pax25packet->pdata[1] >> 5) & MMASK,
			  (pax25packet->pdata[1] >> 1) & MMASK);
		if(pax25packet->pdata[2] & W)
			mprintf(dumpbuf, T_ERROR, " Invalid control field");
		if(pax25packet->pdata[2] & X)
			mprintf(dumpbuf, T_ERROR, " Illegal I-field");
		if(pax25packet->pdata[2] & Y)
			mprintf(dumpbuf, T_ERROR, " Too-long I-field");
		if(pax25packet->pdata[2] & Z)
			mprintf(dumpbuf, T_ERROR, " Invalid seq number");
		mprintf(dumpbuf, T_AXHDR,"%s\n", dama);
	} else if ((pax25packet->frametype == SABM || pax25packet->frametype == UA) && pax25packet->datalength >= 2) {
		/* FlexNet transmits the QSO "handle" for header
		 * compression in SABM and UA frame data fields
		 */
		/* lprintf(T_AXHDR," [%d]%s\n", (pax25packet->pdata << 8) | pax25packet->pdata[1], dama); */
	} else {
		mprintf(dumpbuf, T_AXHDR,"%s\n", dama);
	}
	return(dumpbuf);
}



static char * decode_type(int type)
{
	switch (type) {
		case I:     return "I";
		case SABM:  return "C";
		case SABME: return "CE";
		case DISC:  return "DISC";
		case DM:    return "DM";
		case UA:    return "UA";
		case RR:    return "RR";
		case RNR:   return "RNR";
		case REJ:   return "REJ";
		case FRMR:  return "FRMR";
		case UI:    return "UI";
		default:    return "[invalid]";
	}
}

int mem2ax25call(struct t_ax25call *presbuf, unsigned char *data)
{
	int i;
	char *s;
	char c;

	s = presbuf->sCall;
	memset(s,0,sizeof(*presbuf));
	for (i = 0; i < ALEN; i++) {
		c = (data[i] >> 1) & 0x7F;
		if (c != ' ') *s++ = c;
	}
	/* *s = '\0'; */
	/* decode ssid */
	presbuf->ssid = (data[ALEN] & SSID) >> 1;
	return 0;
}



char * pax25(char *buf, unsigned char *data)
/* shift-ASCII-Call to ASCII-Call */
{
	int i, ssid;
	char *s;
	char c;

	s = buf;

	for (i = 0; i < ALEN; i++) {
		c = (data[i] >> 1) & 0x7F;

		if (!isalnum(c) && c != ' ') {
			strcpy(buf, "[invalid]");
			return buf;
		}

		if (c != ' ') *s++ = c;
	}
	/* decode ssid */
	if ((ssid = (data[ALEN] & SSID)) != 0)
		sprintf(s, "-%d", ssid >> 1);
	else
		*s = '\0';

	return(buf);
}


static int ftype(unsigned char *data, int *type, int *ns, int *nr, int *pf, int extseq)
/* returns then length of the controlfield, this could be > 1 ... */
{
	*nr = *ns = -1; /* defaults */
	if (extseq) {
		if ((*data & 0x01) == 0) {	/* An I frame is an I-frame ... */
			*type = I;
			*ns   = (*data >> 1) & 127;
			data++;
			*nr   = (*data >> 1) & 127;
			*pf   = *data & EPF;
			return 2;
		}
		if (*data & 0x02) {
			*type = *data & ~PF;
			*pf   = *data & PF;
			return 1;
		} else {
			*type = *data;
			data++;
			*nr   = (*data >> 1) & 127;
			*pf   = *data & EPF;
			return 2;
		}
	} else { /* non extended */
		if ((*data & 0x01) == 0) {	/* An I frame is an I-frame ... */
			*type = I;
			*ns   = (*data >> 1) & 7;
			*nr   = (*data >> 5) & 7;
			*pf   = *data & PF;
			return 1;
		}
		if (*data & 0x02) {		/* U-frames use all except P/F bit for type */
			*type = *data & ~PF;
			*pf   = *data & PF;
			return 1;
		} else {			/* S-frames use low order 4 bits for type */
			*type = *data & 0x0F;
			*nr   = (*data >> 5) & 7;
			*pf   = *data & PF;
			return 1;
		}
	}
}

#if 0 /* not used for reference only */
void ax25_dump(struct t_ax25packet *pax25packet, unsigned char *data, int length, int dumpstyle)
{

	ax25_decode( pax25packet, data, length );
#if 0
	/* look for matching QSO */
	pax25packet->pQSO = doQSOMheard(pax25packet);
	tryspy(pax25packet);

	/* switch temp. all those clients off, which do not want to see supervisor
	 * frames, if it is not an I-Frame. Yes. This is hacklike... */
	clientEnablement( ceONLYINFO, (pax25packet->pid > -1) );
	clientEnablement( ceONLYIP,   (pax25packet->pid == PID_IP) );
#endif
	ax25packet_dump( pax25packet, dumpstyle );

#if 0
	/* and switch all disabled clients on */
	clientEnablement( ceONLYIP,   1 );
	clientEnablement( ceONLYINFO, 1 );
#endif
}
#endif /* not used */

void ascii_dump(char *dispbuf, unsigned char *data, int length)
/* Example:  "0000 Dies ist ein Test.."
 */
{
	int  i, j;
	char buf[100];

	for (i = 0; length > 0; i += 64) {
		sprintf(buf, "%04X  ", i);

		for (j = 0; j < 64 && length > 0; j++) {
			length--;

			/* substitute a period for null or cr */
#if 1
			{
				unsigned char c;

				c = *data++;
				if ((c != '\0') && (c != '\n'))
					strncat(buf,(char *)&c, 1);
				else
					//				strcat(buf, ".");
					strncat(buf,(char *)&c, 1);
			}
#endif
		}

		mprintf(dispbuf, T_DATA, "%s\n", buf);
	}
}

void readable_dump(char *dispbuf, unsigned char *data, int length)
/* Example "Dies ist ein Test.
 *	   "
 */
{
	unsigned char c;
	int  i;
	int  cr = 1;
	char buf[BUFSIZE];

	for (i = 0; length > 0; i++) {
		c = *data++;
		length--;
		switch (c) {
			case 0x00:
				buf[i] = ' ';
			case 0x0A: /* hum... */
			case 0x0D:
				if (cr) buf[i] = '\n';
				else i--;
				break;
			default:
				buf[i] = c;
		}
		cr = (buf[i] != '\n');
	}
	if (cr)
		buf[i++] = '\n';
	buf[i++] = '\0';
	mprintf(dispbuf, T_DATA, "%s", buf);
}

void hex_dump(char *dispbuf, unsigned char *data, int length)
/*  Example: "0000 44 69 65 ......               Dies ist ein Test."
 */
{
	int  i, j, length2;
	unsigned char c;
	char *data2;
	char buf[4], hexd[49], ascd[17];

	length2 = length;
	data2	= (char *)data;

	for (i = 0; length > 0; i += 16) {
		hexd[0] = '\0';
		for (j = 0; j < 16; j++) {
			c = *data2++;
			length2--;

			if (length2 >= 0)
				sprintf(buf, "%2.2X ", c);
			else
				strcpy(buf, "   ");
			strcat(hexd, buf);
		}

		ascd[0] = '\0';
		for (j = 0; j < 16 && length > 0; j++) {
			c = *data++;
			length--;

			sprintf(buf, "%c", ((c != '\0') && (c != '\n')) ? c : '.');
			strcat(ascd, buf);
		}

		mprintf(dispbuf, T_DATA, "%04X  %s | %s\n", i, hexd, ascd);
	}
}

void ai_dump(char *dispbuf, unsigned char *data, int length)
{
	int testsize, i;
	char c;
	char *p;
	int dumpstyle = READABLE;

	/* make a smart guess how to dump data */
	testsize = (10>length) ? length:10;
	p = (char *)data;
	for (i = testsize; i>0; i--) {
		c = *p++;
		if( iscntrl(c) && (!isspace(c)) ) {
			dumpstyle = ASCII; /* Hey! real smart! $TODO */
			break;
		}
	}
	/* anything else */
	data_dump(dispbuf, data, length, dumpstyle);
}

void data_dump(char *dispbuf, unsigned char *data, int length, int dumpstyle)
{
	switch (dumpstyle) {

		case READABLE:
			readable_dump(dispbuf, data, length);
			break;
		case HEX:
			hex_dump(dispbuf, data, length);
			break;
		default:
			ascii_dump(dispbuf, data, length);
	}
}

/* Display INP route information frames */
static void netrom_inp_dump(char *dispbuf, unsigned char *data, int length)
{
	char node[10];
	char alias[7];
	int hops;
	int tt;
	int alen;
	int i;

	if (data[0]==0xff) {
		mprintf(dispbuf, T_AXHDR, "INP Route Information Frame:\n");
		i=1;
		while (i<length-10) {
			pax25(node, data+i);
			i+=7;
			hops=data[i++];
			tt=data[i++]*256;
			tt+=data[i++];
			alias[0]=0;
			while (i<length-data[i] && data[i]) {
				if (data[i+1]==0x00) {
					alen=data[i]-2;
					alen=alen < 7 ? alen : 6;
					memcpy(alias, data+i+2, alen);
					alias[alen]=0;
				}
				i+=data[i];
			}
			i++;
			mprintf(dispbuf, T_DATA, "        %12s  %-6s   %6u %6u\n", node, alias, hops, tt);
		}
	}
}

/* Display NET/ROM network and transport headers */
void netrom_dump(char *dispbuf, unsigned char *data, int length, int hexdump, int type)
{
	char tmp[15];
	int i;

	/* See if it is a routing broadcast */
	if (data[0] == NR3NODESIG) {
		/* Filter out INP routing frames */
		if (type!=UI) {
			netrom_inp_dump(dispbuf, data, length);
			return;
		}
		memcpy(tmp, data + 1, ALEN);
		tmp[ALEN] = '\0';
		mprintf(dispbuf, T_AXHDR, "NET/ROM Routing: %s\n", tmp);

		data += (ALEN + 1);
		length -= (ALEN + 1);

		for (i = 0; i < NRDESTPERPACK; i++) {
			if (length < AXLEN)
				break;
			mprintf(dispbuf, T_DATA, "        %12s", pax25(tmp, data));

			memcpy(tmp, data + AXLEN, ALEN);
			tmp[ALEN] = '\0';
			mprintf(dispbuf,T_DATA, "%8s", tmp);

			mprintf(dispbuf, T_DATA, "    %12s",
				  pax25(tmp, data + AXLEN + ALEN));
			mprintf(dispbuf, T_DATA, "    %3u\n",
				  data[AXLEN + ALEN + AXLEN]);

			data += (AXLEN + ALEN + AXLEN + 1);
			length -= (AXLEN + ALEN + AXLEN + 1);
		}

		return;
	}
}

#ifdef B2F_DECODE
/*
 * B2F decompression routines
 */

#define CHRNUL 0
#define CHRSOH 1
#define CHRSTX 2
#define CHREOT 4
#define MAX_SUBJECT_LEN 80 /* length of subject line or title of the message */
#define MAX_AX25_HDR_LEN 32 /* 28 bytes of address, 2 bytes of control, 1 byte pid */

typedef enum {
	B2F_STATE_UNKNOWN,
	B2F_STATE_SOH,
	B2F_STATE_STX,
	B2F_STATE_STX_MORE,
	B2F_STATE_STX_FRAG,
	B2F_STATE_EOT,
	B2F_STATE_MAX
} b2f_state_t;

char *b2f_state_str[] = {
	"B2F unknown state",
	"B2F found SOH",
	"B2F found STX contained in pkt",
	"B2F found STX needs more pkt",
	"B2F msg frag",
	"B2F found EOT"
};

int countproposals(char *buf)
{
	int propcount = 0;
	char c;

	while( (c = *buf++) != CHRNUL) {
		if (c == 'Y') {
			propcount++;
		}
	}
	return( propcount );
}
int find_char_offset(uint8_t *buf, uint8_t protochar, int maxLen)
{
	int offset = 0;

	while (*buf != protochar && maxLen--)
	{
		buf++;
		offset++;
	}
	if(*buf != protochar)
		offset = 0;

	return offset;
}
void dumpsome(uint8_t *pbuf, int len)
{
	int i;

	for(i = 0; i < len; i++) {
		printf("%02x ", *(pbuf+i));
	}
	printf("\n");
}

char *compression_check(struct state *state, char *dumpbuf)
{
	char  *ptrunc, *ptr;
	int   i;
	bool  bIsAscii = true;


	if ((ptrunc = strstr(dumpbuf, "Text:0000")) != NULL) {
		ptrunc += 9;
	} else if ((ptrunc = strstr(dumpbuf, "Text:")) != NULL) {
		ptrunc += 5;
		ptr = ptrunc;
		i=0;
		/* look for any non ascii characters */
		while( i < 5 && *(ptr+i) != 0) {
			if(!isascii(*(ptr + i))) {
				bIsAscii = false;
				break;
			}
			i++;
		}
		if(bIsAscii) {
			ptrunc = NULL;
		}
	}
#ifdef DEBUG_B2F
	{
		char  dbuf[32];

		if(ptrunc) {
			strncpy(dbuf, ptrunc, 10);
			dbuf[10] = '\0';
		} else {
			strcpy(dbuf, "NO");
		}
		printf("b2f state %d ptr: %p, ind: %d,  %s\n",
		       state->prop_ctrl.b2f_state, strstr(dumpbuf, "Text:"), i, dbuf);
	}
#endif	/*  DEBUG_B2F */

	return ptrunc;
}

uint8_t *checkb2f(struct state *state, uint8_t *rawbuf, int size)
{
	uint8_t *praw;
	int offset;
	int stxlen;

	state->prop_ctrl.b2f_state = B2F_STATE_UNKNOWN;
	/* start looking at start of packet past AX.25 address */
	praw = rawbuf;

	/* Check if previous packet only contained a header
	 * (SOH plus subject) */
	if(!state->prop_ctrl.next_pkt_stx) {

		/* ASSERT */
		if(state->prop_ctrl.excess_partial_size) {
			printf("Found pending B2F frag\n");
			state->prop_ctrl.b2f_state = B2F_STATE_STX_FRAG;
		}

		offset=find_char_offset(praw, CHRSOH, MIN(MAX_AX25_HDR_LEN, size) );
		praw = praw + offset;

		if ( *praw != CHRSOH ) {
			if(state->prop_ctrl.need_partial_size) {
				printf("%s: Expecting %d bytes to complete msg\n",
				       __FUNCTION__, state->prop_ctrl.need_partial_size);
			} else {
				printf("%s: SOH not found (0x%02x), offset = %d\n", __FUNCTION__, *praw, offset);
			}
			return(NULL);
		}
		praw++;
		printf("SOH found, hdr len: %d, size: %d\n", *praw, size);
		if(*praw + 2 >= size) {
			printf("Packet only contains hdr/subj, next pkt starts with STX\n");
			state->prop_ctrl.next_pkt_stx = true;
			state->prop_ctrl.b2f_state = B2F_STATE_SOH;
			return(NULL);
		}

		offset=find_char_offset(praw, CHRSTX, MIN(MAX_SUBJECT_LEN, size-offset));
		praw = praw + offset;
	}
	/* expecting STX but may have a SOH resend */
	if ( *praw == CHRSOH ) {
		printf("SOH found, Subj: %s\n", praw+2);
		state->prop_ctrl.b2f_state = B2F_STATE_SOH;
		return NULL;
	}
	if ( *praw == CHRSTX ) {
		printf("STX found\n");
		stxlen =  *(praw+1) ? *(praw+1) : 256;
		state->prop_ctrl.b2f_stx_size = stxlen;
		state->prop_ctrl.b2f_state = (stxlen > size) ? B2F_STATE_STX_MORE : B2F_STATE_STX_FRAG;

	} else {
		printf("STX not found 0x%02x, offset = %d\n", *praw, offset);
		dumpsome(praw, MIN(size,128));
		praw = NULL;
	}
	state->prop_ctrl.next_pkt_stx = false;

	return praw;
}
int gather_b2fmsg(uint8_t *pdecomp, struct buffer *compbuf, int stxlen, int cksum) {

	int  c, i;

	for( i = 0 ; i < stxlen; i++) {
		c = *pdecomp++;
		if(buffer_addchar(compbuf, c ) < 0) {
			printf("%s: memory failure adding char to buffer\n", __FUNCTION__);
			break;
		}
		cksum = (cksum + c) % 256;
	}
	return cksum;
}

/*
 * Gather a packet that may have multiple STX
 * Initial pkt buffer may be a partial gather
 */
int multigather_b2fmsg(struct state *state, uint8_t *pdecomp, struct buffer *compbuf, int stxlen, int buflen, int *checksum) {

	int  gather_len;
	int  cksum = *checksum;
	uint8_t *startdecomp = pdecomp;
	bool first_time = true;

	printf("%s: Gather buffer: %p, comp: %p, size: %d, cksum: 0x%02x\n",
	       __FUNCTION__,
	       pdecomp,
	       state->prop_ctrl.compressed,
	       stxlen,
	       state->prop_ctrl.cksum );


	while (first_time || *(pdecomp) == CHRSTX ) {

		if(!first_time) {
			/* got start of a new b2f message */
			pdecomp++;
			stxlen = *pdecomp ? *pdecomp : 256;
			pdecomp++;

			printf("%s: found STX, pkt len: %d, msg len: %d\n",
			       __FUNCTION__, buflen, stxlen);
		}
		first_time = false;

		gather_len = (buflen > stxlen ? stxlen : stxlen - buflen);

		printf("%s: Using msg len: %d\n", __FUNCTION__, gather_len);

		cksum = gather_b2fmsg(pdecomp,
				      compbuf,
				      gather_len,
				      cksum );

		if(gather_len < stxlen) {
			state->prop_ctrl.compressed = compbuf;
			state->prop_ctrl.need_partial_size = stxlen - gather_len;
			state->prop_ctrl.excess_partial_size = 0;
			printf("%s: remainder of packet buffer (%d) is smaller than size of message (%d) need %d bytes\n",
			       __FUNCTION__,
			       buflen, stxlen,
			       state->prop_ctrl.need_partial_size);
			break;

		}
		buflen -= gather_len;
		pdecomp = pdecomp + gather_len;

		if (buflen <=  2) {
			break;
		}
	}
	*checksum = cksum;
	return (pdecomp - startdecomp);
}

uint8_t b2f_decompress(struct state *state, uint8_t *pdecomp, int pkt_size )
{
	uint8_t stxlen;
	struct buffer *compbuf = NULL;

	pdecomp++;
	stxlen = *pdecomp ? *pdecomp : 256;
	pdecomp++;

	compbuf = state->prop_ctrl.compressed;
	if(compbuf == NULL) {

		/* get a buffer for decompressing packet */
		if ((compbuf = buffer_new()) == NULL) {
			printf("%s: buffer error\n", __FUNCTION__);
			return (0);
		}
		state->prop_ctrl.compressed = compbuf;
	}

	printf("%s: found STX before EOT, remaining size %d, next ctrl byte = 0x%02x\n",
	       __FUNCTION__, stxlen, *(pdecomp+stxlen) );
	printf("%s: Gather Using msg len: %d\n", __FUNCTION__, stxlen);

	/* Check if b2f msg length is larger than
	 * current packet length */
	if(stxlen > pkt_size) {

		state->prop_ctrl.cksum = gather_b2fmsg(pdecomp, compbuf, pkt_size, 0);
		state->prop_ctrl.compressed = compbuf;
		state->prop_ctrl.need_partial_size = stxlen - pkt_size;
		state->prop_ctrl.excess_partial_size = 0;
		state->prop_ctrl.b2f_state = B2F_STATE_STX_FRAG;

		printf("%s: remainder of packet buffer (%d) is smaller than size of message (%d) need %d bytes, buf index %ld\n",
		       __FUNCTION__,
		       pkt_size, stxlen,
		       state->prop_ctrl.need_partial_size,
		       compbuf->dlen
		      );
		printf("%s: Save state buffer: %p, comp: %p, size: %d, cksum: 0x%02x\n",
		       __FUNCTION__,
		       pdecomp,
		       state->prop_ctrl.compressed,
		       state->prop_ctrl.need_partial_size,
		       state->prop_ctrl.cksum );


		return(0);
	}


	state->prop_ctrl.cksum = gather_b2fmsg(pdecomp,
			      state->prop_ctrl.compressed,
			      stxlen,
			      state->prop_ctrl.cksum );

	return (stxlen);
}
uint32_t get_compbuf_size(struct buffer *inbuf) {
	unsigned short crc_read;
	unsigned long int textsize;
	int x;

	if ((x = buffer_iterchar(inbuf)) == EOF) {
		printf("%s: failed on getting crc 1\n", __FUNCTION__);
		return 0;
	}
	crc_read = (unsigned short)x;
	if ((x = buffer_iterchar(inbuf)) == EOF) {
		printf("%s: failed on getting crc 2\n", __FUNCTION__);
		return 0;
	}
	crc_read |= (unsigned short)(x << 8);

	fprintf(stderr, "Decompressed Msg CRC  = %04x\n", crc_read);
	if ((x = buffer_iterchar(inbuf)) == EOF) {
		return 0;
	}
	textsize = (long unsigned int)x;
	if ((x = buffer_iterchar(inbuf)) == EOF) {
		return 0;
	}
	textsize |= (long unsigned int)(x << 8);
	if ((x = buffer_iterchar(inbuf)) == EOF) {
		return 0;
	}
	textsize |= (long unsigned int)(x << 16);
	if ((x = buffer_iterchar(inbuf)) == EOF) {
		return 0;
	}
	textsize |= (long unsigned int)(x << 24);

	fprintf(stderr, "Decompressed Msg Size = %lu\n", textsize);

	buffer_rewind(inbuf);
	return textsize;

}

uint8_t *decode_b2fmsg(struct state *state, int pkt_size, uint8_t *pdecomp, int cksum)
{
	int i,c;
	struct buffer *decompbuf = NULL,  *compbuf;
	uint8_t next_char = *pdecomp;
	uint8_t stxlen;
	int offset;

	if(state->prop_ctrl.compressed != NULL) {
		compbuf = state->prop_ctrl.compressed;
	} else {
		/* get a buffer for decompressing packet */
		if ((compbuf = buffer_new()) == NULL) {
			printf("%s: buffer error\n", __FUNCTION__);
			return ((uint8_t *)NULL);
		}
	}

	switch(next_char) {
		case CHRSOH:

			pdecomp++;
			printf("%s: SOH found, hdrlen: %d, pktsize: %d\n", __FUNCTION__, *pdecomp, pkt_size);

			if(*pdecomp + 2 >= pkt_size) {
				printf("Packet only contains hdr/subj, next pkt starts with STX\n");
				state->prop_ctrl.next_pkt_stx = true;
				return(NULL);
			}

			offset=find_char_offset(pdecomp, CHRSTX, MIN(MAX_SUBJECT_LEN, pkt_size));
			pdecomp = pdecomp + offset;
			state->prop_ctrl.b2f_state = B2F_STATE_STX;

			break;

		case CHRSTX:
			printf("%s: STX found, msglen: %d, pktsize: %d\n",
			       __FUNCTION__, *(pdecomp+1), pkt_size);

			if( (stxlen = b2f_decompress(state, pdecomp, pkt_size)) == 0) {
				pdecomp = NULL;
			} else {
				pdecomp += stxlen;
			}
			break;

			/* Found the End of Transmission */
		case CHREOT:

			printf("%s: Found EOT, pktsize %d, next ctrl 0x%02x\n",
			       __FUNCTION__, pkt_size, *(pdecomp+2));

			if( ((*(pdecomp+1) + cksum) % 256) == 0 ) {
				printf("GOOD Message checksum\n");
			} else {
				printf("BAD message checksum, calc 0x%02x, expected 0x%02x\n",
				       cksum, *(pdecomp+1));
				dumpsome(pdecomp-4, 16);
			}

			if(state->prop_ctrl.prop_pending_cnt) {
				state->prop_ctrl.prop_pending_cnt--;
			}

			printf("Filled compression buffer crc=0x%02x, Extracting\n", cksum);
			/* Add EOT & checksum to buffer */
			for( i = 0 ; i < 2; i++) {
				c = *pdecomp++;
				if(buffer_addchar(compbuf, c ) < 0) {
					printf("%s: memory failure adding char to buffer\n", __FUNCTION__);
					break;
				}
			}
			buffer_addchar(compbuf, CHRNUL );
			buffer_rewind(compbuf);
			get_compbuf_size(compbuf);

			if ((decompbuf = version_1_Decode(compbuf)) == NULL) {
				printf("lz decompress error() - %s\n",strerror(errno));
			} else {
				printf("Finished decompressing buffer\n");
				printf("In buf: alen %ld, dlen %ld, output alen: %ld, dlen: %ld\n",
				       compbuf->alen, compbuf->dlen, decompbuf->alen, decompbuf->dlen);
				dumpsome((compbuf->data) + compbuf->dlen - 16, 20);
				/* dumpsome(decompbuf->data, *(pdecomp+4)); */
				buffer_addchar(decompbuf, '\0');
				printf("decomp: %s\n", decompbuf->data);
			}
			buffer_free(compbuf);
			state->prop_ctrl.compressed = NULL;
			state->prop_ctrl.b2f_state = B2F_STATE_SOH;
			state->prop_ctrl.need_partial_size = 0;

			if(pkt_size > 2) {
				printf("%s: End EOT, more packet to decode, next char: 0x%02x, remaining size: %d\n",
				       __FUNCTION__, *pdecomp, pkt_size -2);
			} else {
				printf("%s: End EOT, size %d, next ctrl 0x%02x\n",
				       __FUNCTION__, pkt_size, *pdecomp);

				pdecomp = NULL;
				state->prop_ctrl.next_pkt_continue = false;
			}
			break;

		default:
			printf("%s: Unhandled ctrl char 0x%02x\n", __FUNCTION__, next_char);
			state->prop_ctrl.b2f_state = B2F_STATE_UNKNOWN;

			/* Save state for next packet */
			state->prop_ctrl.compressed = compbuf;


#if 0
			/* Detect a B2F fragement */
			if(size - (pdecomp - rawbuf) > 0) {
				state->prop_ctrl.partial_size = size - (pdecomp-rawbuf);
				printf("%s 1: Remaining bytes in packet: %d, proposals pending %d\n",
				       __FUNCTION__,
				       state->prop_ctrl.partial_size, state->prop_ctrl.prop_pending_cnt);
				if((state->prop_ctrl.partial_buf = malloc(state->prop_ctrl.partial_size + 1)) == NULL) {
					printf("%s: Failure allocating memory for B2F fragment\n", __FUNCTION__);
				} else {
					memcpy(state->prop_ctrl.partial_buf, pdecomp, state->prop_ctrl.partial_size);
				}
				dumpsome(pdecomp, size - (pdecomp-rawbuf));

			} else if((pdecomp - rawbuf) -size > 0) {
				/* This should never happen */
				printf("%s 2: Parsed beyond end of packet\n", __FUNCTION__);
			} else {
				printf("%s 3: message exactly fits in buffer\n");
				/* Finished with any lingering
				 * B2F fragments */
				state->prop_ctrl.partial_size = 0;
				if(state->prop_ctrl.partial_buf != NULL) {
					free(state->prop_ctrl.partial_buf);
					state->prop_ctrl.partial_buf = NULL;
				}
			}
#endif

			if(state->prop_ctrl.compressed != NULL) {
				buffer_free(state->prop_ctrl.compressed);
				state->prop_ctrl.compressed = NULL;
			}
			state->prop_ctrl.next_pkt_continue = false;
			state->prop_ctrl.b2f_state = B2F_STATE_SOH;
			pdecomp = NULL;

			break;
	}
	state->prop_ctrl.decomp = decompbuf;
	return(pdecomp);
}
/*
 * modify ascii dump buffer to not display following compressed
 * characters.
 */
void term_ugly_display(struct state *state, char *dispbuf, uint8_t *rawbuf, int pkt_size)
{
	char tmpstr[32+MAX_SUBJECT_LEN];
	char b2f_str[64]={" "};
	char *display_state, *displaystate_str="Unkown";

	*dispbuf = '\0';

	display_state = (state->prop_ctrl.b2f_state < B2F_STATE_MAX) ?
			b2f_state_str[state->prop_ctrl.b2f_state] :
			displaystate_str;

	if(state->prop_ctrl.b2f_state == B2F_STATE_SOH) {
		sprintf(b2f_str, "%d", state->prop_ctrl.b2f_stx_size);
	}


	sprintf(tmpstr, " %s data size %d %s\n",
		  display_state,
		  pkt_size,
		  b2f_str
	       );
	if(state->prop_ctrl.b2f_state == B2F_STATE_SOH) {
		sprintf(tmpstr+strlen(tmpstr), " Subj: %s\n", rawbuf+2);
		dumpsome(rawbuf, 16);
		dumpsome((uint8_t *)dispbuf, 16);
	}

	strcat(dispbuf, tmpstr);
}

/*
 * rawbuf - data portion of ax.25 packet after ax.25 header
 * size - size of ax.25 data packet
 */
struct buffer *b2f_decode(struct state *state, char *dumpbuf, uint8_t *rawbuf, int size)
{
	int cksum = 0;
	char  *ptrunc;
	uint8_t *pdecomp=NULL, *nxt_pdecomp;
	struct buffer *decompbuf = NULL;
	int stxlen;
	int rawbuf_index = 0;
	int next_buf_size;
	int next_buf_offset;


	if( (ptrunc = strstr(dumpbuf, "Text:FS")) != NULL) {
		state->prop_ctrl.prop_pending_cnt = countproposals(ptrunc+5);
		printf("%s: proposals pending: %d\n", __FUNCTION__, state->prop_ctrl.prop_pending_cnt);
		dumpsome((uint8_t *)(ptrunc+5), 16);
	}
	/* Check for a B2F message coming in */
	else if ((ptrunc = compression_check(state, dumpbuf)) != NULL) {

		printf("%s: TEXT: Check raw buf, size: %d\n", __FUNCTION__, size);
		dumpsome(rawbuf, size);

		checkb2f(state, rawbuf, size);

		/* modify ascii dump buffer to not display compressed buffer */
		term_ugly_display(state, ptrunc, rawbuf, size);


		if(state->prop_ctrl.need_partial_size) {

			printf("%s: Intermediate compressed buffer, size: %d, need %d\n",
			       __FUNCTION__, size, state->prop_ctrl.need_partial_size);
			dumpsome(rawbuf, size);

			next_buf_size = state->prop_ctrl.need_partial_size;
			next_buf_offset = 0;

			if(size > state->prop_ctrl.need_partial_size + 2) {
				//			next_buf_size = size - 2;
				next_buf_offset = size - (state->prop_ctrl.need_partial_size+2);
			}

			printf("Check size of buffer: %d, need size: %d, buf index %ld, next bf size: %d, offset: %d\n",
			       size,
			       state->prop_ctrl.need_partial_size,
			       state->prop_ctrl.compressed->dlen,
			       next_buf_size,
			       next_buf_offset);


			if(state->prop_ctrl.need_partial_size + 2 > size) {
				printf("\nNeed to handle this?? ...\n\n");
				state->prop_ctrl.b2f_state = B2F_STATE_EOT;
			}

			next_buf_size = multigather_b2fmsg(state, rawbuf,
				state->prop_ctrl.compressed,
				next_buf_size,
				size,
				&state->prop_ctrl.cksum );

			cksum = state->prop_ctrl.cksum;

			printf("%s: 1- Remaining packet pktlen: %d, msglen: %d, szie: %d\n",
			       __FUNCTION__, size, next_buf_size, size - next_buf_size);

			printf("decode ...");
			/* point to 1 byte past end of message buffer, EOT? */
			pdecomp = rawbuf + next_buf_size;
			size -= next_buf_size;

			while ( ((nxt_pdecomp = decode_b2fmsg(
				state,
				size,
				pdecomp,
				cksum)) != NULL) && size > 0) {

				size -= (nxt_pdecomp - pdecomp);
				pdecomp = nxt_pdecomp;
			}
			if(state->prop_ctrl.decomp == NULL)
				printf("decode_b2fmsg,  decompression buffer NULL\n");
			else
				printf("decode_b2fmsg,  decompression buffer OK\n");
			decompbuf = state->prop_ctrl.decomp;
			state->prop_ctrl.need_partial_size = 0;

		}
		/* Check if contiunuation packet is required */

		else if(state->prop_ctrl.next_pkt_continue) {
			printf("Continuation pkt set size = %d\n", size);
			state->prop_ctrl.next_pkt_continue = false;
		}

		/* Check for a new B2F message comin in */
		else if((pdecomp = checkb2f(state, rawbuf, size)) != NULL) {
			int pktlen;

			/* get index into packet for start of data
			 * +2 for CHRSTX & len byte */
			rawbuf_index = (pdecomp - rawbuf) + 2;
			pktlen = size - rawbuf_index;

			printf("%s: New mesg: check raw buf index: %d first pdecomp\n", __FUNCTION__, rawbuf_index);
#ifdef DEBUG_B2F
			dumpsome(pdecomp, 16);
			dumpsome(rawbuf, 16);
#endif
			/* qualify beginning of message */
			if(*(pdecomp) == CHRSTX) {
				state->prop_ctrl.cksum = 0;

				if( (stxlen = b2f_decompress(state, pdecomp, pktlen)) == 0) {
					pdecomp = NULL;
				} else {
					pdecomp += stxlen;
				}
				if(pdecomp) {
					size = pktlen - stxlen;
					printf("%s: 2- Remaining packet pktlen: %d, msglen: %d, size: %d\n",
					       __FUNCTION__, pktlen, stxlen, size);

					while((nxt_pdecomp = decode_b2fmsg(
						state,
						size,
						pdecomp,
						cksum)) != NULL) {

						size -= (nxt_pdecomp - pdecomp);
						pdecomp = nxt_pdecomp;
					}

				}
				decompbuf = state->prop_ctrl.decomp;
			}
		}
	}
	return( decompbuf );
}
#endif /* B2F_DECODE */
