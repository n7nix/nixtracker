/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */
/* Copyright 2012 Dan Smith <dsmith@danplanet.com> */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <termios.h>
#include <sys/un.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <json/json.h>
#include <netinet/if_ether.h> /* only for ETH_P_AX25, canned packets */

#include "util.h"
#include "serial.h"
#include "aprs.h"
#include "ui.h"
#include "aprs-is.h"
#include "aprs-ax25.h"
#include "aprs-msg.h"

/* #define DEBUG_GPS 1 */

/* For USB serial port GPS devices */
/* #define NMEA_PREFIX "$GP" */

/* for NW Digital Radio DRAWS GPS */
#define NMEA_PREFIX "$GN"
#define NMEA_GGA "GGA"
#define NMEA_RMC "RMC"
#define NMEA_GSA "GSA"
const char *nmea_gga=NMEA_PREFIX NMEA_GGA;
const char *nmea_rmc=NMEA_PREFIX NMEA_RMC;
const char *nmea_gsa=NMEA_PREFIX NMEA_GSA;

extern char *__progname;
extern const char *getprogname(void);
time_t convrt_gps_to_epoch(struct state *state);

void fap_free_wrapper(char *dispStr,  struct state *state, fap_packet_t *fap)
{
        if(fap == NULL) {
                printf("fap_free: Trying to free a NULL fap ptr at: %s\n", dispStr);
        } else {
                fap_free(fap);
        }
}

void display_fap_error(char *string, struct state *state, fap_packet_t *fap)
{
        char errbuf[1024];

        fap_explain_error(*fap->error_code, errbuf);
        printf("\n%s FAP Error: %s: %i: %s\n",
               time2str(fap->timestamp, 0),
               string,
               *fap->error_code,
               errbuf);

              if(state->debug.console_display_filter & CONSOLE_DISPLAY_FAPERR) {
                      printf("Error Pkt: %s\n", fap->orig_packet);
              }

        state->stats.fapErrCount++;
}

void display_fap_pkt(char *string, fap_packet_t *fap)
{
        printf("%s: call %p, dest %p, dest call %p,  time %p, type %p, pkt %p\n",
               string,
               fap->src_callsign,
               fap->destination,
               fap->dst_callsign,
               fap->timestamp,
               fap->type,
               fap->orig_packet);
        fflush(stdout);
}

void dump_ui_msg(struct ui_msg *ui_msg)
{
        int i;
        unsigned char *buf = (unsigned char *)ui_msg;

        printf("ui message [%zu]: ", sizeof(struct ui_msg));

        for(i=0; i < ui_msg->length; i++) {
                printf("%02x ", *buf);
                buf++;
        }
        printf("\n");
}


bool send_kiss_beacon(int fd, char *packet)
{
        char buf[512];
        int ret;
        unsigned int len = sizeof(buf);

        printf("Sending KISS Packet: %s\n", packet);

        ret = fap_tnc2_to_kiss(packet, strlen(packet), 0, buf, &len);
        if (!ret) {
                printf("Failed to make beacon KISS packet\n");
                return 1;
        }
        return write(fd, buf, len) == len;
}

bool send_net_beacon(int fd, char *packet)
{
        int ret = 0;

        ret = write(fd, packet, strlen(packet));
        ret += write(fd, "\r\n", 2);

        return ret == (strlen(packet) + 2);
}

/*
 * Send beacon packet out one of the interfaces serial, ax.25 or
 * network stack
 */
bool send_beacon(struct state *state, char *packet)
{
        /* bump transmit packet count */
        state->stats.outPktCount++;

        if(state->debug.console_display_filter & CONSOLE_DISPLAY_PKTOUT) {
                printf("\n%s Packet out: len: %zu, src: %s, pkt: %s\n",
                       time2str(NULL, 0), strlen(packet), state->ax25_srcaddr, packet);
        }
        _ui_send(state, "I_TX", "1000");

        if (STREQ(state->conf.tnc_type, "KISS")) {
                return send_kiss_beacon(state->tncfd, packet);
        } else if (STREQ(state->conf.tnc_type, "AX25")) {
#ifdef HAVE_AX25_TRUE
                return send_ax25_beacon(state, packet);
#else
                printf("%s:%s(): tnc type has been configured to use AX.25 but AX.25 lib has not been installed.\n",
                       __FILE__, __FUNCTION__);
                return 0;
#endif /* #ifdef HAVE_AX25_TRUE */

        } else {
                return send_net_beacon(state->tncfd, packet);
        }
}

int _ui_send(struct state *state, const char *name, const char *value)
{
        int ret=0;
        int *fd = &state->dspfd;

        /* If socket is not valid try connecting */
        if (*fd < 0) {
                *fd = ui_connect(state);
        }

        /* check for a valid socket */
        if( *fd > 0) {
                ret = ui_send(*fd, name, value);
                if (ret < 0) {
                        printf("%s:%s(): closing file descriptor %d due to error\n",
                               __FILE__, __FUNCTION__, *fd);

                        close(*fd);
                        *fd = -1;
                }
        }

        return ret;
}

fap_packet_t *dan_parseaprs(char *string, int len, int isax25)
{
        fap_packet_t *fap;
        char *tmp;

        fap = fap_parseaprs(string, len, isax25);

        if (fap->error_code)
                return fap;

        if (fap->comment) {
                tmp = get_escaped_string(fap->comment);
                free(fap->comment);
                fap->comment = tmp;
        }
        if (fap->status) {
                tmp = get_escaped_string(fap->status);
                free(fap->status);
                fap->status = tmp;
        }

        return fap;
}

char *format_time(time_t t)
{
        static char str[32];

        if (t > (3600 * 24))
                snprintf(str, sizeof(str), "%lud%luh",
                         t / (3600 * 24),
                         t % (3600 * 24));
        else if (t > 3600)
                snprintf(str, sizeof(str), "%luh%lum", t / 3600, (t % 3600) / 60);
        else if (t > 60)
                if (t % 60)
                        snprintf(str, sizeof(str), "%lum%lus", t / 60, t % 60);
                else
                        snprintf(str, sizeof(str), "%lu min", t / 60);
        else
                snprintf(str, sizeof(str), "%lu sec", t);

        return str;
}

const char *format_temp(struct state *state, const char *format, float celsius)
{
        static char str[10];
        float _temp = state->conf.metric_units ? celsius : C_TO_F(celsius);
        char  *unit = state->conf.metric_units ? "C" : "F";

        snprintf(str, sizeof(str), format, _temp, unit);

        return str;
}

const char *format_distance(struct state *state, const char *format, float km)
{
        static char str[10];
        float _dist = state->conf.metric_units ? km : KPH_TO_MPH(km);
        char  *unit = state->conf.metric_units ? "km" : "mi";

        snprintf(str, sizeof(str), format, _dist, unit);

        return str;
}

const char *format_speed(struct state *state, const char *format, double kph)
{
        static char str[10];
        float _speed = state->conf.metric_units ? kph : KPH_TO_MPH(kph);
        char  *unit = state->conf.metric_units ? "km/h" : "MPH";

        snprintf(str, sizeof(str), format, _speed, unit);

        return str;
}

const char *format_wind_speed(struct state *state, const char *format, double ms)
{
        static char str[10];
        float _speed = state->conf.metric_units ? ms : MS_TO_MPH(ms);
        char  *unit = state->conf.metric_units ? "m/s" : "MPH";

        snprintf(str, sizeof(str), format, _speed, unit);

        return str;
}

const char *format_altitude(struct state *state, const char *format, double ft)
{
        static char str[25];
        float _altitude = state->conf.metric_units ? FT_TO_M(ft) : ft;
        char  *unit = state->conf.metric_units ? "m" : "FT";

        snprintf(str, sizeof(str), format, _altitude, unit);

        return str;
}

const char *format_altitude_agl(struct state *state, const char *format, double ft)
{
        static char str[25];
        float _altitude = state->conf.metric_units ? FT_TO_M(ft) : ft;
        char  *unit = state->conf.metric_units ? "m" : "FT";

        snprintf(str, sizeof(str), format, _altitude, unit);

        return str;
}

const char *format_altitude_gps(struct state *state, const char *format, double meters)
{
        static char str[25];
        float _altitude = state->conf.metric_units ? meters :  M_TO_FT(meters);
        char  *unit = state->conf.metric_units ? "m" : "FT";

        snprintf(str, sizeof(str), format, _altitude, unit);

        return str;
}

const char *format_distance_to_posit(struct state *state, fap_packet_t *fap)
{
        const char *dist;
        struct posit *mypos = MYPOS(state);

        if (fap->latitude && fap->longitude) {
                float _dist = fap_distance(mypos->lon, mypos->lat,
                                           *fap->longitude,
                                           *fap->latitude);
                if (_dist < 100.0)
                        dist = format_distance(state, "%5.1f%s", _dist);
                else
                        dist = format_distance(state, "%4.0f%s", _dist);
        } else
                dist = "";

        return dist;
}

char *wx_get_rain(struct state *state, fap_packet_t *_fap)
{
        char *rain = NULL;
        fap_wx_report_t *fap = _fap->wx_report;
        int metric = state->conf.metric_units;

        if (fap->rain_1h && fap->rain_24h &&
            (*fap->rain_1h > 0) && (*fap->rain_24h > 0))
                asprintf(&rain, "Rain %.2fh%.2fd ",
                         metric ? *fap->rain_1h  : MM_TO_IN(*fap->rain_1h),
                         metric ? *fap->rain_24h : MM_TO_IN(*fap->rain_24h));
        else if (fap->rain_1h && (*fap->rain_1h > 0))
                asprintf(&rain, "Rain %.2fh ",
                         metric ? *fap->rain_1h : MM_TO_IN(*fap->rain_1h));
        else if (fap->rain_24h && (*fap->rain_24h > 0))
                asprintf(&rain, "Rain %.2fd ",
                         metric ? *fap->rain_24h : MM_TO_IN(*fap->rain_24h));

        return rain;
}

char *wx_get_wind(struct state *state, fap_packet_t *_fap)
{
        char *wind = NULL;
        fap_wx_report_t *fap = _fap->wx_report;

        int metric = state->conf.metric_units;

        if (fap->wind_gust && fap->wind_dir && fap->wind_speed &&
            ((*fap->wind_gust > 0) || (*fap->wind_speed > 0)))
                asprintf(&wind, "Wind %s %.0f/%.0f%s ",
                         direction(*fap->wind_dir),
                         metric ? *fap->wind_speed : MS_TO_MPH(*fap->wind_speed),
                         metric ? *fap->wind_gust  : MS_TO_MPH(*fap->wind_gust),
                         metric ? "m/s" : "mph");
        else if (fap->wind_dir && fap->wind_speed && (*fap->wind_speed > 0))
                asprintf(&wind, "Wind %s %.0f %s ",
                         direction(*fap->wind_dir),
                         metric ? *fap->wind_speed : MS_TO_MPH(*fap->wind_speed),
                         metric ? "m/s" : "mph");

        return wind;
}

