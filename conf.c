/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */

/* dantracker aprs tracker
 *
 * Copyright 2012 Dan Smith <dsmith@danplanet.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdbool.h>

#include "aprs.h"
#include "aprs-is.h"
#include "ui.h"
#include "util.h"

extern char *__progname;
const char * getprogname(void);

#define TIER2_HOST_NAME ".aprs2.net"

/*
 * If the order of struct items below are changed
 * then need to change switch statement in get_keysubst()
 */
key_subst_t keys[] = {
        {"index",   KEY_TYPE_VARIABLE},
        {"mycall",  KEY_TYPE_STATIC},
        {"temp1",   KEY_TYPE_VARIABLE},
        {"voltage", KEY_TYPE_VARIABLE},
        {"sats",    KEY_TYPE_VARIABLE},
        {"ver",     KEY_TYPE_STATIC},
        {"time",    KEY_TYPE_VARIABLE},
        {"date",    KEY_TYPE_VARIABLE},
        {"digiq",   KEY_TYPE_VARIABLE},
        {NULL, 0}
};

const char * getprogname(void)
{
        return __progname;
}

void show_version(void)
{
        printf("%s v%d.%02d(%d)\n", getprogname(),
                 TRACKER_MAJOR_VERSION,
                 TRACKER_MINOR_VERSION,
                 BUILD);
}

void usage(char *argv0)
{
        printf("Usage:\n"
               "%s [OPTS]\n"
               "Options:\n"
               "  --help, -h       This help message\n"
               "  --tnc, -t        Serial port for TNC\n"
               "  --gps, -g        Serial port for GPS\n"
               "  --telemetry, -T  Serial port for telemetry\n"
               "  --testing        Testing mode (faked speed, course, digi)\n"
               "  --verbose, -v    Log to stdout\n"
               "  --Version, -V    Display version\n"
               "  --conf, -c       Configuration file to use\n"
               "  --display, -d    Host to use for display over INET socket\n"
               "  --netrange, -r   Range (miles) to use for APRS-IS filter\n"
               "  --metric, -m     Display metric units\n"
               "\n",
               argv0);
}

int lookup_host(struct state *state)
{
        const char *hostname =state->conf.ui_host_name;
        struct hostent *host;
        struct sockaddr_in *sa = (struct sockaddr_in *)&state->conf.display_to.afinet;

        printf("Looking up hostname: %s\n", hostname);
        host = gethostbyname(hostname);
        if (!host) {
                perror(hostname);
                return -errno;
        }

        if (host->h_length < 1) {
                fprintf(stderr, "No address for %s\n", hostname);
                return -EINVAL;
        }

        sa->sin_family = AF_INET;
        sa->sin_port = htons(state->conf.ui_inet_port);
        memcpy(&sa->sin_addr, host->h_addr_list[0], sizeof(sa->sin_addr));
        fprintf(stderr, "Found address for hostname: %s\n", hostname);
        return 0;
}

int parse_opts(int argc, char **argv, struct state *state)

{
        static struct option lopts[] = {
                {"help",      0, 0, 'h'},
                {"tnc",       1, 0, 't'},
                {"gps",       1, 0, 'g'},
                {"telemetry", 1, 0, 'T'},
                {"testing",   0, 0,  1 },
                {"verbose",   0, 0, 'v'},
                {"Version",   0, 0, 'V'},
                {"conf",      1, 0, 'c'},
                {"display",   1, 0, 'd'},
                {"netrange",  1, 0, 'r'},
                {"metric",    0, 0, 'm'},
                {NULL,        0, 0,  0 },
        };


        state->conf.aprsis_range = 100;

        while (1) {
                int c;
                int optidx;

                c = getopt_long(argc, argv, "mht:g:T:c:svVd:r:",
                                lopts, &optidx);
                if (c == -1)
                        break;

                switch(c) {
                        case 'h':
                                usage(argv[0]);
                                exit(1);
                        case 't':
                                state->conf.tnc = optarg;
                                break;
                        case 'g':
                                state->conf.gps = optarg;
                                break;
                        case 'T':
                                state->conf.tel = optarg;
                                break;
                        case 1:
                                state->conf.testing = 1;
                                break;
                        case 'v':
                                state->conf.verbose = 1;
                                break;
                        case 'V':
                                show_version();
                                exit(0);
                        case 'c':
                                state->conf.config = optarg;
                                break;
                        case 'd':
                                state->conf.ui_host_name = optarg;
                                break;
                        case 'r':
                                state->conf.aprsis_range = \
                                        (unsigned int)strtoul(optarg, NULL, 10);
                                break;
                        case 'm':
                                state->conf.metric_units = true;
                                break;
                        case '?':
                                printf("Unknown option\n");
                                return -1;
                };
        }

        return 0;
}

