/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */

/* dantracker aprs tracker
 *
 * Copyright 2012 Dan Smith <dsmith@danplanet.com>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

#include "util.h"
#include "aprs.h"

const char *CARDINALS[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

const char *direction(double degrees)
{
        return CARDINALS[((int)((degrees + 22.5) / 45.0)) % 8];
}

double get_direction(double fLng, double fLat, double tLng, double tLat)
{
        double rads;
        double result;

        fLng = DEG2RAD(fLng);
        fLat = DEG2RAD(fLat);
        tLng = DEG2RAD(tLng);
        tLat = DEG2RAD(tLat);

        rads = atan2(sin(tLng-fLng)*cos(tLat),
                     cos(fLat)*sin(tLat)-sin(fLat)*cos(tLat)*cos(tLng-fLng));

        result = RAD2DEG(rads);

        if (result < 0)
                return result + 360;
        else
                return result;
}

char *get_escaped_string(char *string)
{
        int i;
        char *escaped = NULL;
        int length = (strlen(string) * 2) + 2;

        escaped = calloc(length, sizeof(char));

        /* Escape values */
        for (i = 0; i < strlen(string); i++) {
                if (strlen(escaped) + 6 >= length) {
                        char *tmp;
                        tmp = realloc(escaped, length * 2);
                        escaped = tmp;
                        length *= 2;
                }

                if (string[i] == '&')
                        strcat(escaped, "&amp;");
                else if (string[i] == '<')
                        strcat(escaped, "&lt;");
                else if (string[i] == '>')
                        strcat(escaped, "&gt;");
                else
                        strncat(escaped, &string[i], 1);
        }

        return escaped;
}

char *time2str(time_t *ptime, int format_type)
{
        struct tm *ptm_now;
        static char tmstring[40];
        time_t curtime;
        time_t *thistime;

        if (ptime == NULL) {
                curtime = time(NULL);
                thistime=&curtime;
        } else {
                thistime=ptime;
        }

        ptm_now = localtime(thistime);
        if(format_type == 0) {
                strftime(tmstring, sizeof(tmstring)-1,
                         "%d.%m.%y %H:%M:%S"
                         ,ptm_now );
        } else {
                /* more appropriate for filenames */
                strftime(tmstring, sizeof(tmstring)-1,
                         "%y%m%d%H%M"
                         ,ptm_now );

        }
        return( tmstring );
}

/*
 * Display current time, option of milliseconds
 */
char *mtime2str(struct timeval *tvtime, bool bmsec)
{
        struct tm *now;
        static char tmstring[40];
        struct timeval curtime,  *thistime;
        struct timezone tmzone;


        if (tvtime == NULL) {
                gettimeofday(&curtime, &tmzone);
                thistime=&curtime;
        } else {
                thistime=tvtime;
        }

        now = localtime(&thistime->tv_sec);
        if(bmsec) {
                sprintf(tmstring,
                          "%d:%02d:%02d:%03ld",
                          now->tm_hour,
                          now->tm_min,
                          now->tm_sec,
                          thistime->tv_usec/1000);
        } else {
                sprintf(tmstring,
                          "%d:%02d:%02d",
                          now->tm_hour,
                          now->tm_min,
                          now->tm_sec);
        }
        return( tmstring );
}

char *strupper(char *s)
{
        unsigned char *cp;

        if (s == NULL) {
                return NULL;
        }
        for (cp = (unsigned char *) s; *cp; cp++) {
                if (islower(*cp)) {
                        *cp = toupper(*cp);
                }
        }
        return s;
}

/*
 * Expect input string is a valid 6 character or less callsign with a
 * dash & 2 numeric character sid.
 */
int get_base_callsign(char *strOutput, int *ssid, char *strInput)
{
        int cnt = 0;
        int retcode = 1;

        while ( (*strInput != '-') && (*strInput != 0) && cnt++ <= MAX_CALLSIGN) {
                *strOutput = toupper(*strInput);
                strInput++;
                strOutput++;
        }
        if(cnt <= MAX_CALLSIGN) {
                *strOutput = '\0';
                *ssid = atoi(strInput + 1);
        } else {
              retcode = 0;
        }

        return(retcode);
}
