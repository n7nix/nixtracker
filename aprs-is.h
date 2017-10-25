/* Copyright 2012 Dan Smith <dsmith@danplanet.com> */

#ifndef __APRS_IS_H
#define __APRS_IS_H

#define APRS_PORT_FILTERED_FEED 14580

int aprsis_connect(const char *hostname, int port, const char *mycall, char *filter);
int get_packet_text(int fd, char *buffer, unsigned int *len);

#endif