char *process_tnc_cmd(char *cmd)
{
        char *ret;
        char *a, *b;

        ret = malloc((strlen(cmd) * 2) + 1);
        if (ret < 0)
                return NULL;

        for (a = cmd, b = ret; *a; a++, b++) {
                if (*a == ',')
                        *b = '\r';
                else
                        *b = *a;
        }

        *b = '\0';

        //printf("TNC command: `%s'\n", ret);

        return ret;
}

char **parse_list(char *string, int *count)
{
        char **list;
        char *ptr;
        int i = 0;

        for (ptr = string; *ptr; ptr++)
                if (*ptr == ',')
                        i++;
        *count = i+1;

        list = calloc(*count, sizeof(char **));
        if (!list)
                return NULL;

        for (i = 0; string; i++) {
                ptr = strchr(string, ',');
                if (ptr) {
                        *ptr = 0;
                        ptr++;
                }
                list[i] = strdup(string);
                string = ptr;
        }

        return list;
}

/*
 * Check for a substitution key  $key$
 */
bool has_key(char *str)
{
        char * keydelim_1;
        char * keydelim_2;
        bool retcode = false;

        if( (keydelim_1 = strchr(str, '$')) == NULL) {
                return false;
        }
        if( (keydelim_2 = strrchr(str, '$')) == NULL) {
                return false;
        }
        if(keydelim_2 != keydelim_1) {
                retcode = true;
        }
        return retcode;
}

/*
 * Return pointer to first character in string past last key value
 * delimiter.
 */
char *get_key(char *cfg_line, char *key_val)
{
        char *keydelim;
        int key_len;

        if( (keydelim = strchr(cfg_line, '$')) == NULL) {
                return NULL;
        }
        /* point past first $ */
        keydelim++;

        /* get length of key */
        if((key_len = strcspn(keydelim, "$")) == 0 ) {
                return NULL;
        }

        /* copy found key to supplied key pointer */
        strncpy(key_val, keydelim, key_len);
        key_val[key_len] = '\0';

        return(keydelim+ key_len + 1);
}

/*
 * Return index into struct array for key string.
 * or -1 if it doesn't exist
 */
int check_key(char *keystr) {

        int i = 0;

        while(keys[i].key_val != NULL) {
                if(STREQ(keystr, keys[i].key_val)) {
                        return i;
                }
                i++;
        }

        return -1;
}

/* Get a substitution value for a given key (result must be free()'d) */
char *get_keysubst(struct state *state, int keyind)
{
        char *value = NULL;
        struct tm tm;
        char timestr[16];
        time_t t;

        switch (keyind) {
                case 0: /* index */
                        asprintf(&value, "%i",
                                 state->comment_idx++ % state->conf.comments_count);
                        break; /* mycall */
                case 1:
                        value = strdup(state->basecall);
                        break;
                case 2: /* temp1 */
                        asprintf(&value, "%.0f", state->tel.temp1);
                        break;
                case 3: /* voltage */
                        asprintf(&value, "%.1f", state->tel.voltage);
                        break;
                case 4: /* sats */
                        asprintf(&value, "%i", MYPOS(state)->sats);
                        break;
                case 5: /* ver */
                        asprintf(&value, "v%d.%02d(%d)",
                                 TRACKER_MAJOR_VERSION,
                                 TRACKER_MINOR_VERSION,
                                 BUILD);
                        break;
                case 6: /* time */
                        t = time(NULL);
                        localtime_r(&t, &tm);
                        strftime(timestr, sizeof(timestr), "%H:%M:%S", &tm);
                        value = strdup(timestr);
                        break;
                case 7: /* date */
                        t = time(NULL);
                        localtime_r(&t, &tm);
                        strftime(timestr, sizeof(timestr), "%m/%d/%Y", &tm);
                        value = strdup(timestr);
                        break;
                case 8: /* digiq */
                        {
                        int count = 0, i;
                        for (i = 0; i < 8; i++)
                                count += (state->digi_quality >> i) & 0x01;
                        asprintf(&value, "%02d%%", (count >> 3) * 100);
                        }
                        break;
                default:
                        fprintf(stderr, "%s: Bad key index %d",__FUNCTION__, keyind);
                        break;
        }

        return value;
}

