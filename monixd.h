/*
 *  ax25spyd - An AX.25 monitorspy daemon
 *  Copyright (C) 1999 Free Software Foundation, Inc.
 *  Copyright (C) 1999 Walter Koch, dg9ep
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define CONFFILE AX25CONFDIR"/ax25spyd.conf"

#define MAXCLIENTS  50
/* size of ax-25 adressfield for one call */
#define AXLEN		7
/* size of ax25 callfield inside the adressfield */
#define ALEN		6


#include <unistd.h>
#ifdef __GLIBC__
#include <net/if.h>
#else
#include <linux/if.h>
#endif
#include <linux/if_ether.h>

/* in monixd */
/* extern struct t_client client[MAXCLIENTS]; */
extern int nClient ;
extern int fVerbose ;
extern int fForceNoKiss ;

/* in mheard.c */
extern struct t_mheard mheard;
extern struct t_CallMheard *pCallMheardRoot;
extern struct t_qso  *pQSOMheardRoot;

extern int fDaemon;
extern int fSpyAllToFile;
extern int fDoMailSpy;

/*
 * =============================
 */

#define BUFSIZE  3500

#define ASCII		0
#define HEX		1
#define READABLE	2

#define U		0x03
#define S		0x01
#define PF		0x10
#define EPF		0x01
/* see .frametype */
#define I		0x00
#define FRMR		0x87
#define UI		0x03

#define RR		0x01
#define RNR		0x05
#define REJ		0x09

#define SABM		0x2F
#define SABME		0x6F
#define DISC		0x43
#define DM		0x0F
#define UA		0x63

/* From MonixD to client */
#define T_ERROR 	1

#define T_PORT		2

#define T_KISS		3
#define T_BPQ		4
#define T_DATA		5
#define T_PROTOCOL	6
#define T_AXHDR 	7
#define T_ADDR		8
#define T_IPHDR 	9
#define T_TCPHDR	10
#define T_ROSEHDR	11
#define T_TIMESTAMP	12
#define T_FLEXNET	13

#define T_ZIERRAT	14
/* Kindof T_ADDR */
#define T_CALL		15
/* Kindof T_CALL */
#define T_FMCALL	16
#define T_TOCALL	17
#define T_VIACALL	18

#define T_HDRVAL	19

#define T_MHEARD	   118
#define T_MHEARDSTRUCT	   119
#define T_CALLMHEARDSTRUCT 121
#define T_QSOMHEARDSTRUCT  122

#define T_DXCLUST	   120

/* see file programming */
#define T_SPYDATA	   123
#define T_SPYHEADER	   124
#define T_SPYEND	   125

#define T_RAWPACKET	   126

#define T_OFFERQSO	   127
#define T_QSOOFFER	   T_OFFERQSO

#define T_VERSION	   128
#define T_PROTVERSION	   129

/* End of data flag for variable count of data, see sendCallMHeard() */
#define T_ENDOFDATA	   200

#define PID_VJIP06	0x06
#define PID_VJIP07	0x07
#define PID_SEGMENT	0x08
#define PID_ARP 	0xCD
#define PID_NETROM	0xCF
#define PID_IP		0xCC
#define PID_X25 	0x01
#define PID_TEXNET	0xC3
#define PID_FLEXNET	0xCE
#define PID_PSATFT	0xBB
#define PID_PSATPB	0xBD
#define PID_NO_L3	0xF0
#define PID_TEXT	PID_NO_L3

struct t_ax25call {
	char sCall[6+1];
	int  ssid;
};

/*------------------------------------------------------------*/
/* well, not really a ax25packet... */
struct t_ax25packet {
	int     valid;
    /* Moni */
        time_t  time;
        struct timeval timeval;
	char    *port;
    /* KISS */
	int     fsmack,
	fflexcrc,
	fcrc;
    /* AX25 */
    /* adress */
	struct t_ax25call fmcall;
	struct t_ax25call tocall;
	struct t_ax25call digi[8];
	unsigned int      ndigi; /* =-1 unknown Digicount; 0=no digi */
	unsigned int      ndigirepeated; /* digi[ndigirepeated] has repeated, -1==none */
	int 	      adresshash; /* not yet used: only from and to, including SSID */
	int 	      pathhash;   /* not yet used: from, to and digipeater */
	int 	      fFmCallIsLower; /* sort criteria */
	struct t_qso      *pQSO;  /* pointer to QSO */
    /* ctrlinfo */
	int  fDama;    /* DAMA - Bit set? */
	int  fExtseq;  /* extended Sequenzno. in use? */
	int  nr, ns;
	int  frametype; /* Frametype (I,C,... */
	int  cmdrsp;    /* command or respond */
	int  pf;	    /* normal or poll/final  */
	int  pid;	    /* pid; -1 if no pid availble */
	int  totlength;
	int  protType;   /* not yet used: type of  struct pProtData points to */
	void *pProtData; /* not yet used: Pointer to next decoded packet-struct */
	int  datalength;
	char *pdata;
	int  datacrc;    /* for framecollector */
};