char *wx_get_humid(fap_packet_t *_fap)
{
        char *humid = NULL;
        fap_wx_report_t *fap = _fap->wx_report;

        if (fap->humidity)
                asprintf(&humid, "%2i%% ", *fap->humidity);

        return humid;
}

char *wx_get_temp(struct state *state, fap_packet_t *_fap)
{
        char *temp = NULL;
        fap_wx_report_t *fap = _fap->wx_report;

        if (fap->temp) {
                asprintf(&temp, "%s ",
                         format_temp(state, "%.0f%s", *fap->temp));
        }
        return temp;
}

char *wx_get_report(struct state *state, fap_packet_t *fap)
{
        char *wind, *temp, *rain, *humid;
        char *report = NULL;

        /* Check for a pointer to the parsed weather report */
        if(fap->wx_report != NULL) {
                wind = wx_get_wind(state, fap);
                temp = wx_get_temp(state, fap);
                rain = wx_get_rain(state, fap);
                humid = wx_get_humid(fap);

                asprintf(&report, "%s%s%s%s",
                         wind ? wind : "",
                         temp ? temp : "",
                         rain ? rain : "",
                         humid ? humid : "");

                free(wind);
                free(rain);
                free(temp);
                free(humid);
        } else {
                asprintf(&report, "NULL wx_report");
        }

        return report;
}

void str_subst(char *string, char c, char s)
{
        char *ptr;

        if (!string)
                return;

        while ((ptr = strchr(string, c)))
                *ptr = s;
}

void update_recent_wx(struct state *state)
{
        char *dist = NULL;
        int ret;
        char *report;
        struct posit *mypos = MYPOS(state);
        float distance;
        const char *dir;
        fap_packet_t *fap = state->last_wx;


        if (fap == NULL) {
                _ui_send(state, "WX_DATA", "");
                _ui_send(state, "WX_DIST", "");
                _ui_send(state, "WX_NAME", "");
                _ui_send(state, "WX_ICON", "/W");
                _ui_send(state, "WX_COMMENT", "No weather received yet");
                return;
        }

        if (fap->latitude && fap->longitude) {
                distance = KPH_TO_MPH(fap_distance(mypos->lon, mypos->lat,
                        *fap->longitude,
                        *fap->latitude));
                dir = direction(get_direction(mypos->lon, mypos->lat,
                                              *fap->longitude,
                                              *fap->latitude));
        } else {
                distance = -1;
        }

        report = wx_get_report(state, fap);

        _ui_send(state, "WX_DATA", report);

        if (distance < 0)
                ret = asprintf(&dist, "(%s ago)",
                               format_time(time(NULL) - *fap->timestamp));
        else
                ret = asprintf(&dist, "%s %s (%s ago)",
                               format_distance_to_posit(state, fap),
                               dir,
                               format_time(time(NULL) - *fap->timestamp));
        if (ret != -1) {
                _ui_send(state, "WX_DIST", dist);
                free(dist);
        }
        _ui_send(state, "WX_NAME", OBJNAME(fap));
        _ui_send(state, "WX_ICON", "/W");

        str_subst(fap->comment, '\n', ' ');
        str_subst(fap->comment, '\r', ' ');
        str_subst(fap->status, '\n', ' ');
        str_subst(fap->status, '\r', ' ');

        if (fap->comment_len) {
                char buf[512];

                strncpy(buf, fap->comment, fap->comment_len);
                buf[fap->comment_len] = 0;
                _ui_send(state, "WX_COMMENT", buf);
        } else if (fap->status_len) {
                char buf[512];

                strncpy(buf, fap->status, fap->status_len);
                buf[fap->comment_len] = 0;
                _ui_send(state, "WX_COMMENT", buf);
        } else {
                _ui_send(state, "WX_COMMENT", "");
        }

        free(report);
}

void display_wx(struct state *state, fap_packet_t *fap)
{
        struct posit *mypos = MYPOS(state);
        float distance = -1, last_distance = -1;
        char *report;

        state->stats.inWxCount++;

        if (fap->latitude && fap->longitude)
                distance = KPH_TO_MPH(fap_distance(mypos->lon, mypos->lat,
                        *fap->longitude,
                        *fap->latitude));

        if (state->last_wx &&
            state->last_wx->latitude && state->last_wx->longitude)
                last_distance = \
                                KPH_TO_MPH(fap_distance(mypos->lon, mypos->lat,
                        *state->last_wx->longitude,
                        *state->last_wx->latitude));
        else
                last_distance = 9999999.0; /* Very far away, if unknown */

        /* If the last-retained weather beacon is older than 30
         * minutes, farther away than the just-received beacon, or the
         * same as the just-received beacon, then replace it. Oh, but not if
         * it's OUR weather beacon.
         */
        if (!STREQ(OBJNAME(fap), state->mycall) ||
            !state->last_wx ||
            STREQ(OBJNAME(state->last_wx), OBJNAME(fap)) ||
            ((time(NULL) - *state->last_wx->timestamp) > 1800) ||
            ((distance > 0) && (distance <= last_distance))) {
#ifdef DEBUG_VERBOSE
                printf("  weather no pos: dist %.1f <= %.1f, delta %lu sec\n",
                       distance,
                       last_distance,
                       state->last_wx ? time(NULL) - *state->last_wx->timestamp : 0);
#endif /*  DEBUG_VERBOSE */
                if(state->last_wx != NULL) {
                        fap_free_wrapper("Wx", state, state->last_wx);
                        state->last_wx = NULL;
                }
                if(state->debug.console_display_filter & CONSOLE_DISPLAY_WX) {
                        printf("display_wx(%zu): %s\n",
                               strlen(fap->orig_packet), fap->orig_packet);
                }

                state->last_wx = dan_parseaprs(fap->orig_packet,
                                               strlen(fap->orig_packet), 0);
                if( (state->last_wx->timestamp = malloc(sizeof(*fap->timestamp))) == NULL) {
                        printf("%s: malloc error: %s\n",
                              __FUNCTION__, strerror(errno));
                } else {
                        time(state->last_wx->timestamp);
                }
                update_recent_wx(state);
        }

        report = wx_get_report(state, fap);
        _ui_send(state, "AI_COMMENT", report);

        /* Comment is used for larger WX report, so report the
         * comment (if any) in the smaller course field
         */
        if (fap->comment_len) {
                char buf[512];

                strncpy(buf, fap->comment, fap->comment_len);
                buf[fap->comment_len] = 0;
                _ui_send(state, "AI_COURSE", buf);
        } else if (fap->status_len) {
                char buf[512];

                strncpy(buf, fap->status, fap->status_len);
                buf[fap->comment_len] = 0;
                _ui_send(state, "AI_COURSE", buf);
        } else {
                _ui_send(state, "AI_COURSE", "");
        }

        free(report);
}

#define TELEMSTRBUFSIZE 512
void display_telemetry(struct state *state, fap_telemetry_t *fap)
{
        char *data = NULL;
        char tmpbuf[64];
        int ret;
        int telem_strsize = 0;

        ret = asprintf(&data, "Telemetry #%03i", *fap->seq);
        _ui_send(state, "AI_COURSE", ret == -1 ? "" : data);
        free(data);

        /* Malloc a buffer large enought to hold the 5 telemetry values
         * plus the bits, see fap_telemetry-t
         */
        if( (data = malloc(TELEMSTRBUFSIZE)) == NULL) {
                printf("%s: malloc error: %s\n",
                       __FUNCTION__, strerror(errno));
                return;
        }
        data[0] = '\0';

        if(fap->val1 != NULL) {
                telem_strsize += sprintf(tmpbuf, "%.0f ", *fap->val1);
                strcat(data, tmpbuf);
        }

        if(fap->val2 != NULL) {
                telem_strsize += sprintf(tmpbuf, "%.0f ", *fap->val2);
                strcat(data, tmpbuf);
        }
        if(fap->val3 != NULL) {
                telem_strsize += sprintf(tmpbuf, "%.0f ", *fap->val3);
                strcat(data, tmpbuf);
        }
        if(fap->val4 != NULL) {
                telem_strsize += sprintf(tmpbuf, "%.0f ", *fap->val4);
                strcat(data, tmpbuf);
        }
        if(fap->val5 != NULL) {
                telem_strsize += sprintf(tmpbuf, "%.0f ", *fap->val5);
                strcat(data, tmpbuf);
        }

        telem_strsize += sprintf(tmpbuf, "%8.8s", fap->bits);
        if(telem_strsize > TELEMSTRBUFSIZE) {
                printf("%s: Telemetry string over ran buffer\n",
                       __FUNCTION__);
                return;
        }
        strcat(data, tmpbuf);
        _ui_send(state, "AI_COMMENT", ret == -1 ? "" : data);
        free(data);

        _ui_send(state, "AI_ICON", "/Q");
}

void display_phg(struct state *state, fap_packet_t *fap)
{
        int power, gain, dir;
        char height;
        int ret;
        char *buf = NULL;

        ret = sscanf(fap->phg, "%1d%c%1d%1d",
                     &power, &height, &gain, &dir);
        if (ret != 4) {
                _ui_send(state, "AI_COURSE", "(Broken PHG)");
                return;
        }

        asprintf(&buf, "Power %iW at %s (%idB gain @ %s)",
                 power*power,
                 format_altitude_agl(state, "%.0f%s", pow(2, height - '0') * 10),
                 gain,
                 dir ? direction(dir) : "omni");
        _ui_send(state, "AI_COMMENT", buf);
        free(buf);

        if (fap->comment) {
                buf = strndup(fap->comment, fap->comment_len);
                _ui_send(state, "AI_COURSE", buf);
                free(buf);
        } else
                _ui_send(state, "AI_COURSE", "");
}

void display_posit(struct state *state, fap_packet_t *fap, bool isnew)
{
        char buf[512];

        if (fap->speed && fap->course && (*fap->speed > 0.0) && fap->altitude) {
                snprintf(buf, sizeof(buf), "%s %2s @ %s",
                         format_speed(state, "%.0f %s", *fap->speed),
                         direction(*fap->course),
                         format_altitude(state, "%.0f %s", *fap->altitude));
                _ui_send(state, "AI_COURSE", buf);
        } else if (fap->speed && fap->course && (*fap->speed > 0.0)) {
                snprintf(buf, sizeof(buf), "%s %2s",
                         format_speed(state, "%.0f %s", *fap->speed),
                         direction(*fap->course));
                _ui_send(state, "AI_COURSE", buf);
        } else if (isnew)
                _ui_send(state, "AI_COURSE", "");

        if (fap->type && (*fap->type == fapSTATUS)) {
                strncpy(buf, fap->status, fap->status_len);
                buf[fap->status_len] = 0;
                _ui_send(state, "AI_COMMENT", buf);
        } else if (fap->format && (*fap->format == fapPOS_MICE)) {
                fap_mice_mbits_to_message(fap->messagebits, buf);
                buf[0] = toupper(buf[0]);
                _ui_send(state, "AI_COMMENT", buf);
        } else if (fap->comment_len) {
                strncpy(buf, fap->comment, fap->comment_len);
                buf[fap->comment_len] = 0;
                _ui_send(state, "AI_COMMENT", buf);
        } else if (isnew)
                _ui_send(state, "AI_COMMENT", "");
}