/* Given a string with substitution variables, do the first substitutions
 * and return the sub string for the substitution.
 * Also return an index to the string after the substitution variable.
 */
char *process_single_key(struct state *state, char *src, int keyindex)
{
        char *str, *dupstr;
        char *ptr1;
        char *ptr2;
        char key[MAX_KEY_LEN] = "";
        char *value = NULL;
        int linesize_nokey = strlen(src);

        /* get enough memory to hold string plus substituted key */
        if( (str = malloc(MAX_SUBST_LINE)) == NULL) {
                fprintf(stderr, "%s: malloc error: %s\n",
                       __FUNCTION__, strerror(errno));
                return str;
        }

        str[0] = '\0';
        ptr1 = src;

        if(src != NULL && ((ptr2 = strchr(ptr1, '$')) !=NULL)) {

                /* Copy up to the next variable */
                strncat(str, ptr1, ptr2-ptr1);

                ptr1 = ptr2+1;
                ptr2 = strchr(ptr1, '$');
                if (!ptr2) {
                        fprintf(stderr, "%s: Bad substitution `%s'\n",
                                  __FUNCTION__, ptr1);
                        goto err;
                }

                /* make a copy of the key */
                strncpy(key, ptr1, ptr2 - ptr1);
                /* reduce line size by key size $key$ */
                linesize_nokey -= ((ptr2 - ptr1) + 2);

                value = get_keysubst(state, keyindex);
                if (value) {
                        /* test for overflow */
                        if(strlen(value) + linesize_nokey > MAX_SUBST_LINE - 1) {
                                fprintf(stderr, "%s: Substitution line overflow `%s'\n",
                                          __FUNCTION__, src);
                                goto err;
                        }
                        strcat(str, value);
                        free(value);
                }
                /* malloc'd a generous amount of memory for the initial string
                 * substitution, adjust size of allocated memory for this string */
                dupstr = strdup(str);
                free(str);
                return dupstr;
        }
err:
        free(str);
        return NULL;
}

char *key_subst(struct state *state, char *key_line, char* buildstr)
{
        char keyval[MAX_KEY_LEN];
        char *newstr;
        char *newkey_line;
        int key_ind;


        newkey_line = get_key( key_line, keyval);

        if( (newkey_line != NULL) && ((key_ind=check_key(keyval)) == -1) ) {
                fprintf(stderr, "ERROR: no key found for %s in line:\n%s\n",
                          keyval, key_line);
                return NULL;
        }
        if(keys[key_ind].key_type == KEY_TYPE_STATIC || state->debug.parse_ini_test) {
                newstr = process_single_key(state, key_line, key_ind);
#ifdef DEBUG
                printf("Processed single key: %s, key: %s, remaining: %zu\n",
                       newstr, keyval, strlen(newkey_line));
#endif /* DEBUG */
                if(newstr != NULL) {
                        strcat(buildstr, newstr);
                        free(newstr);
                }

        } else {
                /* copy unmodified string */
                strncat(buildstr, key_line, newkey_line-key_line);
        }
        return (newkey_line);
}


/* Given a string with substitution variables, do the substitutions
 * and return the new result (which must be free()'d)
 */
