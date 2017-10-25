/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */
/* Copyright 2012 Dan Smith <dsmith@danplanet.com> */

#ifndef __APRS_AX25_H
#define __APRS_AX25_H

#define ISAX25ADDR(x) (((x >= 'A') && (x <= 'Z')) || ((x >= '0') && (x <= '9')) || (x == ' '))

bool packet_qualify(struct state *state, struct sockaddr *sa, unsigned char *buf, int size);
void fap_conversion_debug(char *rxbuf, char *buf, int size);
int get_ax25packet(struct state *state, char *buffer, unsigned int *len);
int aprsax25_connect(struct state *state);
bool send_ax25_beacon(struct state *state, char *packet);

#endif /* APRS_AX25_H */