const char *find_heard_via(fap_packet_t *fap)
{
        int i;
        int heard_index = -1;
        const char *heard_via;

        for (i = 0; i < fap->path_len; i++)
                if (strchr(fap->path[i], '*'))
                        heard_index = i;

        if (heard_index == -1)
                return "Direct";
        heard_via = fap->path[heard_index];

        if ((strstr(heard_via, "WIDE") == heard_via) &&
            (heard_index > 0))
                heard_via = fap->path[heard_index - 1];

        return heard_via;
}

void display_dist_and_dir(struct state *state, fap_packet_t *fap)
{
        char buf[512] = "";
        char via[32] = "Direct";
        const char *dist;
        struct posit *mypos = MYPOS(state);

        strcpy(via, find_heard_via(fap));
        if (strchr(via, '*'))
                *strchr(via, '*') = 0; /* Nuke the asterisk */

        dist = format_distance_to_posit(state, fap);

        if (STREQ(fap->src_callsign, state->mycall))
                snprintf(buf, sizeof(buf), "via %s", via);
        else if (fap->latitude && fap->longitude)
                snprintf(buf, sizeof(buf), "%s %2s <small>via %s</small>",
                         dist,
                         direction(get_direction(mypos->lon, mypos->lat,
                        *fap->longitude,
                        *fap->latitude)),
                         via);
        else if (fap->latitude && fap->longitude && fap->altitude)
                snprintf(buf, 512, "%s %2s (%4.0f ft)",
                         dist,
                         direction(get_direction(mypos->lon, mypos->lat,
                        *fap->longitude,
                        *fap->latitude)),
                         M_TO_FT(*fap->altitude));
        _ui_send(state, "AI_DISTANCE", buf);
}

#ifdef WEBAPP
void display_statistics(struct state *state)
{
        json_object *jstats;
        json_object *jint;

        jstats = json_object_new_object();
        jint = json_object_new_int(state->stats.inPktCount);
        json_object_object_add(jstats, "inPktCount", jint);

        jint = json_object_new_int(state->stats.outPktCount);
        json_object_object_add(jstats, "outPktCount", jint);

        jint = json_object_new_int(state->stats.retryPktCount);
        json_object_object_add(jstats, "retryPktCount", jint);

        jint = json_object_new_int(state->stats.inMsgCount);
        json_object_object_add(jstats, "inMsgCount", jint);

        jint = json_object_new_int(state->stats.inWxCount);
        json_object_object_add(jstats, "inWxCount", jint);

        jint = json_object_new_int(state->stats.encapCount);
        json_object_object_add(jstats, "encapCount", jint);

        jint = json_object_new_int(state->stats.fapErrCount);
        json_object_object_add(jstats, "fapErrorCount", jint);

        _ui_send(state, "ST_APRSPKT",
                 json_object_to_json_string(jstats));

}
#endif /* WEBAPP */

void display_packet(struct state *state, fap_packet_t *fap)
{
        char buf[512];
        static char last_callsign[32] = "";
        bool isnew = false;


        if (!STREQ(OBJNAME(fap), last_callsign)) {
                isnew = true;
        }

        _ui_send(state, "AI_CALLSIGN", OBJNAME(fap));

        strncpy(last_callsign, OBJNAME(fap), MAX_CALLSIGN);
        last_callsign[31] = '\0';

        display_dist_and_dir(state, fap);

        if (fap->wx_report)
                display_wx(state, fap);
        else if (fap->telemetry)
                display_telemetry(state, fap->telemetry);
        else if (fap->phg)
                display_phg(state, fap);
#ifdef WEBAPP
        else if (fap->message) {
                display_fap_message(state, fap);  /* DEBUG only - look at message packets */
                if(isnewmessage(state, fap)) {
                        save_message(state, fap);
                        webdisplay_message(state, fap);
                }
        } else  if( (fap->type != NULL) && (*fap->type == fapTHIRD_PARTY) ) {
                webdisplay_thirdparty(state, fap);
        }

#endif /* WEBAPP */
        else {
                display_posit(state, fap, isnew);
        }

        /* Test if symbol table & symbol have been set */
        if(fap->symbol_table && fap->symbol_code) {
                /*
                 * Currently support primary & alternate symbol
                 * tables & uncompressed lat/long overlays for
                 * alternate symbol table
                 */
                if(fap->symbol_table == '/' || /* Primary symbol table */
                   fap->symbol_table == '\\' || /* Alternate symbol table */
                   (fap->symbol_table >= '0' &&  fap->symbol_table <= '9') || /* Numeric overlay alternate symbol table */
                   (fap->symbol_table >= 'A' &&  fap->symbol_table <= 'Z')    /* Alpha overlay alternate symbol table */
                  ) {
                        snprintf(buf, sizeof(buf), "%c%c", fap->symbol_table, fap->symbol_code);
                        _ui_send(state, "AI_ICON", buf);
                } else {
                        /* Take a look at the unrecognized symbol table value */
                        printf("icon: unrecognized table: %c (%02x), code: %c (%02x), packet: %s\n",
                               fap->symbol_table, fap->symbol_table,
                               fap->symbol_code, fap->symbol_code,
                               fap->orig_packet);
                }
        }
}

int stored_packet_desc(struct state *state, fap_packet_t *fap, int index,
                       double mylat, double mylon,
                       char *buf, int len)
{
        if (fap->latitude && fap->longitude) {
                const char *dirstr = direction(get_direction(mylon, mylat,
                        *fap->longitude,
                        *fap->latitude));

                snprintf(buf, len,
                         "%i:%-9s <small>%s %-2s</indexsmall>",
                         index, OBJNAME(fap),
                         format_distance(state, "%3.0f%s",
                                         fap_distance(mylon, mylat,
                                         *fap->longitude,
                                         *fap->latitude)),
                                         dirstr);
        } else if (fap->src_callsign != NULL) {
                snprintf(buf, len,
                         "%i:%-9s <small>%s</small>",
                         index, OBJNAME(fap),
                         fap->timestamp ? format_time(time(NULL) - *fap->timestamp) : "");
        }

        return 0;
}

int update_packets_ui(struct state *state)
{
        int i, j;
        char name[] = "AL_00";
        char buf[64];
        struct posit *mypos = MYPOS(state);

        if (state->last_packet && (state->disp_idx < 0))
                display_dist_and_dir(state, state->last_packet);

        for (i = KEEP_PACKETS, j = state->recent_idx + 1; i > 0; i--, j++) {
                fap_packet_t *p = state->recent[j % KEEP_PACKETS];

                sprintf(name, "AL_%02i", i-1);
                if (p)
                        stored_packet_desc(state, p, i,
                                           mypos->lat, mypos->lon,
                                           buf, sizeof(buf));
                else
                        sprintf(buf, "%i:", i);
                _ui_send(state, name, buf);
        }

        update_recent_wx(state);

        return 0;
}

/* Move packets below @index to @index */
int move_packets(struct state *state, int index)
{
        int i;
        const int max = KEEP_PACKETS;
        int end = (state->recent_idx +1 ) % max;
#if 0
        printf("move_packets index: %d [%p], end: %d, obj: %s\n",
               index, state->recent[index], end, OBJNAME(state->recent[index]) );
#endif
        fap_free_wrapper("move_packets", state, state->recent[index]);
        state->recent[index] = NULL;

        for (i = index; i != end; i -= 1) {
                /* If Zero now then KEEP-1 next */
                i = (i == 0) ? KEEP_PACKETS : i;
                state->recent[i % max] = state->recent[(i - 1) % max];
        }

        /* This made a hole at the bottom */
        state->recent[end] = NULL;

        return 0;
}

int find_packet(struct state *state, fap_packet_t *fap)
{
        int i;

        for (i = 0; i < KEEP_PACKETS; i++)
                if (state->recent[i] &&
                    STREQ(OBJNAME(state->recent[i]), OBJNAME(fap)))
                        return i;

        return -1;
}

#define SWAP_VAL(new, old, value)                               \
    do {                                                        \
            if (old->value && !new->value) {            \
                    new->value = old->value;            \
                    old->value = 0;                             \
            }                                           \
    } while (0);

int merge_packets(fap_packet_t *new, fap_packet_t *old)
{
        SWAP_VAL(new, old, speed);
        SWAP_VAL(new, old, course);
        SWAP_VAL(new, old, latitude);
        SWAP_VAL(new, old, longitude);
        SWAP_VAL(new, old, altitude);
        SWAP_VAL(new, old, symbol_table);
        SWAP_VAL(new, old, symbol_code);

        if (old->comment_len && !new->comment_len) {
                new->comment_len = old->comment_len;
                new->comment = old->comment;
                old->comment_len = 0;
                old->comment = NULL;
        }

        if (old->status_len && !new->status_len) {
                new->status_len = old->status_len;
                new->status = old->status;
                old->status_len = 0;
                old->status = NULL;
        }

        return 0;
}

int store_packet(struct state *state, fap_packet_t *fap)
{
        int i;

        /* check if fap supplied a time stamp */
        if (fap->timestamp == NULL) {
                if( (fap->timestamp = malloc(sizeof(*fap->timestamp))) == NULL) {
                        printf("%s: malloc error: %s\n",
                               __FUNCTION__, strerror(errno));
                } else {
                        time(fap->timestamp);
                }
        }

        if (state->last_packet &&
            STREQ(OBJNAME(state->last_packet), OBJNAME(fap))) {
                /* Received another packet for the latest, merge and bail */
                merge_packets(fap, state->last_packet);
                fap_free_wrapper("store_packet 1", state, state->last_packet);
                state->last_packet = NULL;
                goto out;
        }

        /* If the station has been heard, remove it from the old position
         * in the list and merge its data into the current one
         */
        i = find_packet(state, fap);
        if (i != -1) {
                merge_packets(fap, state->recent[i]);
                move_packets(state, i);
        }

        /* Note: we don't store our own packets on the list */

        if (state->last_packet &&
            !STREQ(state->last_packet->src_callsign, state->mycall)) {
                /* Push the previously-current packet onto the list */
                state->recent_idx = (state->recent_idx + 1) % KEEP_PACKETS;
                if (state->recent[state->recent_idx]) {
                        fap_free_wrapper("store_packet 2", state, state->recent[state->recent_idx]);
                        state->recent[state->recent_idx] = NULL;
                }
                state->recent[state->recent_idx] = state->last_packet;
        }
 out:
        state->last_packet = fap;
        update_packets_ui(state);

        return 0;
}