char *process_subst(struct state *state, char *src)
{
        char *str, *dupstr;
        char *ptr1;
        char *ptr2;
        int linesize_nokey = strlen(src);

        /* get enough memory to hold string plus substituted key(s) */
        if( (str = malloc(MAX_SUBST_LINE)) == NULL) {
                printf("%s: malloc error: %s\n",
                       __FUNCTION__, strerror(errno));
                return str;
        }

        str[0] = 0;

        for (ptr1 = src; *ptr1; ptr1++) {
                char key[MAX_KEY_LEN] = "";
                char *value = NULL;

                ptr2 = strchr(ptr1, '$');
                if (!ptr2) {
                        /* No more substs */
                        strcat(str, ptr1);
                        break;
                }

                /* Copy up to the next variable */
                strncat(str, ptr1, ptr2-ptr1);

                ptr1 = ptr2+1;
                ptr2 = strchr(ptr1, '$');
                if (!ptr2) {
                        printf("Bad substitution `%s'\n", ptr1);
                        goto err;
                }

                /* make a copy of the key */
                strncpy(key, ptr1, ptr2 - ptr1);
                /* reduce line size by key size $key$ */
                linesize_nokey -= ((ptr2 - ptr1) + 2);

                ptr1 = ptr2;

                value = get_keysubst(state, check_key(key));
                if (value) {
                        /* test for overflow */
                        if(strlen(value) + linesize_nokey > MAX_SUBST_LINE - 1) {
                                printf("Substitution line overflow `%s'\n", src);
                                goto err;
                        }
                        linesize_nokey += strlen(value);
                        strcat(str, value);
                        free(value);
                }
        }

        /* malloc'd a generous amount of memory for the initial string
         * substitution, adjust size of allocated memory for this string */
        dupstr = strdup(str);
        free(str);
        return dupstr;
err:
        free(str);
        return NULL;
}

