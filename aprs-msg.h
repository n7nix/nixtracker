/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */
/* Copyright 2013 Basil Gunn <basil@pacabunga.com> */

#ifndef __APRS_MSG_H
#define __APRS_MSG_H

/* time to wait in seconds for initial ACK response */
#define ACK_INTERVAL 30

bool isnewmessage(struct state *state, fap_packet_t *fap);
void webdisplay_message(struct state *state, fap_packet_t *fap);
void webdisplay_thirdparty(struct state *state, fap_packet_t *fap);
void save_message(struct state *state, fap_packet_t *fap);
void handle_aprsMessages(struct state *state, fap_packet_t *fap, char *packet);
void handle_thirdparty(struct state *state, fap_packet_t *fap);
void send_msg_ack(struct state *state, fap_packet_t *fap, char *msg_id);
void send_message(struct state *state, char *to_str, char *mesg_str, char **build_msg);
void handle_ack_timer(struct state *state, ack_outstand_t *ackout);

/* Debug only */
void display_fap_message(struct state *state, fap_packet_t *fap);

#endif /* APRS_MSG_H */