/*
 * Update third panel
 */
int update_mybeacon_status(struct state *state)
{
        char buf[512];
        time_t delta = (time(NULL) - state->last_beacon);
        uint8_t quality = state->digi_quality;
        unsigned int count = 1;
        int i;

        for (i = 1; i < 8; i++) {
                count += (quality >> i) & 0x01;
        }

        if( (count / 2) > 4 ) {
                pr_debug("Sending sigbar value of %d, should be 0-4 => count =%d\n",
                         count / 2, count);
        }

        snprintf(buf, sizeof(buf), "%u", count / 2);
        _ui_send(state, "G_SIGBARS", buf);

        if (state->last_beacon)
                snprintf(buf, sizeof(buf), "%s ago", format_time(delta));
        else
                snprintf(buf, sizeof(buf), "Never");
        _ui_send(state, "G_LASTBEACON", buf);

        return 0;
}

int should_digi_packet(struct state *state, fap_packet_t *fap)
{
        int len;

        if (!state->conf.digi_enabled)
                return 0;

        len = strlen(state->conf.digi_alias);

        /* We digi if the first element of the path is our digi_alias */
        return ((fap->path_len > 0) &&
                  fap->path && fap->path[0] &&
                  STRNEQ(fap->path[0], state->conf.digi_alias, len));
}

int digi_packet(struct state *state, fap_packet_t *fap)
{
        char *first_digi_start, *first_digi_end;
        char *copy_packet = NULL;
        char *digi_packet = NULL;
        int ret;

        copy_packet = strdup(fap->orig_packet);
        if (!copy_packet)
                return 0;

        first_digi_start = strstr(copy_packet, fap->path[0]);
        first_digi_end = first_digi_start + strlen(fap->path[0]) + 1;
        if (state->conf.digi_append)
                first_digi_end = strchr(first_digi_end, ':');
        if (!first_digi_start || (first_digi_end <= first_digi_start)) {
                printf("DIGI: failed to find first digi `%s' in %s\n",
                       fap->path[0], copy_packet);
                ret = 0;
                goto out;
        }
        *first_digi_start = '\0';

        ret = asprintf(&digi_packet, "%s%s*,%s%s",
                       copy_packet,
                       state->mycall,
                       state->conf.digi_append ? state->conf.digi_path : "",
                       first_digi_end);
        if (ret < 0)
                goto out;

        /* txdelay in ms */
        usleep(state->conf.digi_delay * 1000);

        ret = send_beacon(state, digi_packet);
        _ui_send(state, "I_DG", "1000");

out:
        free(copy_packet);
        free(digi_packet);

        return ret;
}

void parse_incoming_packet(struct state *state, char *packet, int len, int isax25)
{
        fap_packet_t *fap;

        /* bump received packet count */
        state->stats.inPktCount++;

        fap = dan_parseaprs(packet, len, isax25);

        if (!fap->error_code) {
                if (fap->type != NULL) {
                        /* Handle message packets */
                        switch(*fap->type) {
#ifdef WEBAPP
                                case fapMESSAGE:
                                        handle_aprsMessages(state, fap, packet);
                                        break;
#endif /* WEBAPP */
                                case fapTHIRD_PARTY:
                                        handle_thirdparty(state, fap);
                                        break;

                                default:
                                        if(fap->message != NULL) {
                                                printf("%s: %s FAP error, message pointer set but type not fapMessage, type=0x%0x, message: %s\n",
                                                       time2str(fap->timestamp, 0), __FUNCTION__,  *fap->type,  packet);
                                        }
                                        break;
                        }

                } else {
                        if(state->debug.console_display_filter & CONSOLE_DISPLAY_DEBUG) {
                                printf("%s %s: FAP unhandled packet type\n%s\n",
                                       time2str(fap->timestamp, 0), __FUNCTION__, packet);
                        }
                }

                store_packet(state, fap);
                if (STREQ(fap->src_callsign, state->mycall)) {
                        state->digi_quality |= 1;
                        update_mybeacon_status(state);
                }
                if (state->disp_idx < 0) /* No other packet displayed */
                        display_packet(state, fap);
                state->last_packet = fap;
                _ui_send(state, "I_RX", "1000");
                if (should_digi_packet(state, fap)) {
                        digi_packet(state, fap);
                        printf("DEBUG: digipeated packet\n");
                }
        } else {
                if(state->debug.verbose_level > 2) {
                        display_fap_error("incoming pkt", state, fap);
                }
                fap_free_wrapper("incoming pkt", state, fap);
        }
#ifdef WEBAPP
        display_statistics(state);
#endif /* WEBAPP */
}

int handle_incoming_packet(struct state *state)
{
        char packet[512];
        unsigned int len = sizeof(packet);

        int ret;
        int isax25;

        memset(packet, 0, len);

        if (STREQ(state->conf.tnc_type, "AX25")) {
                isax25 = 1;
#ifdef HAVE_AX25_TRUE
                ret = get_ax25packet(state, packet, &len);
#else
                printf("%s:%s(): tnc type has been configured to use AX.25 but AX.25 lib has not been installed.\n",
                       __FILE__, __FUNCTION__);
                return 0;
#endif /* #ifdef HAVE_AX25_TRUE */

        } else if (STREQ(state->conf.tnc_type, "KISS")) {
                isax25 = 1;
                ret = get_packet(state->tncfd, packet, &len);
        } else {
                isax25 = 0;
                ret = get_packet_text(state->tncfd, packet, &len);
        }

        if (!ret)
                return -1;

        parse_incoming_packet(state, packet, len, isax25);
        return 0;
}

int parse_gps_nmea_string(struct state *state)
{
        char *str = &state->gps_buffer[state->gps_idx];

        if (*str == '\n')
                str++;

        if (!valid_checksum(str))
                return 0;

        if (strncmp(str, nmea_gga, 6) == 0) {
                return parse_gga(MYPOS(state), str);
        } else if (strncmp(str, nmea_rmc, 6) == 0) {
                state->mypos_idx = (state->mypos_idx + 1) % KEEP_POSITS;
                return parse_rmc(MYPOS(state), str);
        }

        return 0;
}
int parse_gpsd_nmea_string(struct state *state)
{
        char *str = &state->gps_buffer[state->gps_idx];

#if 0
        printf("debug (%d) gpsd_nmea_string: >%s<\n", strlen(str), str);
#endif

        if (*str == '\n') {
                str++;
        }
        if (strncmp(str, nmea_gga, 6) == 0) {
                return parse_gga(MYPOS(state), str);
        } else if (strncmp(str, nmea_rmc, 6) == 0) {
                state->mypos_idx = (state->mypos_idx + 1) % KEEP_POSITS;
                return parse_rmc(MYPOS(state), str);
        }

        return 0;
}

/*
 * Display UTC with dst correction from GPS time
 *  NEMA $GPGGA is in format HHMMSS
 */
int display_gps_info(struct state *state)
{
        char buf[512];
        char timestr[32];
        time_t epochtime;
        struct posit *mypos = MYPOS(state);
        const char *status = mypos->qual != 0 ?
                             "Locked" :
                             "<span background='red'>INVALID</span>";

        switch(state->conf.gps_type_int) {
                case GPS_TYPE_GPSD:
                        strftime(timestr, sizeof(timestr), "%H:%M:%S",
                                 localtime(&mypos->epochtime));
                        break;
                case GPS_TYPE_SERIAL:
                        epochtime = convrt_gps_to_epoch(state);
                        strftime(timestr, sizeof(timestr), "%H:%M:%S",
                                 localtime(&epochtime));

                        break;
        }

        sprintf(buf, "%7.5f%c %8.5f%c   %s   %s: %2i sats",
                  fabs(mypos->lat), mypos->lat > 0 ? 'N' : 'S',
                  fabs(mypos->lon), mypos->lon > 0 ? 'E' : 'W',
                  timestr,
                  status,
                  mypos->sats);
        _ui_send(state, "G_LATLON", buf);

        if (mypos->speed > MIN_GPS_SPEED) {
                sprintf(buf, "%s %2s, Alt %s",
                          format_speed(state, "%.0f %s", KTS_TO_KPH(mypos->speed)),
                          direction(mypos->course),
                          format_altitude_gps(state, "%.0f %s", mypos->alt));
        } else {
                sprintf(buf, "Stationary, Alt %s", format_altitude_gps(state, "%.0f %s", mypos->alt));
        }
        _ui_send(state, "G_SPD", buf);

        _ui_send(state, "G_MYCALL", state->mycall);

        return 0;
}

/*
 * Convert GPS time from serial GPS to Unix epoch time
 */
time_t convrt_gps_to_epoch(struct state *state)
{
        struct posit *mypos = MYPOS(state);
        int tstamp = mypos->tstamp;
        int dstamp = mypos->dstamp;
        time_t gpstime, machinetime;
        int hour, min, sec;
        int day, mon, year;
        struct tm gtm;

        /* seed the time_t struct with current machine time
         *  - then change with gps time
         */
        if((machinetime = time(NULL)) == -1) {
                fprintf(stderr, "%s: Error getting machine time: %s\n", __FUNCTION__, strerror(errno));
                return(-1);
        }

        gmtime_r(&machinetime, &gtm);

        hour = (tstamp / 10000);
        min = (tstamp / 100) % 100;
        sec = tstamp % 100;

        day = (dstamp / 10000);
        mon = (dstamp / 100) % 100;
        year = dstamp % 100;
        /*
         * convert broken down time (struct
         * tm) to unix calendar time (time_t)
         */
        gtm.tm_sec = sec;
        gtm.tm_min = min;
        gtm.tm_hour = hour;
        gtm.tm_mday = day;
        gtm.tm_mon = mon-1; /* number of months since January 0-11 */
        gtm.tm_year = year+100; /* years since 1900 */

        /*
         * During Daylight saving subtract an hour.
         */
        gpstime = mktime(&gtm);
        if ( gpstime != -1 ) {
                gpstime += gtm.tm_gmtoff;
                if(gtm.tm_isdst) {
                        gpstime -=3600;
                }
        }

        if(state->debug.verbose_level > 3) {

                struct tm lt;

                localtime_r(&machinetime, &lt);

                if (lt.tm_isdst < 0) {
                        printf("Can not determine if Daylight Saving Time is in effect\n");
                } else {
                        printf("DST is %s in effect\n", lt.tm_isdst ? "" : "NOT");
                }

                printf("\ngps hour: %d, min: %d, sec: %d\n", hour, min, sec);
                localtime_r(&gpstime, &gtm);
                printf("loc debug: hr: %d, mn: %d, sc: %d, day: %d, mon: %d, yr: %d off: %ld [%ld]\n",
                       lt.tm_hour, lt.tm_min, lt.tm_sec, lt.tm_mday, lt.tm_mon, lt.tm_year, lt.tm_gmtoff, machinetime);
                printf("gps debug: hr: %d, mn: %d, sc: %d, day: %d, mon: %d, yr: %d off: %ld [%ld]\n",
                       gtm.tm_hour, gtm.tm_min, gtm.tm_sec, gtm.tm_mday, gtm.tm_mon, gtm.tm_year, gtm.tm_gmtoff, gpstime);
                printf("Time: machine %s, gps %s\n", asctime(&lt), asctime(&gtm));
        }

        return gpstime;
}

