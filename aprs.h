/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */

/* dantracker aprs tracker
 *
 * Copyright 2012 Dan Smith <dsmith@danplanet.com>
 * Copyright 2013-2014 Basil Gunn
 */

#ifndef __APRS__H
#define __APRS__H

#include <sys/socket.h>
#include <sys/un.h>

#ifndef CONSOLE_SPY
#include <fap.h>
#include <iniparser.h>
#endif /* NOT CONSOLE_SPY */
#include <stdbool.h>
#include <inttypes.h>
#ifdef HAVE_GPSD_LIB
#include <gps.h>
#endif /* HAVE_GPSD_LIB */
#include "nmea.h"

#define TRACKER_MAJOR_VERSION 1
#define TRACKER_MINOR_VERSION 2
#define TRACKER_BUILD 1

#define MAX_AX25_DIGIS 8        /* Maximum number of digipeaters: */
#define MAX_APRS_MSG_LEN (67)
#define MAX_CALLSIGN  9
#define MAX_MSGID     5
#define KEEP_MESSAGES 8 /* needs to be a power of 2 */
#define MAX_KEY_LEN 16
#define MAX_SUBST_LINE 1024
#define MAX_OUTSTANDING_MSGS 8  /* needs to be a power of 2 */

/* While stationary GPS regularly reported a speed of 1.04 mph
 *  - define speed that is criteria for stationary */
#define MIN_GPS_SPEED (3.0)

#define KEEP_PACKETS 8
#define KEEP_POSITS  4

#define DO_TYPE_NONE 0
#define DO_TYPE_WX   1
#define DO_TYPE_PHG  2

#define TZ_OFFSET (-8)

#define MIC_E_DEST_ADDR_LEN (7)

#define APRS_DATATYPE_POS_NO_TIME_NO_MSG '!'
#define APRS_DATATYPE_POS_NO_TIME_WITH_MSG '='
#define APRS_DATATYPE_POS_WITH_TIME_WITH_MSG '@'
#define APRS_DATATYPE_POS_WITH_TIME_NO_MSG '/'
#define APRS_DATATYPE_CURRENT_MIC_E_DATA '`'
#define APRS_DATATYPE_STATUS '>'

#ifdef DEBUG
#define pr_debug(format, ...) fprintf (stderr, "DEBUG: "format, ## __VA_ARGS__)
#else
#define pr_debug(format, ...)
#endif

/* Define config bits for console_display_filter */
#define CONSOLE_DISPLAY_ALL    (0xff)
#define CONSOLE_DISPLAY_WX     (0x01)
#define CONSOLE_DISPLAY_MSG    (0x02) /* display APRS messages */
#define CONSOLE_DISPLAY_PKTOUT (0x04) /* display out going packets */
#define CONSOLE_DISPLAY_FAPERR (0x08) /* Display FAP error packets */
#define CONSOLE_DISPLAY_DEBUG  (0x80)


typedef enum {
        KEY_TYPE_STATIC,
        KEY_TYPE_VARIABLE
} key_type_t;

typedef struct key_subst {
        char *key_val;
        int  key_type;
} key_subst_t;

extern key_subst_t keys[];

/* APRS received message */
typedef struct aprsmsg {
        bool acked;
        time_t timestamp;
        char *message_id;
        char srccall[MAX_CALLSIGN+1];
        char dstcall[MAX_CALLSIGN+1];
        char *message;
} aprsmsg_t;

/*
 * Keep track of outstanding ack requests for sent messages
 */
typedef struct ack_outstanding {
        bool needs_ack;
        time_t timestamp;
        int timer_fd;
        struct itimerspec *itimerspec;
        int ack_msgid;
        int ack_retry_count;
        char *aprs_msg;
} ack_outstand_t;

/* AX.25 Packet Device Filter */
typedef enum {
        AX25_PKT_DEVICE_FILTER_UNDEFINED,
        AX25_PKT_DEVICE_FILTER_OFF,
        AX25_PKT_DEVICE_FILTER_ON
} ax25_pkt_filter_t;

struct smart_beacon_point {
        float int_sec;
        float speed;
};

/* for decompressing winlink packets for spy output */
typedef struct prop_ctrl {
        int b2f_state;
        int b2f_stx_size;
        bool next_pkt_stx;
        bool next_pkt_continue;
        int prop_pending_cnt;
        int excess_partial_size;
        int need_partial_size;
        int cksum;
        uint8_t *partial_buf;
        struct buffer *compressed;
        struct buffer *decomp;
} prop_ctrl_t;

typedef enum  {
        GPS_TYPE_FAKE,
        GPS_TYPE_SERIAL,
        GPS_TYPE_GPSD,
        GPS_TYPE_UNDEFINED = -1
} gps_type_t;

/* Bit definitions for spy display format (spy_format) */
#define SPY_FORMAT_TIME_ENABLE          0x01
#define SPY_FORMAT_TIME_RESOLUTION      0x02
#define SPY_FORMAT_PORT_ENABLE          0x04
#define SPY_FORMAT_PKTLEN_ENABLE        0x08