int parse_ini(char *filename, struct state *state)
{
        dictionary *ini;
        char *tmp;
        char *basecallsign;
        static char *socketpath;
        char **subst_str[25];
        int subst_line_cnt = 0;

        ini = iniparser_load(filename);
        if (ini == NULL)
                return -EINVAL;

        state->conf.ini_dict = ini; /* save the ini parser dictionary */
        state->conf.aprs_message_ack = true;

         /* Manually set which packets to display on console
         *  until the parser can handle it */
        state->debug.console_display_filter |= (CONSOLE_DISPLAY_MSG | CONSOLE_DISPLAY_PKTOUT | CONSOLE_DISPLAY_FAPERR);

        /* To disable the record and playback pkt functions comment
         * out the appropriate line in the ini file.
         */
        state->debug.record_pkt_filename = iniparser_getstring(ini, "debug:record", NULL);
        state->debug.playback_pkt_filename = iniparser_getstring(ini, "debug:playback", NULL);
        state->debug.playback_time_scale = iniparser_getint(ini, "debug:playback_scale", 1);
        tmp = strdup(iniparser_getstring(ini, "debug:parse_ini_test", "OFF"));
        strupper(tmp);
        state->debug.parse_ini_test = STREQ(tmp, "OFF") ? false : true;
        state->debug.verbose_level = iniparser_getint(ini, "debug:verbose_level", 0);

        tmp = strdup(iniparser_getstring(ini, "debug:display_fap_enable", "OFF"));
        strupper(tmp);
        state->debug.display_fap_enable = STREQ(tmp, "OFF") ? false : true;

        tmp = strdup(iniparser_getstring(ini, "debug:display_spy_enable", "OFF"));
        strupper(tmp);
        state->debug.display_spy_enable = STREQ(tmp, "OFF") ? false : true;

        tmp = strdup(iniparser_getstring(ini, "debug:console_spy_enable", "OFF"));
        strupper(tmp);
        state->debug.console_spy_enable = STREQ(tmp, "OFF") ? false : true;

        if (state->debug.display_fap_enable) {
                printf("FAB display enabled\n");
        }

        if (state->debug.display_spy_enable) {
                printf("SPY display enabled\n");
        }

        if (!state->conf.tnc)
                state->conf.tnc = (char *)iniparser_getstring(ini, "tnc:port", NULL);
        state->conf.tnc_rate = iniparser_getint(ini, "tnc:rate", 9600);
        state->conf.tnc_type = (char *)iniparser_getstring(ini, "tnc:type", "KISS");

        tmp = (char *)iniparser_getstring(ini, "tnc:init_kiss_cmd", "");
        state->conf.init_kiss_cmd = process_tnc_cmd(tmp);

        if (!state->conf.gps)
                state->conf.gps = (char *)iniparser_getstring(ini, "gps:port", NULL);
        state->conf.gps_rate = iniparser_getint(ini, "gps:rate", 4800);
        state->conf.gps_type = (char *)iniparser_getstring(ini, "gps:type", "static");

        /* set default gps device static/fake */
        state->conf.gps_type_int = GPS_TYPE_FAKE;

        /* set gps device based on ini file string */
        if(STREQ(state->conf.gps_type, "gpsd")) {
                state->conf.gps_type_int = GPS_TYPE_GPSD;
        } else if(STREQ(state->conf.gps_type, "serial")) {
                state->conf.gps_type_int = GPS_TYPE_SERIAL;
        }

        /* set period in minutes to update system time from gps
         *  default is once a day */
        state->conf.gps_time_update = iniparser_getint(ini, "gps:time_update_period", 60*24);

        printf("CONF debug: gps system update time every %d minutes\n", state->conf.gps_time_update);

        /* Build the TIER 2 host name */
        tmp = (char *)iniparser_getstring(ini, "net:server_host_address", "oregon");

        state->conf.aprsis_server_host_addr = calloc(sizeof(tmp)+ 1 + sizeof(TIER2_HOST_NAME) + 1, sizeof(char));
        sprintf(state->conf.aprsis_server_host_addr,"%s%s", tmp, TIER2_HOST_NAME);

        /* Get APRS server port */
        state->conf.aprsis_server_port = iniparser_getint(ini, "net:server_port", APRS_PORT_FILTERED_FEED);

        /* set range to use for APRS net_server_host_address:net_server_port
         *  - for [TNC] type=net to suck packets from the aprs-is
         *  server specified above.
         */
        state->conf.aprsis_range = iniparser_getint(ini, "net:range", 100);
        state->conf.aprsis_filter = (char *)iniparser_getstring(ini, "net:server_filter", NULL);

        /* Get the AX25 port name */
        state->conf.ax25_port = (char *)iniparser_getstring(ini, "ax25:port", "undefined");

        /* Get the AX25 device filter setting */
        tmp = strdup(iniparser_getstring(ini, "ax25:device_filter", "OFF"));
        strupper(tmp);
        if(STREQ(tmp, "OFF")) {
                state->conf.ax25_pkt_device_filter = AX25_PKT_DEVICE_FILTER_OFF;
        } else if(STREQ(tmp, "ON")) {
                state->conf.ax25_pkt_device_filter = AX25_PKT_DEVICE_FILTER_ON;
        } else {
                state->conf.ax25_pkt_device_filter = AX25_PKT_DEVICE_FILTER_UNDEFINED;
        }

        /* Get the APRS transmit path */
        state->conf.aprs_path = (char *)iniparser_getstring(ini, "ax25:aprspath", "");

        if (!state->conf.tel)
                state->conf.tel = (char *)iniparser_getstring(ini, "telemetry:port",
                        NULL);
        state->conf.tel_rate = iniparser_getint(ini, "telemetry:rate", 9600);

        state->mycall = (char *)iniparser_getstring(ini, "station:mycall", "N0CALL-7");

        /* Verify call sign */
        basecallsign=strdup(state->mycall);
        if(!get_base_callsign(basecallsign, &state->myssid, state->mycall)) {
                printf("Error parsing base callsign from %s", state->mycall);
                return(-1);
        }

        if(STREQ(basecallsign, "NOCALL")) {
                printf("Configure file %s with your callsign\n",
                       state->conf.config ? state->conf.config : "aprs.ini");
                return(-1);
        }
        state->basecall = basecallsign;

        pr_debug("Calling iniparser for station call %s, ssid:%d\n",
                 state->basecall, state->myssid);

        state->conf.ui_host_name = (char *)iniparser_getstring(ini, "ui_net:sock_hostname", "");
        printf("CONF debug: sock_host name string length = %zd\n", strlen(state->conf.ui_host_name));

        if (strlen(state->conf.ui_host_name) == 0 ) {
                state->conf.ui_host_name = NULL;
        } else {
                printf("got this hostname: %s\n", state->conf.ui_host_name);
        }

        /* If host address arg isn't defined us UNIX socket path */
        asprintf(&socketpath, "/tmp/%s_UI", basecallsign);
        state->conf.ui_sock_path = (char *)iniparser_getstring(ini, "ui_net:unix_socket", socketpath);
        if(has_key(state->conf.ui_sock_path)) {
                subst_str[subst_line_cnt] = &state->conf.ui_sock_path;
                subst_line_cnt++;
        }

        state->conf.ui_inet_port = iniparser_getint(ini, "ui_net:sock_port", 9123);


        /* configure what gets display for packet spy */
        tmp = strdup(iniparser_getstring(ini, "display:time", "OFF"));
        strupper(tmp);
        state->conf.spy_format |= STREQ(tmp, "OFF") ? 0 : SPY_FORMAT_TIME_ENABLE;

        tmp = strdup(iniparser_getstring(ini, "display:time_msec", "OFF"));
        strupper(tmp);
        state->conf.spy_format |= STREQ(tmp, "OFF") ? 0 : SPY_FORMAT_TIME_RESOLUTION;

        tmp = strdup(iniparser_getstring(ini, "display:port", "OFF"));
        strupper(tmp);
        state->conf.spy_format |= STREQ(tmp, "OFF") ? 0 : SPY_FORMAT_PORT_ENABLE;

        tmp = strdup(iniparser_getstring(ini, "display:packet_length", "OFF"));
        strupper(tmp);
        state->conf.spy_format |= STREQ(tmp, "OFF") ? 0 : SPY_FORMAT_PKTLEN_ENABLE;


        state->conf.icon = (char *)iniparser_getstring(ini, "station:icon", "/>");

        if (strlen(state->conf.icon) != 2) {
                printf("ERROR: Icon must be two characters, not `%s'\n",
                       state->conf.icon);
                return -1;
        }

        state->conf.digi_path = (char *)iniparser_getstring(ini, "station:digi_path",
                "WIDE1-1,WIDE2-1");

        state->conf.power = iniparser_getint(ini, "station:power", 0);
        state->conf.height = iniparser_getint(ini, "station:height", 0);
        state->conf.gain = iniparser_getint(ini, "station:gain", 0);
        state->conf.directivity = iniparser_getint(ini, "station:directivity",
                0);

        state->conf.atrest_rate = iniparser_getint(ini,
                "beaconing:atrest_rate",
                600);
        state->conf.sb_low.speed = iniparser_getint(ini,
                "beaconing:min_speed",
                10);
        state->conf.sb_low.int_sec = iniparser_getint(ini,
                "beaconing:min_rate",
                600);
        state->conf.sb_high.speed = iniparser_getint(ini,
                "beaconing:max_speed",
                60);
        state->conf.sb_high.int_sec = iniparser_getint(ini,
                "beaconing:max_rate",
                60);
        state->conf.course_change_min = iniparser_getint(ini,
                "beaconing:course_change_min",
                30);
        state->conf.course_change_slope = iniparser_getint(ini,
                "beaconing:course_change_slope",
                255);
        state->conf.after_stop = iniparser_getint(ini,
                "beaconing:after_stop",
                180);

        state->conf.static_lat = iniparser_getdouble(ini,
                "static:lat",
                0.0);
        state->conf.static_lon = iniparser_getdouble(ini,
                "static:lon",
                0.0);
        state->conf.static_alt = iniparser_getdouble(ini,
                "static:alt",
                0.0);
        state->conf.static_spd = iniparser_getdouble(ini,
                "static:speed",
                0.0);
        state->conf.static_crs = iniparser_getdouble(ini,
                "static:course",
                0.0);

        state->conf.digi_alias = (char *)iniparser_getstring(ini, "digi:alias",
                "TEMP1-1");
        state->conf.digi_enabled = iniparser_getint(ini, "digi:enabled", 0);
        state->conf.digi_append = iniparser_getint(ini, "digi:append_path",
                0);
        state->conf.digi_delay = iniparser_getint(ini, "digi:txdelay", 500);

        tmp = (char *)iniparser_getstring(ini, "station:beacon_types", "posit");
        if (strlen(tmp) != 0) {
                char **types;
                int count;
                int i;


                types = parse_list(tmp, &count);

                printf("CONF debug beacon types count %d, beacon: %s\n", count, tmp);

                if (!types) {
                        printf("Failed to parse beacon types\n");
                        return -EINVAL;
                }

                for (i = 0; i < count; i++) {
                        if (STREQ(types[i], "weather"))
                                state->conf.do_types |= DO_TYPE_WX;
                        else if (STREQ(types[i], "phg"))
                                state->conf.do_types |= DO_TYPE_PHG;
                        else
                                printf("WARNING: Unknown beacon type %s\n",
                                       types[i]);
                        free(types[i]);
                }
                free(types);
        }

        tmp = strdup(iniparser_getstring(ini, "locale:units", "imperial"));
        strupper(tmp);
        state->conf.metric_units = STREQ(tmp, "METRIC") ? true : false;

        tmp = (char *)iniparser_getstring(ini, "comments:enabled", "");
        printf("CONF debug comments count %zd, comment: %s\n", strlen(tmp), tmp);

        if (strlen(tmp) != 0) {
                int i;

                state->conf.comments = parse_list(tmp,
                        &state->conf.comments_count);
                if (!state->conf.comments)
                        return -EINVAL;

                for (i = 0; i < state->conf.comments_count; i++) {
                        char section[32];

                        snprintf(section, sizeof(section),
                                 "comments:%s", state->conf.comments[i]);
                        free(state->conf.comments[i]);
                        state->conf.comments[i] = (char *)iniparser_getstring(ini,
                                section,
                                "INVAL");
                        if(has_key(state->conf.comments[i])) {
                                subst_str[subst_line_cnt] = &state->conf.comments[i];
                                subst_line_cnt++;
                        }
                }
        }

        /*
         * Check that mycall has been set in ini file
         */
        if (STREQ(state->mycall, "NOCALL")) {
                fprintf(stderr, "ERROR: Please config ini file with your call sign\n");
                return -1;
        }

        /*
         * APRS-IS server filter can be built in one of two ways:
         *  - from the static config  used when no GPS is attached
         *    [static]
         *  - from filter string [net] server_filter =
         * If a filter string under [net] config is defined that takes
         * precedence.
         */
        if(state->conf.aprsis_filter == NULL) {
                /* build the aprs-is filter
                 * The GPS may not be be active yet so always take the static
                 * lat, lon
                 * Range filter r/lat/lon/dist
                 */

                asprintf(&state->conf.aprsis_filter, "r/%.3f/%.3f/%.1f",
                         state->conf.static_lat,
                         state->conf.static_lon,
                         (double)state->conf.aprsis_range);
        }

        printf("CONF debug: aprsis filter string: %s\n", state->conf.aprsis_filter);

        /* Any substitution keys found?
         * Process any static substitution keys */
        if(subst_line_cnt > 0) {
                int i;
                char *key_line;
                char buildstr[MAX_SUBST_LINE];
                int key_count = 0;

                /* iterate through collected lines with found key values */
                for(i = 0; i < subst_line_cnt; i++) {
                        buildstr[0] = '\0';

                        key_line = *subst_str[i];

                        while(key_line != NULL && (strlen(key_line) > 0)) {
#ifdef DEBUG_VERBOSE
                                printf("parse key in: %s\n", key_line);
#endif /* DEBUG */
                                key_line = key_subst(state, key_line, buildstr);
                                if(key_line != NULL) {
                                        key_count++;
#if DEBUG_VERBOSE
                                        printf("Building string: count %d, remaining str: %s\n",
                                               key_count, key_line);
#endif /* DEBUG */
                                }
                        }
                        printf("Final str: %s\n", buildstr);
                        *subst_str[i] = strdup(buildstr);
                }
                printf("Processed %d lines, total keys: %d\n", subst_line_cnt, key_count);
        }
        /* Exit if just testing ini parser */
        if(state->debug.parse_ini_test) {
                exit(0);
        }
        return 0;
}

void ini_cleanup(struct state *state)
{
        /* free the previously loaded dictionary */
        iniparser_freedict(state->conf.ini_dict);
}