/*
 * Set system time using GPS time
 */
int set_time(struct state *state)
{
        struct posit *mypos = MYPOS(state);
        time_t epochtime = mypos->epochtime;
        time_t gpstime, machinetime;

        /*
         * Only set time if a sufficient number of GPS satellites have
         * been acquired.
         */
        if (mypos->qual == 0)
                return 1; /* No fix, no set */
        else if (mypos->sats < 3) {
                return 1; /* Not enough sats, don't set */
        }

        /* Only set the Computer system time at boot up & on a period
         * defined by configuration (in minutes).
         *
         * Setting system time frequently (every 2 minutes) causes
         * programs that check & verify time like Dovecot to indicate a
         * variation in system time of more than 7 seconds causing bad
         * things to happen.
         */
        else if (state->last_time_set &&
                 state->conf.gps_time_update &&
                 !HAS_BEEN(state->last_time_set, state->conf.gps_time_update * 60)) {
                return 1; /* Too recent */
        } else if (state->conf.gps_time_update == 0) {
                return 1; /* Config'ed to not set system time with GPS fix time */
        }

        if((machinetime = time(NULL)) == -1) {
                fprintf(stderr, "%s: Error getting machine time: %s\n", __FUNCTION__, strerror(errno));
                return(-1);
        }

        /*
         * Handle gps daemon & gps serial devices
         */
        if(state->conf.gps_type_int == GPS_TYPE_GPSD) {

                gpstime = epochtime;

        } else if(state->conf.gps_type_int == GPS_TYPE_SERIAL) {
                gpstime = convrt_gps_to_epoch(state);

        } else {

                fprintf(stderr, "%s: gps device not configured\n", __FUNCTION__);
                return -1;
        }

        if(gpstime == machinetime) {
                printf("***Not setting machine time, gps = machine\n");
        } else if(gpstime > machinetime) {
                printf(" *** Setting machine time +%ld\n", gpstime - machinetime);
        } else {
                printf(" *** Setting machine time -%ld\n", machinetime - gpstime);
        }
        /*
         * Only set machine time if difference is greater than 1 second.
         */
        if(abs(gpstime - machinetime) > 2) {
                /* int stime(const time_t *t) is deprecated, use:
                 * int clock_settime(clockid_t clockid, const struct timespec *tp);
                 *  stime(&gpstime);
                 */

                /* Set the system clock to *WHEN.  */
                {
                    struct timespec ts;
                    ts.tv_sec = gpstime;
                    ts.tv_nsec = 0;

                    clock_settime (CLOCK_REALTIME, &ts);
                }
        }

        state->last_time_set = time(NULL);

        return 0;
}

/* #define GPS_JSON_RESPONSE_MAX	4096 */
int handle_gps_data(struct state *state)
{
        char buf[33];
        int ret;
        char *cr;
        /* buffer to hold one JSON message */
        char message[GPS_JSON_RESPONSE_MAX];


        switch(state->conf.gps_type_int) {

                /* Serial port gets GPS data in NMEA 0183 sentence format */
                case GPS_TYPE_SERIAL:
                        ret = read(state->gpsfd, buf, 32);
                        buf[ret] = 0; /* Safe because size is +1 */

                        if (ret < 0) {
                                perror("gps");
                                return -errno;
                        } else if (ret == 0)
                                return 0;

                        if (state->gps_idx + ret > sizeof(state->gps_buffer)) {
                                printf("Clearing overrun buffer\n");
                                state->gps_idx = 0;
                        }

                        cr = strchr(buf, '\r');
                        if (cr) {
                                *cr = 0;
                                strcpy(&state->gps_buffer[state->gps_idx], buf);
                                if (parse_gps_nmea_string(state))
                                        state->last_gps_data = time(NULL);
                                strcpy(state->gps_buffer, cr+1);
                                state->gps_idx = strlen(state->gps_buffer);
                        } else {
                                memcpy(&state->gps_buffer[state->gps_idx], buf, ret);
                                state->gps_idx += ret;
                        }

                        break;
                case GPS_TYPE_GPSD:   /* GPSD gets data in a format dependent on flags */

                {
#ifdef HAVE_GPSD_LIB
                        int gpsdata_cnt;
                        struct gps_data_t *pgpsd= &state->gpsdata;



                        /* reading directly from the socket avoids decode overhead */
                        errno = 0;
                        if ((gpsdata_cnt = (int)recv(pgpsd->gps_fd, message, sizeof(message), 0)) == -1) {
                                if (errno == EAGAIN) {
                                        return 0;
                                }
                                fprintf(stderr, "%s: socket error 4", __FUNCTION__);
                                fprintf(stderr, ", is: %s\n", (errno == 0) ? "GPS_GONE" : "GPS_ERROR");
                                return -errno;
                        }
#if 0
                        printf("debug: data cnt: %d ", gpsdata_cnt);
#endif
                        if (state->gps_idx + gpsdata_cnt > sizeof(state->gps_buffer)) {
                                printf("Clearing overrun buffer\n");
                                state->gps_idx = 0;
                        }

                        char *msgptr=message;
                        if (gpsdata_cnt > 0) {
                                cr = strchr(msgptr, '\n');
                                if (cr != NULL) {
                                        int cnt_gps;

                                        *cr = 0;
#if 0
                                        printf(" === found cr: %s, cnt: %d\n",cr+1, gpsdata_cnt);
#endif
                                        strcpy(&state->gps_buffer[state->gps_idx], message);

                                        if (parse_gpsd_nmea_string(state))
                                                state->last_gps_data = time(NULL);
                                        strcpy(state->gps_buffer, cr+1);
                                        cnt_gps = strlen(state->gps_buffer);
                                        state->gps_idx = cnt_gps;
                                        gpsdata_cnt-= cnt_gps;

                                } else {
                                        printf("debug no cr\n");
                                        memcpy(&state->gps_buffer[state->gps_idx], message, gpsdata_cnt);
                                        state->gps_idx += gpsdata_cnt;
                                }
                        }

#else
                        fprintf(stderr, "%s: gpsd daemon not configured\n", __FUNCTION__);
#endif /* HAVE_GPSD_LIB */
                }
                break;

                case GPS_TYPE_FAKE:
                        fake_gps_data(state);
                        break;

                default:
                        fprintf(stderr, "%s: gps device not configured\n", __FUNCTION__);
                        return -1;
                        break;
        }

        if (MYPOS(state)->speed > 0)
                state->last_moving = time(NULL);

        if (HAS_BEEN(state->last_gps_update, 1)) {
                display_gps_info(state);
                state->last_gps_update = time(NULL);
                set_time(state);
                update_mybeacon_status(state);
                update_packets_ui(state);
        }

        return 0;
}

int handle_telemetry(struct state *state)
{
        char _buf[512] = "";
        int i = 0;
        int ret;
        char *buf = _buf;
        char *space;

        while (i < sizeof(_buf)) {
                ret = read(state->telfd, &buf[i], 1);
                if (buf[i] == '\n')
                        break;
                if (ret < 0)
                        return -ret;
                else if (ret == 1)
                        i++;
        }

        while (buf && *buf != '\n') {
                char name[16];
                char value[16];

                space = strchr(buf, ' ');
                if (space)
                        *space = 0;

                ret = sscanf(buf, "%16[^=]=%16s", (char*)&name, (char*)&value);
                if (ret != 2) {
                        printf("Invalid telemetry: %s\n", buf);
                        return -EINVAL;
                }

                buf = space+1;

                if (STREQ(name, "temp1"))
                        state->tel.temp1 = atof(value);
                else if (STREQ(name, "voltage"))
                        state->tel.voltage = atof(value);
                else
                        printf("Unknown telemetry value %s\n", name);
        }

        snprintf(_buf, sizeof(_buf), "%.1fV", state->tel.voltage);
        _ui_send(state, "T_VOLTAGE", _buf);

        snprintf(_buf, sizeof(_buf), "%.0fF", state->tel.temp1);
        _ui_send(state, "T_TEMP1", _buf);

        state->tel.last_tel = time(NULL);

        return 0;
}

int handle_display_showinfo(struct state *state, int index)
{
        fap_packet_t *fap;
        int number = (state->recent_idx + KEEP_PACKETS - index) % KEEP_PACKETS;

        state->disp_idx = index;

        if (index < 0)
                fap = state->last_packet;
        else
                fap = state->recent[number];
        if (!fap)
                return 1;

        display_packet(state, fap);

        return 0;
}

int handle_display_initkiss(struct state *state)
{
        const char *cmd = state->conf.init_kiss_cmd;
        int ret;

        ret = write(state->tncfd, cmd, strlen(cmd));
        if (ret > 0)
                printf("Sent KISS initialization command\n");
        else
                printf("Failed to send KISS initialization command: %m\n");

        return 0;
}

/*
 * System Actions - enable remote system control
 *  - shutdown remote system
 *  - restart aprs & spy apps
 */
void sys_ctrl(char *msg)
{
        struct utsname utsname;

        if (STREQ(msg, UI_MSG_VALUE_SYSCTRL_SHUTDOWN)) {
                if(uname(&utsname) != 0 ) {
                        fprintf(stderr, "%s: error: %s\n",
                                  __FUNCTION__, strerror(errno));
                        return;
                }

                printf("Performing sysctrl action on %s machine: SHUTDOWN\n",
                       utsname.machine);
                /* Conditional so dev workstation doesn't power itself
                 * off */
                if(STRNEQ(utsname.machine, "armv7l", 6)) {
                        system("shutdown -h now");
                } else {
                        printf("Failed to shutdown due to machine type check\n");
                        printf("Found machine type: %s, expected: armv7l\n",utsname.machine);
                }

        } else if (STREQ(msg, UI_MSG_VALUE_SYSCTRL_RESET)) {
                printf("Performing sysctrl action: RESET\n");
                system("at now -f/etc/tracker/tracker-restart");
        } else {
                printf("Unhandled sysctrl action %s\n", msg);
        }
}