int  calc_abincrc(char *buf, int n, unsigned int crc);


/*----------------------------------------------------------------------*/

struct t_client {
    int socket;   /* The connection to the client */
    /* struct sockaddr conn_addr; */
    /* int conn_addrlen; */
    int prot;	  /* Welches Protokol auf dem Socket ? */
#define cCLIENT_PROT_PREFIXED	1
#define cCLIENT_PROT_PLAIN_TEXT 2
#define cCLIENT_PROT_PLAIN_BUF	3
    int fShow;		/* Bool: show all strings; see ClientEnable() */
    int fRawPacket;	/* Bool: transfer _all_ recvd packets raw to the client */
    int fMoni;		/* Bool: Ist der Monitor für diesen Client aktiv? */
    int fTimestamp;	/* timestamp übertragen? */
    int fOnlyHeaders;	/* show only Packetheaders */
    int fOnlyIFrames;	/* show only i-Frames */
    int fOnlyIP;	/* show only IP-Frames */
    int fdxclusterspy;	/* Monitors DX de ... */
    int fOfferNewQSO;	 /* announce new detected QSOs */
    int fOfferAllNewQSO; /* announce new detected QSOs, even if the otherqso has already been offerd */
    int fAutoSpyOtherID; /* Automagicaly spy the other qso too, if it is availble */
    char *sSpyFilter;
    char *sHeaderFilter;
};

extern struct t_client client[MAXCLIENTS];

/* In monixd.c */
void tcprint(int dtype, char *str, int size);
void startSpyNewQSO( struct t_qso* );
#define ASCII		0
#define HEX		1
#define READABLE	2
void data_dump(char *, unsigned char *, int, int);
void hex_dump(char *, unsigned char *data, int length);
void ascii_dump(char *, unsigned char *data, int length);
void readable_dump(char *, unsigned char *data, int length);
void ai_dump(char *, unsigned char *data, int length) ;

void lwriteAll(int dtype, const void *buf, size_t size);
void lwrite(int iClient, int dtype, const void *buf, size_t size);
char *lprintf(int dtype, char *fmt, ...);
void lprintfClient(int iClient, int dtype, char *fmt, ...);
void lwriteSpy(struct t_qso*, const void *, size_t);

#if STATICLINK
int initDaemon(void);
#endif

#define ceONLYINFO 1
#define ceONLYIP   2
void clientEnablement( int why, int enable );

/* In mheard.c */
void tryspy(struct t_ax25packet* );
void mheard_init(void);
void sendMHeard(int);
void sendCallMHeard(int, char*);
void sendQSOMHeard(int, char*);
void doCallMheard(struct t_ax25packet*);
void tryspydxcluster(struct t_ax25packet*);
struct t_qso* doQSOMheard(struct t_ax25packet* pax25);
void qsoclientIndexDeleted( int );
void qsoclientIndexChanged( int, int );



char *ax25packet_dump(struct t_ax25packet *pax25packet, int dumpstyle, int displayformat);
struct t_ax25packet *ax25_decode( struct t_ax25packet *ax25packet , unsigned char *data, int length);

char *pax25(char *, unsigned char *);

/* In ipdump.c */
void ip_dump(char *, unsigned char *, int, int);

/* In nrdump.c */
void netrom_dump(char *, unsigned char *data, int length, int dumpstyle, int pkttype);

void mprintf(char *accumStr, int dtype, char *fmt, ...);

#if 0

/* ax25dump.c */
void ax25_dump( struct t_ax25packet*, unsigned char *, int, int);

void delQSO( struct t_qso * );
struct t_qso* searchQSOFromQSOid( t_qsoid );

/* in spy.c */
void foundSpyQSO( int, struct t_qso*);
void startSpyNewQSO( struct t_qso*  );
int startSpyNewClientQsoId( int , int );
int stopSpyClientQsoId( int , int );
void startSpyNewClient( int , char* );

/* crc.c */
void init_crc(void);
int calc_abincrc(char* , int , unsigned int );

/* In kissdump.c */
void kiss_dump(struct t_ax25packet*, unsigned char *, int, int);
void dev_dump( struct ifreq *, unsigned char *data, int length);


/* In arpdump.c */
void arp_dump(unsigned char *, int);

/* In icmpdump.c */
void icmp_dump(unsigned char *, int, int);

/* In udpdump.c */
void udp_dump(unsigned char *, int, int);

/* In dnsdump.c */
void dns_dump(unsigned char *, int);

/* In tcpdump.c */
void tcp_dump(unsigned char *, int, int);

/* In rspfdump.c */
void rspf_dump(unsigned char *, int);

/* In ripdump.c */
void rip_dump(unsigned char *, int);

/* In rosedump.c */
void rose_dump(unsigned char *, int, int);

/* In flexnetdump.c */
void flexnet_dump(unsigned char *, int, int);

#endif /* #if 0 */