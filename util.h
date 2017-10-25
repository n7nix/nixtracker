/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */

/* dantracker aprs tracker
 *
 * Copyright 2012 Dan Smith <dsmith@danplanet.com>
 */

#ifndef __UTIL_H
#define __UTIL_H

#include <time.h>
#include <stdbool.h>

#define KPH_TO_MPH(km) (km * 0.621371192)
#define MS_TO_MPH(m) (m * 2.23693629)
#define M_TO_FT(m) (m * 3.2808399)
#define FT_TO_M(ft) (ft / 3.2808399)
#define C_TO_F(c) ((c * 9.0/5.0) + 32)
#define MM_TO_IN(mm) (mm * 0.0393700787)
#define KTS_TO_MPH(kts) (kts * 1.15077945)
#define KTS_TO_KPH(kts) (kts * 1.852)

#define STREQ(x,y) (strcmp(x, y) == 0)
#define STRNEQ(x,y,n) (strncmp(x, y, n) == 0)

#define HAS_BEEN(s, d) ((time(NULL) - s) > d)

#define PI 3.14159265
#define DEG2RAD(x) (x*(PI/180))
#define RAD2DEG(x) (x/(PI/180))
#define MYPOS(s) (&(s)->mypos[(s)->mypos_idx])

#define OBJNAME(p) ((p)->object_or_item_name ? (p)->object_or_item_name : \
                    (p)->src_callsign)

const char *direction(double degrees);
char *get_escaped_string(char *string);
double get_direction(double fLng, double fLat, double tLng, double tLat);
char *strupper(char *s);
char *time2str(time_t *ptime, int format);
char *mtime2str(struct timeval *tvtime, bool bmsec);
int get_base_callsign(char *strOutput, int *ssid, char *strInput);


#endif /* __UTIL_H */