int handle_display(struct state *state)
{
        struct ui_msg *msg = NULL;
        const char *name;
        int ret;

        ret = ui_get_msg(state, &msg);

        if ((ret <= 0) || msg == NULL) {
                if(ret <= 0) {
                        fprintf(stderr, "%s: %s error: %s\n",
                                  __FUNCTION__,
                                  (ret == ENOMEM) ? "malloc" : "socket read",
                                  strerror(errno));
                }
                close(state->dspfd);
                state->dspfd = -1;
                return -errno;
        }

        /* Should return null when using web app */
        name = ui_get_msg_name(msg);

        if (!name)
                goto out;
#ifdef DEBUG_VERBOSE
        printf("%s: name: %s\n", __FUNCTION__, name);
#endif /*  DEBUG_VERBOSE */
        if (STREQ(name, UI_MSG_NAME_STATION_INFO)) {
                int index = atoi(ui_get_msg_valu(msg));
                ret = handle_display_showinfo(state, index);
        } else if (STREQ(name, UI_MSG_NAME_BEACON)) {
                state->last_beacon = 0;
        } else if (STREQ(name, UI_MSG_NAME_KISS)) {
                handle_display_initkiss(state);
        } else if (STREQ(name, UI_MSG_NAME_SEND)) {
/*                send_beacon(state, ui_get_msg_valu(msg)); */
        } else if (STREQ(name, UI_MSG_NAME_SETCFG)) {
                /* Set any config coming back from web app */
        } else if (STREQ(name, UI_MSG_NAME_GETCFG)) {
                /* Send requested config back to web app */
                _ui_send(state, "CF_CALL", state->mycall);
                printf("Sent source callsign %s\n", state->mycall);
        } else if (STREQ(name, UI_MSG_NAME_SYSCTRL)) {
                /* sys ctrl actions never return */
                sys_ctrl(ui_get_msg_valu(msg));
        } else {
                printf("Display said: name: %s, value: %s\n",
                       name, ui_get_msg_valu(msg));
                dump_ui_msg(msg);
        }
out:

        free(msg);
        return ret;
}

char *get_comment(struct state *state)
{
        int cmt = state->comment_idx++ % state->conf.comments_count;

        return process_subst(state, state->conf.comments[cmt]);
}

/*
 * Choose a comment out of the list, and choose a type
 * of (phg, wx, normal) from the list of configured types
 * and construct it.
 */
char *choose_data(struct state *state, char *req_icon)
{
        char *data = NULL;
        char *comment;

        comment = get_comment(state);
        if (!comment)
                comment = strdup("Error");

        switch (state->other_beacon_idx++ % 3) {
                case DO_TYPE_WX:
                        if ((state->conf.do_types & DO_TYPE_WX) &&
                            (!HAS_BEEN(state->tel.last_tel, 30))) {
                                *req_icon = '_';
                                asprintf(&data,
                                         ".../...g...t%03.0f%s",
                                         state->tel.temp1,
                                         comment);
                                break;
                        }
                case DO_TYPE_PHG:
                        if (state->conf.do_types & DO_TYPE_PHG) {
                                asprintf(&data,
                                         "PHG%1d%1d%1d%1d,%s",
                                         state->conf.power,
                                         state->conf.height,
                                         state->conf.gain,
                                         state->conf.directivity,
                                         comment);
                                break;
                        }
                case DO_TYPE_NONE:
                        data = strdup(comment);
                        break;
        }

        free(comment);
        return data;
}

void separate_minutes(double minutes, unsigned char *min, unsigned char *hun)
{
        double _min, _hun;

        _hun = modf(minutes, &_min);
        *min = (unsigned char)_min;
        *hun = (unsigned char)(_hun * 100);

        printf("min: %hhd hun: %hhd\n", *min, *hun);
}

/* Get the @digith digit of a base-ten number
 *
 * 1234
 * |||^-- 0
 * ||^--- 1
 * |^---- 2
 * ^----- 3
 */
unsigned char get_digit(int value, int digit)
{
        value /= pow(10, digit);
        return value % 10;
}

char *make_mice_beacon(struct state *state)
{
        char *str = NULL;

        struct posit *mypos = MYPOS(state);
        double ldeg, lmin;
        double Ldeg, Lmin;
        int lat;
        unsigned char north = mypos->lat > 0 ? 0x50 : 0x30;
        unsigned char lonsc = (fabs(mypos->lon) >= 100) ||
                              (fabs(mypos->lon) < 10) ? 0x50 : 0x30;
        unsigned char west = mypos->lon > 0 ? 0x30 : 0x50;

        unsigned char lon_deg, lon_min, lon_hun;

        unsigned char spd_htk;
        unsigned char spd_crs;
        unsigned char crs_tud;

        unsigned int atemp;
        char _altitude[5];
        char *altitude = &_altitude[0];

        lmin = modf(fabs(mypos->lat), &ldeg) * 60;
        Lmin = modf(fabs(mypos->lon), &Ldeg) * 60;

        /* Latitude DDMMmm encoded in base-10 */
        lat = (ldeg * 10000) + (lmin * 100);

        /* Longitude degrees encoded per APRS spec */
        if (Ldeg <= 9)
                lon_deg = (int)Ldeg + 118;
        else if (Ldeg <= 99)
                lon_deg = (int)Ldeg + 28;
        else if (Ldeg <= (int)109)
                lon_deg = (int)Ldeg + 108;
        else if (Ldeg <= 179)
                lon_deg = ((int)Ldeg - 100) + 28;

        /* Minutes and hundredths of a minute encoded per APRS spec */
        separate_minutes(Lmin, &lon_min, &lon_hun);
        if (Lmin > 10)
                lon_min += 28;
        else
                lon_min += 88;
        lon_hun += 28;

        /* Speed, hundreds and tens of knots */
        spd_htk = (mypos->speed / 10) + 108;

        /* Units of speed and course hundreds of degrees */
        spd_crs = 32 + \
                  (((int)mypos->speed % 10) * 10) + \
                  ((int)mypos->course / 100);

        /* Course tens and units of degrees */
        crs_tud = ((int)mypos->course % 100) + 28;

        /* Mic_E altitude field follows,
         * restricted to not start with one of:
         *  `,', or 0x1d
         *  so as not to be confused with telemetry data
         *  3 (or 4) character altitude terminated with }
         *  page 55 Aprs 1.0 spec.
         */

        /* Altitude, base-91
         * xxx in meters relative to 10km below mean sea level
         */
        atemp = mypos->alt + 10000;
        altitude[0] = 33 + (atemp / pow(91, 3));
        atemp = atemp % (int)pow(91, 3);
        altitude[1] = 33 + (atemp / pow(91, 2));
        atemp = atemp % (int)pow(91, 2);
        altitude[2] = 33 + (atemp / 91);
        altitude[3] = 33 + (atemp % 91);
        altitude[4] = '\0';
        if (altitude[0] == 33)
                altitude = &altitude[1];

        /*
         * AX.25 stores Mic-E data in the destination address field.
         * AX.25 send beacon routine pulls out required fields &
         * formats appropriately.
         */
        if (STREQ(state->conf.tnc_type, "AX25")) {
                /*  AX.25 builds it's own packet header src & dest
                 *  address */

                asprintf(&str,
                         "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%s}",
                         APRS_DATATYPE_CURRENT_MIC_E_DATA,
                         get_digit(lat, 5) | 0x50,
                         get_digit(lat, 4) | 0x30,
                         get_digit(lat, 3) | 0x50,
                         get_digit(lat, 2) | north,
                         get_digit(lat, 1) | lonsc,
                         get_digit(lat, 0) | west,
                         lon_deg,
                         lon_min,
                         lon_hun,
                         spd_htk,
                         spd_crs,
                         crs_tud,
                         state->conf.icon[1],
                         state->conf.icon[0],
                         altitude);
        } else {
                asprintf(&str,
                         "%s>%c%c%c%c%c%c,%s:%c%c%c%c%c%c%c%c%c%s}",
                         state->mycall,
                         get_digit(lat, 5) | 0x50,
                         get_digit(lat, 4) | 0x30,
                         get_digit(lat, 3) | 0x50,
                         get_digit(lat, 2) | north,
                         get_digit(lat, 1) | lonsc,
                         get_digit(lat, 0) | west,
                         state->conf.digi_path,
                         APRS_DATATYPE_CURRENT_MIC_E_DATA,
                         lon_deg,
                         lon_min,
                         lon_hun,
                         spd_htk,
                         spd_crs,
                         crs_tud,
                         state->conf.icon[1],
                         state->conf.icon[0],
                         altitude);
        }
        return str;
}

char *make_status_beacon(struct state *state)
{
        char *packet = NULL;
        char *data = get_comment(state);

        /*  AX.25 builds it's own packet src & dest address */
        if (STREQ(state->conf.tnc_type, "AX25")) {
                asprintf(&packet,
                         "%c%s",
                         APRS_DATATYPE_STATUS,
                         data);
        } else {
                asprintf(&packet,
                         "%s>%s,%s:%c%s",
                         state->mycall,
                         "APZDMS",
                         state->conf.digi_path,
                         APRS_DATATYPE_STATUS,
                         data);
        }
        free(data);

        return packet;
}

char *make_beacon(struct state *state, char *payload)
{
        char *data = NULL;
        char *packet;
        char _lat[16];
        char _lon[16];
        int ret;
        char icon = state->conf.icon[1];
        struct posit *mypos = MYPOS(state);
        char course_speed[] = ".../...";

        double lat = fabs(mypos->lat);
        double lon = fabs(mypos->lon);

        snprintf(_lat, 16, "%02.0f%05.2f%c",
                 floor(lat),
                 (lat - floor(lat)) * 60,
                 mypos->lat > 0 ? 'N' : 'S');

        snprintf(_lon, 16, "%03.0f%05.2f%c",
                 floor(lon),
                 (lon - floor(lon)) * 60,
                 mypos->lon > 0 ? 'E' : 'W');

        if (mypos->speed > 5)
                snprintf(course_speed, sizeof(course_speed),
                         "%03.0f/%03.0f",
                         mypos->course,
                         mypos->speed);
        else
                course_speed[0] = 0;

        if (!payload)
                payload = data = choose_data(state, &icon);

        if (STREQ(state->conf.tnc_type, "AX25")) {
#ifdef HAVE_AX25_TRUE
                /*  AX.25 builds it's own packet header src & dest
                 *  address */
                ret = asprintf(&packet,
                               "%c%s%c%s%c%s/A=%06i%s",
                               APRS_DATATYPE_POS_NO_TIME_WITH_MSG,
                               _lat,
                               state->conf.icon[0],
                               _lon,
                               icon,
                               course_speed,
                               (int)M_TO_FT(mypos->alt),
                               payload);
#else
                printf("%s:%s(): tnc type has been configured to use AX.25 but AX.25 lib has not been installed.\n",
                       __FILE__, __FUNCTION__);
#endif /* #ifdef HAVE_AX25_TRUE */
        } else {
                ret = asprintf(&packet,
                               "%s>APZDMS,%s:%c%s%c%s%c%s/A=%06i%s",
                               state->mycall,
                               state->conf.digi_path,
                               APRS_DATATYPE_POS_NO_TIME_WITH_MSG,
                               _lat,
                               state->conf.icon[0],
                               _lon,
                               icon,
                               course_speed,
                               (int)M_TO_FT(mypos->alt),
                               payload);
        }

        free(data);

        if (ret < 0)
                return NULL;

        return packet;
}