struct state {
        struct {
                char *tnc;
                int tnc_rate;
                char *gps;
                int gps_rate;
                char *tel;
                int tel_rate;
                char *net;

                char *tnc_type;
                char *gps_type;
                int gps_type_int;
                int gps_time_update;

                char *aprsis_server_host_addr;
                int aprsis_server_port;
                unsigned int aprsis_range;
                char *aprsis_filter;

                int ax25_pkt_device_filter;
                char *ax25_port;
                char *aprs_path;
                int testing;
                int verbose;
                char *icon;

                char *digi_path;

                int power;
                int height;
                int gain;
                int directivity;

                int atrest_rate;
                struct smart_beacon_point sb_low;
                struct smart_beacon_point sb_high;
                int course_change_min;
                int course_change_slope;
                int after_stop;

                unsigned int do_types;

                char **comments;
                int comments_count;

                char *config;

                double static_lat, static_lon, static_alt;
                double static_spd, static_crs;

                char *init_kiss_cmd;

                char *digi_alias;
                int digi_enabled;
                int digi_append;
                int digi_delay;
                union {
                        struct sockaddr_un afunix;
                        struct sockaddr afinet;
                } display_to;

                char *ui_sock_path;
                char *ui_host_name;
                unsigned int ui_inet_port;

                bool metric_units;
                bool aprs_message_ack;
#ifndef CONSOLE_SPY
                dictionary *ini_dict;
#endif  /* NOT CONSOLE_SPY */

                int spy_format;

        } conf;

        prop_ctrl_t prop_ctrl;                 /* for decompressing winlink packets */
        struct posit mypos[KEEP_POSITS];
        int mypos_idx;

        struct posit last_beacon_pos;

        struct {
                double temp1;
                double voltage;

                time_t last_tel_beacon;
                time_t last_tel;
        } tel;

        char *mycall;
        int myssid;
        char *basecall;

        char *ax25_dev;                 /* device name */
        char *ax25_srcaddr;             /* callsign assigned to port name */
        int ax25_recvproto;             /* Protocol to use for receive ETH_P_ALL or ETH_P_AX25 */
        int ax25_tx_sock;

        int tncfd;
        int gpsfd;
        int telfd;
        int dspfd;

        aprsmsg_t *msghist[KEEP_MESSAGES];
        int msghist_idx;

        int outstanding_ack_timer_count;
        ack_outstand_t ackout[MAX_OUTSTANDING_MSGS];
        int ack_msgid;

#ifndef CONSOLE_SPY
        fap_packet_t *last_packet; /* In case we don't store it below */
        fap_packet_t *recent[KEEP_PACKETS];
        fap_packet_t *last_wx;
#endif  /* NOT CONSOLE_SPY */
        int recent_idx;
        int disp_idx;

#ifdef HAVE_GPSD_LIB
        struct gps_data_t gpsdata;
#endif /* HAVE_GPSD_LIB */
        char gps_buffer[4096];
        int gps_idx;            /* index into gps_buffer for nmea string capture */
        time_t last_gps_update;
        time_t last_gps_data;
        time_t last_beacon;
        time_t last_time_set;
        time_t last_moving;
        time_t last_status;

        int comment_idx;
        int other_beacon_idx;

        uint8_t digi_quality;
        struct {
                unsigned long inPktCount;
                unsigned long outPktCount;
                unsigned long retryPktCount;
                unsigned long inMsgCount;
                unsigned long inWxCount;
                unsigned long encapCount;
                unsigned long fapErrCount;
        } stats;

        struct {
                const char *record_pkt_filename;
                const char *playback_pkt_filename;
                int playback_time_scale;
                int console_display_filter;
                int verbose_level;
                bool parse_ini_test;
                bool display_fap_enable;
                bool display_spy_enable;
                bool console_spy_enable;
                bool b2f_decode_enable;
        } debug;
};

/*
 * defines the header written before each canned packet
 */
typedef struct recpkt {
        uint32_t currtime;
        int size;
} recpkt_t;

#ifndef CONSOLE_SPY

void display_fap_pkt(char *string, fap_packet_t *fap);
int store_packet(struct state *state, fap_packet_t *fap);
fap_packet_t *dan_parseaprs(char *string, int len, int isax25);
void display_fap_error(char *string, struct state *state, fap_packet_t *fap);
/* Debug only */
void fap_free_wrapper(char *dispStr,  struct state *state, fap_packet_t *fap);

#endif /* NOT CONSOLE_SPY */

void handle_ax25_pkt(struct state *state, struct sockaddr *sa, unsigned char *buf, int size);

int parse_ini(char *filename, struct state *state);
int parse_opts(int argc, char **argv, struct state *state);
void show_version(void);
void parse_incoming_packet(struct state *state, char *packet, int len, int isax25);
void ini_cleanup(struct state *state);
int lookup_host(struct state *state);
char *process_subst(struct state *state, char *src);
int _ui_send(struct state *state, const char *name, const char *value);
int fake_gps_data(struct state *state);

bool send_kiss_beacon(int fd, char *packet);
bool send_net_beacon(int fd, char *packet);
bool send_beacon(struct state *state, char *packet);

#endif /* APRS_H */