double sb_course_change_thresh(struct state *state)
{
        double mph = KTS_TO_MPH(MYPOS(state)->speed);
        double slope = state->conf.course_change_slope;
        double min = state->conf.course_change_min;

        return min + (slope / mph);
}

bool should_beacon(struct state *state)
{
        struct posit *mypos = MYPOS(state);
        time_t delta = time(NULL) - state->last_beacon;
        time_t sb_min_delta;
        double speed_frac;
        double d_speed = state->conf.sb_high.speed - state->conf.sb_low.speed;
        double d_rate = state->conf.sb_low.int_sec -
                        state->conf.sb_high.int_sec;
        double sb_thresh = sb_course_change_thresh(state);
        double sb_change = fabs(state->last_beacon_pos.course - mypos->course);

        char *reason = NULL;

        /* If we went from a NW course to a NE course, the change will be large,
         * so correct it to the difference instead of assuming we always take
         * large right turns
         */
        if (sb_change > 180)
                sb_change = 360.0 - sb_change;

        /* Time required to have passed in order to beacon,
         * 0 if never, -1 if now
         */
        time_t req = 0;

        /* NEVER more often than every 10 seconds! */
        if (delta < 10)
                return false;

        /* The fractional penetration into the lo/hi zone */
        speed_frac = (KTS_TO_MPH(mypos->speed) -
                      state->conf.sb_low.speed) / d_speed;

        /* Determine the fractional that we are slower than the max */
        sb_min_delta = (d_rate * (1 - speed_frac)) +
                       state->conf.sb_high.int_sec;

        /* Never when we aren't getting data anymore */
        if (HAS_BEEN(state->last_gps_data, 30)) {
                mypos->qual = mypos->sats = 0;
                reason = "NODATA";
                goto out;
        }

        /* When we don't have a gps fix do the atrest rate
         *  - this can occur when:
         *   doing dev and gps is in house
         *   driving through steep canyons or tall trees
         */
        if (mypos->qual == 0) {
                reason = "NOLOCK";
                req = state->conf.atrest_rate;
                /* No valid gps data so use the static lat/long/course
                 */
                mypos->lat = state->conf.static_lat;
                mypos->lon = state->conf.static_lon;
                mypos->course = state->conf.static_crs;
                goto out;
        }

        /* If we have recently stopped moving, do one beacon */
        if (state->last_moving &&
            HAS_BEEN(state->last_moving, state->conf.after_stop)) {
                state->last_moving = 0;
                req = -1;
                reason = "STOPPED";
                goto out;
        }

        /* If we're not moving at all, choose the "at rest" rate */
        if (mypos->speed <= 1) {
                req = state->conf.atrest_rate;
                reason = "ATREST";
                goto out;
        }

        /* SmartBeaconing: Course Change (only if moving) */
        if ((sb_change > sb_thresh) && (KTS_TO_MPH(mypos->speed) > 2.0)) {
                printf("SB: Angle changed by %.0f (>%.0f)\n",
                       sb_change, sb_thresh);
                reason = "COURSE";
                req = -1;
                goto out;
        }

        /* SmartBeaconing: Range-based variable speed beaconing */

        /* If we're going below the low point, use that interval */
        if (KTS_TO_MPH(mypos->speed) < state->conf.sb_low.speed) {
                req = state->conf.sb_low.int_sec;
                reason = "SLOWTO";
                goto out;
        }

        /* If we're going above the high point, use that interval */
        if (KTS_TO_MPH(mypos->speed) > state->conf.sb_high.speed) {
                req = state->conf.sb_high.int_sec;
                reason = "FASTTO";
                goto out;
        }

        /* We must be in the speed zone, so adjust interval according
         * to the fractional penetration of the speed range
         */
        req = sb_min_delta;
        reason = "FRACTO";
 out:
        if (reason) {
                char tmp[256];

                if (req <= 0)
                        strcpy(tmp, reason);
                else
                        sprintf(tmp, "Every %s", format_time(req));
                _ui_send(state, "G_REASON", tmp);
        }

        /* req = 0 if never, -1 if now */
        if (req == 0) {
                update_mybeacon_status(state);
                return false;
        } else if (req == -1) {
                /* temporary debug */
                printf("%s: beacon reason: %s, req: now\n",
                       time2str(NULL, 0),
                       reason ? reason : "No reason");
                return true;
        }

        /* temporary debug */
        if(delta > req) {
                printf("%s: beacon reason: %s, req: %lu delta: %lu\n",
                       time2str(NULL, 0),
                       reason ? reason : "No reason",
                       req, delta);
        }
        return delta > req;
}

/*
 * return true if beacon was sent successfully
 */
int beacon(struct state *state)
{
        char *packet;
        static time_t max_beacon_check = 0;
        int ret;

        /* Don't even check but every half-second */
        if (!HAS_BEEN(max_beacon_check, 0.5)) {
                return 0;
        }

        max_beacon_check = time(NULL);

        if (!should_beacon(state))
                return 0;

        if (MYPOS(state)->speed > 5) {
                pr_debug("Sending mic-e beacon\n");

                /* Send a short MIC-E position beacon */
                packet = make_mice_beacon(state);
                ret = send_beacon(state, packet);
                free(packet);

                if (HAS_BEEN(state->last_status, 120)) {
                        /* Follow up with a status packet */
                        packet = make_status_beacon(state);
                        ret = send_beacon(state, packet);
                        free(packet);
                        state->last_status = time(NULL);
                }
        } else {
                packet = make_beacon(state, NULL);
                ret = send_beacon(state, packet);
                free(packet);
        }
        if(ret) {
                state->last_beacon = time(NULL);
                state->digi_quality <<= 1;
                update_mybeacon_status(state);

                _ui_send(state, "I_TX", "1000");

                state->last_beacon_pos = state->mypos[state->mypos_idx];
        }
        return ret;
}

int redir_log(struct state *state)
{
        int fd;
        char *fname=NULL;

        asprintf(&fname, "/tmp/aprs_tracker.log");

        fd = open(fname, O_WRONLY|O_TRUNC|O_CREAT, 0644);
        if (fd < 0) {
                perror(fname);
                return -errno;
        }

        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        setvbuf(stdout, NULL, _IONBF, 0);

        return 0;
}

int fake_gps_data(struct state *state)
{
        struct posit *mypos = MYPOS(state);

        if (state->conf.testing) {
                //state->conf.static_lat -= 0.01;
                //state->conf.static_lon += 0.01;
                //if (state->conf.static_spd > 0)
                //    state->conf.static_spd -= 1;
                state->conf.static_crs += 0.1;
        }

        mypos->lat = state->conf.static_lat;
        mypos->lon = state->conf.static_lon;
        mypos->course = state->conf.static_crs;
        /* If ini parameter locale:units = metric
         * assume static variables are given in metric */
        mypos->alt = state->conf.metric_units ? state->conf.static_alt : FT_TO_M(state->conf.static_alt);
        mypos->speed = KPH_TO_MPH(state->conf.static_spd);

        mypos->qual = 1;
        mypos->sats = 0; /* We may claim qual=1, but no sats */

        state->last_gps_data = time(NULL);
        state->tel.temp1 = 75;
        state->tel.voltage = 13.8;
        state->tel.last_tel = time(NULL);

        if ((time(NULL) - state->last_gps_update) > 3) {
                display_gps_info(state);
                state->last_gps_update = time(NULL);
        }

        return 0;
}

int gps_init(struct state *state)
{
        int gpsd_flags=WATCH_ENABLE | WATCH_NMEA;
        state->gpsfd = -1;

        switch(state->conf.gps_type_int) {
                case GPS_TYPE_SERIAL:
                        if(state->debug.verbose_level > 2) {
                                printf("%s: serial\n", __FUNCTION__);
                        }
                        if (state->conf.gps) { /* check for a serial device from config */
                                state->gpsfd = serial_open(state->conf.gps, state->conf.gps_rate, 0);
                                if (state->gpsfd < 0) {
                                        perror(state->conf.gps);
                                        return(-1);
                                }
                        } else {
                                fprintf(stderr, "GPS serial device choosen but no device specified\n");
                                return(-1);
                        }
                        break;

                case GPS_TYPE_GPSD:
#ifdef HAVE_GPSD_LIB
                        if(state->debug.verbose_level > 2) {
                                printf("%s: GPSD\n", __FUNCTION__);
                        }

                        if(gps_open("localhost", "2947", &state->gpsdata) < 0){
                                fprintf(stderr,"Could not connect to GPSd\n");
                                return(-1);
                        }
                        /* This is not an error condition for gpsd */
                        if (state->conf.gps == NULL) { /* check for a serial device from config */
                                fprintf(stderr, "GPS no device specified, ok\n");
                        }

                        /* register for updates */
                        /* Original code always returned satellites_used=0 for DRAWS gps device
                         * Now specifying WATCH_NMEA which fixes that problem.
                         */
                        /* Check for a gps serial port name */
                        if (state->conf.gps != NULL) {
                                gpsd_flags |= WATCH_DEVICE;
                        }
                        if(gps_stream(&state->gpsdata, gpsd_flags, state->conf.gps) == -1) {
                                perror("gps_stream()");
                                return(-1);
                        }
                        state->gpsfd = state->gpsdata.gps_fd;
#else
                        fprintf(stderr, "%s: gpsd daemon not configured\n", __FUNCTION__);
#endif /* HAVE_GPSD_LIB */
                        break;

                case GPS_TYPE_FAKE:
                        if(state->debug.verbose_level > 2) {
                                printf("%s: fake\n", __FUNCTION__);
                        }
                        fake_gps_data(state);
                        break;
                default:
                        fprintf(stderr, "gps type %s unhandled\n", state->conf.gps_type);
                        state->conf.gps_type_int = GPS_TYPE_UNDEFINED;
                        return (-1);
                        break;
        }
        return(0);
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
        time_t last_time, wait_time, pending_time;
        struct sockaddr sa;
        FILE *fsrecpkt;
        int *uifd = &state->dspfd;
        fd_set fds;
        struct timeval tv = {1, 0};
        int ret;

        /* open play back file */
        fsrecpkt = fopen(state->debug.playback_pkt_filename, "rb");
        if (fsrecpkt == NULL) {
                perror(state->debug.playback_pkt_filename);
                return errno;
        }

        if(*uifd <= 0) {
                *uifd=ui_connect(state);
        }

        printf("Playing back packets from file %s\n", state->debug.playback_pkt_filename);

        /* read the starting time of the captured packets */
        if(fread(&recpkt, sizeof(recpkt_t), 1, fsrecpkt) != 1) {
                fprintf(stderr, "Problem reading first bytes of playback file\n");
                return 1;
        }
        rewind(fsrecpkt);

        /* Initialize  reference time */
        last_time = recpkt.currtime;

        strcpy(sa.sa_data, "File");
        pending_time = 0;

        while(bytes_read > 0) {
                if (pending_time == 0) {
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
                                }  else {
                                        fprintf(stderr, "%s: Error reading file: 0x%02x %s %s\n",
                                                  __FUNCTION__, errno, strerror(errno), state->debug.playback_pkt_filename);
                                        return errno;
                                }
                        }
                        pkt_size = recpkt.size;

                        bytes_read = fread(inbuf, 1, pkt_size, fsrecpkt);
                        if(bytes_read != pkt_size) {
                                printf("Read error expected %d, read %d\n",
                                       pkt_size, bytes_read);
                                break;
                        }

                        cnt_pkts++;
                        cnt_bytes += bytes_read;

                        if(state->debug.verbose_level > 3) {
                                printf("Pkt count %d, size %d, time %s ... ",
                                       cnt_pkts, pkt_size, time2str((time_t *)&recpkt.currtime, 0));
                        }

                        /* pace the packets */
                        wait_time = recpkt.currtime - last_time;

                        if(state->debug.verbose_level > 3) {
                                printf("Wait %lu secs", wait_time);
                        }

                        /* Pace packets quicker than reality */
                        if(wait_time > 0)
                                wait_time = wait_time / state->debug.playback_time_scale;

                        if(wait_time < 0 || wait_time > 5*60) {
                                printf("Wait time seems unreasonable %lu\n", wait_time);
                                wait_time = 60;
                        }
                        last_time = recpkt.currtime;
                        tv.tv_sec = wait_time;
                        pending_time = time(NULL) + wait_time;

                } else {
                        wait_time = pending_time - time(NULL);
                        tv.tv_sec = wait_time > 0 ? wait_time : 0;
                }
                /* node server gets recursive interrupts intermittently.
                 * - slow the pacing down to see if that helps */
                tv.tv_usec = (wait_time == 0) ? 500 : 0;

                if(state->debug.verbose_level > 3) {
                        printf(", now %lu.%lu secs\n", tv.tv_sec, tv.tv_usec);
                        fflush(stdout);
                }

                FD_ZERO(&fds);

                if (state->dspfd > 0)
                        FD_SET(state->dspfd, &fds);
                if (state->gpsfd > 0)
                        FD_SET(state->gpsfd, &fds);

                ret = select(100, &fds, NULL, NULL, &tv);
                if (ret == -1) {
                        perror("select");
                        if (errno == EBADF)
                                break;
                        continue;
                } else if (ret > 0) {
                        if( (state->gpsfd != -1) && FD_ISSET(state->gpsfd, &fds)) {
                                handle_gps_data(state);
/*                                printf("g"); fflush(stdout); */
                        }
                        if( (state->dspfd != -1) && FD_ISSET(state->dspfd, &fds)) {
                                handle_display(state);
                        }
                }
                if(time(NULL) >= pending_time) {
                        handle_ax25_pkt(state, &sa, &inbuf[1], bytes_read-1);
                        pending_time = 0;
                }
        }
        return 0;
}

int main(int argc, char **argv)
{
        int i;
        fd_set fds;
        struct state state;
        bool bUpdateUI = true;

        printf("STARTING %s\n", getprogname());

        memset(&state, 0, sizeof(state));

        state.dspfd = -1;

        fap_init();

        if (parse_opts(argc, argv, &state)) {
                printf("Invalid option(s)\n");
                exit(1);
        }

        if (parse_ini(state.conf.config ? state.conf.config : "aprs.ini", &state)) {
                printf("Invalid config\n");
                exit(1);
        }

        /* Force fap display on */
        state.debug.display_fap_enable = true;

        /* If a hostname or ip address was supplied using the -d command
         * line argument then use AF_INET socket family.  Otherwise use
         * the AF_UNIX family with socket path parsed from ini file.
         */
        if(state.conf.ui_host_name != NULL) {
                if(lookup_host(&state)) {
                        printf("Failure connecting to display device\n");
                        exit(1);
                }
        } else {
                state.conf.display_to.afinet.sa_family = AF_UNIX;
                strcpy((&state.conf.display_to.afunix)->sun_path,
                       state.conf.ui_sock_path);
        }

        if (!state.conf.verbose)
                redir_log(&state);

        if (state.conf.testing)
                state.digi_quality = 0xFF;

        /* state struct is memset to zero above
         *  take this out */
        for (i = 0; i < KEEP_PACKETS; i++)
                state.recent[i] = NULL;

        printf("%s: Initializing gps\n", getprogname());
        if(gps_init(&state) == -1) {
                fprintf(stderr, "Failed to init gps, exiting");
                exit(1);
        }

        /*
         * Check if playback packets is enabled
         */
        if(state.debug.playback_pkt_filename != NULL) {
                printf("Using canned packets\n");

                state.ax25_recvproto = ETH_P_AX25;
                state.ax25_srcaddr = state.mycall;
                state.disp_idx = -1;
                _ui_send(&state, "AI_CALLSIGN", "HELLO");

                canned_packets(&state);
                fap_cleanup();
                ini_cleanup(&state);

                return 0;
        }

        printf("%s: Using live packets\n", getprogname());

        if (state.conf.tnc && STREQ(state.conf.tnc_type, "KISS")) {
                state.tncfd = serial_open(state.conf.tnc, state.conf.tnc_rate, 1);
                if (state.tncfd < 0) {
                        printf("Failed to open TNC: %m\n");
                        exit(1);
                }
        } else if (STREQ(state.conf.tnc_type, "NET")) {
                state.tncfd = aprsis_connect(state.conf.aprsis_server_host_addr,
                                             state.conf.aprsis_server_port,
                                             state.mycall,
                                             state.conf.aprsis_filter);
                if (state.tncfd < 0) {
                        printf("Sock %i: %m\n", state.tncfd);
                        printf("Failed to connect with host: %s, port: %d, call: %s, filter: %s\n",
                               state.conf.aprsis_server_host_addr,
                               state.conf.aprsis_server_port,
                               state.basecall,
                               state.conf.aprsis_filter);
                        exit(1);
                }
        } else if (STREQ(state.conf.tnc_type, "AX25")) {
#ifdef HAVE_AX25_TRUE
                state.tncfd = aprsax25_connect(&state);
                if (state.tncfd < 0) {
                        printf("Sock %i: %m\n", state.tncfd);
                        return -1;
                }
                pr_debug("tnc type is AX25 try aprsax25_connect to %s, socket=%d\n",
                         state.conf.aprs_path, state.tncfd);

#else
                printf("%s:%s(): tracker tnc type has been configured to use AX.25 but AX.25 lib has not been installed.\n",
                       __FILE__, __FUNCTION__);
#endif /* #ifdef HAVE_AX25_TRUE */


        } else {
                state.tncfd = -1;
                printf("WARNING: TNC %s is not configured.\n", state.conf.tnc_type);
                exit(1);
        }

        if (STREQ(state.conf.tnc_type, "KISS")) {
                handle_display_initkiss(&state);
        }

        /* Test for any telemetry config'ed */
        if (state.conf.tel) {
                state.telfd = serial_open(state.conf.tel, state.conf.tel_rate, 0);
                if (state.telfd < 0) {
                        perror(state.conf.tel);
                        exit(1);
                }
        } else
                state.telfd = -1;

        state.disp_idx = -1;
        _ui_send(&state, "AI_CALLSIGN", "HELLO");

        printf("%s: Entering loop\n", getprogname());
        while (1) {
                int ret;
                struct timeval tv = {1, 0};

                FD_ZERO(&fds);

                if (state.tncfd > 0)
                        FD_SET(state.tncfd, &fds);
                if (state.gpsfd > 0)
                        FD_SET(state.gpsfd, &fds);
                if (state.telfd > 0)
                        FD_SET(state.telfd, &fds);
                if (state.dspfd > 0)
                        FD_SET(state.dspfd, &fds);
                if(state.outstanding_ack_timer_count > 0) {
                        i = 0;
                        while(i < MAX_OUTSTANDING_MSGS) {
                                if(state.ackout[i].timer_fd > 0) {
                                        FD_SET(state.ackout[i].timer_fd, &fds);
                                }
                                i++;
                        }
                }

                /* Only look at read file descriptors */
                ret = select(100, &fds, NULL, NULL, &tv);
                if (ret == -1) {
                        perror("select");
                        if (errno == EBADF)
                                break;
                        continue;
                } else if (ret > 0) {
                        if (FD_ISSET(state.tncfd, &fds)) {
                                handle_incoming_packet(&state);
                                bUpdateUI = true;
                        }
                        if( (state.gpsfd != -1) && FD_ISSET(state.gpsfd, &fds)) {
                                handle_gps_data(&state);
                        }
                        if( (state.telfd != -1) && FD_ISSET(state.telfd, &fds)) {
                                handle_telemetry(&state);
                                bUpdateUI = true;
                        }
                        if ((state.dspfd != -1) && FD_ISSET(state.dspfd, &fds)) {
                                handle_display(&state);
                                bUpdateUI = true;
                        }
                        if (state.outstanding_ack_timer_count > 0) {
                                i = 0;
                                while(i < MAX_OUTSTANDING_MSGS) {
                                        if( (state.ackout[i].timer_fd > 0) &&
                                            (FD_ISSET(state.ackout[i].timer_fd, &fds)) ) {
                                                handle_ack_timer(&state, &state.ackout[i]);
                                        }
                                        i++;
                                }
                        }
                        /* only update UI if something watched changes */
                        if(bUpdateUI) {
                                update_packets_ui(&state);
                                bUpdateUI = false;
                        }
                }
#if 0 /* WHY update display every second ? */
                else {
                        /* Work to do if no other events */
                        update_packets_ui(&state);
                }
#endif
                /* Using a fake gps? */
                if(state.conf.gps_type_int == GPS_TYPE_FAKE) {
                        handle_gps_data(&state);
                }
                beacon(&state);
                fflush(NULL);
        }

        fap_cleanup();
        ini_cleanup(&state);

        return 0;
}
