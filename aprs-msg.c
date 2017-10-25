/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offs_t: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */
/* Copyright 2013 Basil Gunn <basil@pacabunga.com> */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/timerfd.h>

#include "util.h"
#include "aprs.h"
#include "aprs-ax25.h"
#include "aprs-msg.h"


void handle_thirdparty(struct state *state, fap_packet_t *fap) {
        printf("%s: handle encap\n", time2str(fap->timestamp, 0));
}

#ifdef  WEBAPP
char *format_msgtime(time_t *timestamp)
{
        static char timestr[32];
        struct tm *tm;
        time_t curtime;
        time_t *thistime;

        if(timestamp == NULL) {
                curtime = time(NULL);
                thistime=&curtime;
        }else {
                thistime=timestamp;
        }
        tm=localtime(thistime);

        strftime(timestr, sizeof(timestr), "%H%M", tm);

        return timestr;
}

bool isnewmessage(struct state *state, fap_packet_t *fap)
{
        aprsmsg_t *lastmsg;
        int i;

        /*
         * Check for an ack
         */
        if(fap->message_ack != NULL) {
                return(false);
        }

        for(i = 0; i < KEEP_MESSAGES; i++) {
                if(state->msghist[i] != NULL) {

                        lastmsg = state->msghist[i];

#if 0
                        printf("Message: Comparing these src calls: %s, %s, %d\n",
                               lastmsg->srccall, OBJNAME(fap), STREQ(lastmsg->srccall, OBJNAME(fap) ) );

                        printf("Message: Comparing these dst calls: %s, %s, %d\n",
                               lastmsg->dstcall, fap->destination, STREQ(lastmsg->dstcall, fap->destination) );

                        printf("Message: Comparing these msgs: %s, %s, %d\n",
                               lastmsg->message, fap->message, STREQ(lastmsg->message, fap->message) );
#endif

                        /*
                         * If the timestamps are ever compared need to add in
                         * some slop for jitter.
                         */
                        if( STREQ(lastmsg->srccall, OBJNAME(fap) ) &&
                            STREQ(lastmsg->dstcall, fap->destination) &&
                            STREQ(lastmsg->message, fap->message) ) {
                                return(false);
                        }
                }
        }

        return(true);
}

void save_message(struct state *state, fap_packet_t *fap)
{
        int msgind = state->msghist_idx;
        char basecall[MAX_CALLSIGN];
        int ssid;
        aprsmsg_t *aprsmsg;

        assert(msgind < KEEP_MESSAGES && msgind >= 0);

        if(state->msghist[msgind] != NULL) {
                aprsmsg = state->msghist[msgind];
                free(aprsmsg->message);
                aprsmsg->message = NULL;
                if (aprsmsg->message_id != NULL) {
                        free(aprsmsg->message_id);
                        aprsmsg->message_id = NULL;
                }
        } else {
                if( (state->msghist[msgind] = calloc(sizeof(aprsmsg_t), 1)) == NULL) {
                        printf("%s: malloc message struct error: %s\n",
                               __FUNCTION__, strerror(errno));
                        return;
                }
                aprsmsg = state->msghist[msgind];
        }

        if( (aprsmsg->message = malloc(strlen(fap->message) + 1)) == NULL){
                printf("%s: malloc message error: %s\n",
                       __FUNCTION__, strerror(errno));
                free(aprsmsg);
                state->msghist[msgind] = NULL;
                return;
        }

        if(fap->timestamp == NULL) {
                aprsmsg->timestamp = time(NULL);
        } else {
                aprsmsg->timestamp = *fap->timestamp;
        }

        strcpy(aprsmsg->message, fap->message);
        strncpy(aprsmsg->srccall, fap->src_callsign, MAX_CALLSIGN);
        strncpy(aprsmsg->dstcall, fap->destination, MAX_CALLSIGN);

        printf("Message: storing this src call: %s, check: %s\n",
               aprsmsg->srccall, OBJNAME(fap) );

        /*
         * Handle message sent with an ID which is an ACK request
         */
        if(fap->message_id != NULL) {
                if( (aprsmsg->message_id=calloc(MAX_MSGID + 1, 1)) == NULL) {
                        printf("%s: malloc message id error: %s\n",
                               __FUNCTION__, strerror(errno));
                        return;
                }
                aprsmsg->acked = false;
                strncpy(aprsmsg->message_id, fap->message_id, MAX_MSGID);
                get_base_callsign(basecall, &ssid, aprsmsg->dstcall);
                printf("callsign check: %s %s\n", basecall, aprsmsg->dstcall);
        }

        printf("save_message index: %d, from: %s, to: %s, ack: %s, id: %s, Msg: %s\n",
               msgind,
               fap->src_callsign, fap->destination,
               (fap->message_ack == NULL) ? "None" : fap->message_ack,
               (fap->message_id == NULL) ? "None" : fap->message_id,
               fap->message);

        msgind++;
        state->msghist_idx = msgind & (KEEP_MESSAGES - 1);
}

void webdisplay_message(struct state *state, fap_packet_t *fap)
{
        char buf[512];

        printf("webdisplay_message: from: %s, to: %s, ack: %s, nack: %s, id: %s, Msg: %s\n",
               fap->src_callsign, fap->destination,
               (fap->message_ack == NULL) ? "None" : fap->message_ack,
               (fap->message_nack == NULL) ? "None" : fap->message_nack,
               (fap->message_id == NULL) ? "None" : fap->message_id,
               fap->message);

        snprintf(buf, sizeof(buf), "%s->%s@%s:%s",
                 fap->src_callsign,
                 fap->destination,
                 format_msgtime(fap->timestamp),
                 fap->message);

        _ui_send(state, "MS_MESSAGE", buf);
}

/*
 * Copy a string containing a callsign to a destination buffer.
 */
int parse_callsign(char *pSign, char *callBuf)
{
        char *inBuf=pSign;
        int signLen=0;

        while( (signLen < 10) && (isalnum(*inBuf) || *inBuf == '-') ) {
                callBuf[signLen] = *inBuf;
                signLen++;
                inBuf++;
        }
        callBuf[signLen] = '\0';
        return( strlen(callBuf) );
}

bool isEncap(fap_packet_t *fap, char **encap) {

        char *encap_start;

        /* Check for a third party packet */
        if( ((encap_start = strchr(fap->orig_packet, ':')) != NULL)
            && *(encap_start + 1) == '}' ) {
                *encap = encap_start + 2;
                return true;
        }

        return false;
}

#define MAX_ACK_RETRY_COUNT 4
int retry_interval_mult[] = {1,1,2,4,8};
/*
 * Start a timer for retrying message
 */
int pending_ack_timer(ack_outstand_t *ackout, int ack_request_number)
{
        int fd;
        int interval = ACK_INTERVAL*retry_interval_mult[ack_request_number];
        struct itimerspec *newitimer;

        if( (newitimer = (struct itimerspec *)calloc(sizeof(struct itimerspec), 1)) == NULL) {
                printf("%s: malloc itimerspec error: %s\n",
                       __FUNCTION__, strerror(errno));
                return -1;
        }

        fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
        if (fd == -1) {
                printf("%s: timerfd_create error: %s\n",
                       __FUNCTION__, strerror(errno));
                free(newitimer);
                return -1;
        }
        /* Save timer state before starting timer */
        printf("pending ack timer with fd = 0x%02x\n", fd);
        ackout->timer_fd = fd;
        ackout->ack_retry_count = ack_request_number;
        ackout->needs_ack = true;
        ackout->itimerspec = newitimer;

        /* Start a relative timer */
        newitimer->it_value.tv_nsec = 0;
        newitimer->it_value.tv_sec = interval;
        if(timerfd_settime(fd, 0, newitimer, NULL) == -1) {
                printf("%s: timerfd_settime error: %s\n",
                       __FUNCTION__, strerror(errno));
        }

        return 1;
}

int clear_ack_request(ack_outstand_t *ackout)
{
        ackout->needs_ack = false;
        if(ackout->aprs_msg != NULL) {
                free(ackout->aprs_msg);
        }
        ackout->aprs_msg = NULL;
        /* Disable timer */
        ackout->itimerspec->it_value.tv_nsec = 0;
        ackout->itimerspec->it_value.tv_sec = 0;
        if(timerfd_settime(ackout->timer_fd, 0,
                           ackout->itimerspec, NULL) == -1) {
                printf("%s: timerfd_settime error: %s\n",
                       __FUNCTION__, strerror(errno));
        }
        close(ackout->timer_fd);
        ackout->timer_fd = 0;
        free(ackout->itimerspec);

        return 1;
}

void handle_ack_timer(struct state *state, ack_outstand_t *ackout) {

        ssize_t readsize;
        uint64_t exp_count;

        printf("%s: *** handle_ack_timer for %d\n",
               time2str(NULL, 0),
               ackout->ack_msgid);

        /* consume the expiration event */
        readsize = read(ackout->timer_fd, &exp_count, sizeof(uint64_t));
        if (readsize != sizeof(uint64_t)) {
                printf("%s: timerfd_read error: %s\n",
                       __FUNCTION__, strerror(errno));
                return;
        }
        if(ackout->ack_retry_count >= MAX_ACK_RETRY_COUNT) {
                clear_ack_request(ackout);
                state->outstanding_ack_timer_count--;

        } else {
                printf("RESENDING msg, handle_ack_timer for %d, retry: %d\n",
                       ackout->ack_msgid, ackout->ack_retry_count);

                /* bump retry packet count */
                state->stats.retryPktCount++;

                /* Send message again */
                send_beacon(state, ackout->aprs_msg);
                ackout->ack_retry_count++;
                /* Re-arm timer */
                ackout->itimerspec->it_value.tv_sec = ACK_INTERVAL*retry_interval_mult[ackout->ack_retry_count];
                if(timerfd_settime(ackout->timer_fd, 0,
                                   ackout->itimerspec, NULL) == -1) {
                        printf("%s: timerfd_settime error: %s\n",
                               __FUNCTION__, strerror(errno));
                }
        }
}

void handle_aprsMessages(struct state *state, fap_packet_t *fap, char *packet) {

        int ack_ind;
        bool isnew;
        char *tokstr;

        if(fap->destination != NULL) {
                printf("\n%s Msg [%lu]: type: 0x%02x, src: %s, dest: %s msg: %s\n",
                       time2str(fap->timestamp, 0),
                       state->stats.inPktCount,
                       *fap->type,
                       state->mycall,
                       fap->destination,
                       packet);
        }
        /*
         * Qualify packet source address
         */
        if(fap->src_callsign != NULL &&
           STRNEQ(fap->src_callsign,
                  state->mycall,
                  strlen(state->mycall))) {
                printf("   mycall matches src callsign drop!\n");
                return;
        }

        /*
         * Qualify packet destination address
         */
        if(fap->destination != NULL &&
           STRNEQ(fap->destination,
                  state->mycall,
                  strlen(state->mycall))) {
                printf("   mycall matches destination ok!\n");
                /*
                 * Qualify message as an ACK - libfap sucks
                 */
                if(fap->message_ack != NULL) {
                        ack_ind = atoi(fap->message_ack) & (MAX_OUTSTANDING_MSGS - 1);
                        /* mark message as acked & stop retrys */
                        pr_debug("Receiving aprs ack: %s, index %d %d\n",
                                 fap->message_ack, ack_ind, state->ackout[ack_ind].ack_msgid);

                        /* Qualify ack state & turn off
                         * "waiting for ack"
                         */
                        if ( state->ackout[ack_ind].ack_msgid == ack_ind) {
                                if(!state->ackout[ack_ind].needs_ack) {
                                        pr_debug("aprs msg already acked: %s, index %d %d\n",
                                                fap->message_ack, ack_ind, state->ackout[ack_ind].ack_msgid);
                                } else {
                                        printf("Received an ACK for msgid %d\n", ack_ind);
                                        clear_ack_request(&state->ackout[ack_ind]);
                                        state->outstanding_ack_timer_count--;
                                }
                        }
                } else {
                        /* message_ack pointer is null but check
                         * for an ack sent to our call sign anyways */
                        tokstr=strstr(packet, ":ack");
                        if(tokstr != NULL)  {
                                pr_debug("Found ack [%s] with index: %d in this msg [%s]\n",
                                         tokstr, atoi(tokstr+4), packet);
                                return;
                        }

                        /* Here for message sent to our callsign */
                        pr_debug("!!! send_msg_ack to %s, msg:%s\n", fap->src_callsign, fap->message );
                        pr_debug("Receiving aprs message from %s requiring an ack id: %s\n",
                                 fap->src_callsign, fap->message_id);

                        /*
                         * Qualify messsage requiring an ACK
                         */
                        if(fap->message_id != NULL) {

                                /* send a msg ack for every message
                                 * received */
                                isnew = isnewmessage(state, fap);
                                send_msg_ack(state, fap, fap->message_id);
                                printf(", for %s message\n", isnew ? "new" : "OLD");
                        } else {
                                /*  - fix libfap */

                                pr_debug("Would have sent ack to: [%s] but message_id was NULL\n", packet);
                                tokstr=strstr(packet,"{");
                                if(tokstr != NULL) {
                                        pr_debug("parse for id found this: %s\n", tokstr);
                                        send_msg_ack(state, fap, tokstr+1);
                                }
                        }
                }
        }
}

/*
 * Display third party packets
 */
void webdisplay_thirdparty(struct state *state, fap_packet_t *fap)
{
        char buf[512];
        char fapbuf[512];
        char tmpbuf[16];
        char *nextchar = NULL;
        char *dst_callsign;
        fap_packet_t *encap_fap = NULL;

        if(isEncap(fap, &nextchar)) {
                strcpy(fapbuf, nextchar);
        } else {
                printf("%s: Failed to verify encapsultated packet %s\n",
                       __FUNCTION__, fap->orig_packet);
                display_fap_pkt("third party verify failed", fap);
                return;
        }

        /* Validated a 3rd party message packet, now parse it */
        encap_fap=dan_parseaprs(fapbuf, strlen(fapbuf), 0);

        if(encap_fap == NULL) {
                printf("%s: Failed to parse encapsultated packet %s\n",
                       __FUNCTION__, fap->orig_packet);
                display_fap_pkt("third party parse failed", fap);
                return;
        }

        if (encap_fap->error_code != NULL) {
                display_fap_error("Third Party", state, encap_fap);
                fap_free_wrapper("Third party", state, encap_fap);
                return;
        }

        display_fap_pkt("third party parse ok", encap_fap);

        state->stats.encapCount++;

        /* Check for a third party packet that's a message */
        if(encap_fap->message) {
                display_fap_message(state, encap_fap);   /* DEBUG only - look at message packets */
                if(isnewmessage(state, encap_fap)) {
                        save_message(state, encap_fap);
                        webdisplay_message(state, encap_fap);
                }
        } else {

                if(encap_fap->src_callsign == NULL) {
                        asprintf(&encap_fap->src_callsign, "NULL");
                }

                dst_callsign = encap_fap->destination;
                if(encap_fap->destination == NULL) {
                        if(encap_fap->dst_callsign != NULL) {
                                dst_callsign = encap_fap->dst_callsign;
                        } else if( (nextchar=strchr(encap_fap->orig_packet, '>')) != NULL) {
                                if(parse_callsign(nextchar+1, tmpbuf) > 0) {
                                        dst_callsign = tmpbuf;
                                }
                        }  else {
                                asprintf(&dst_callsign, "NULL");
                        }
                }

                if(encap_fap->orig_packet == NULL) {
                        asprintf(&encap_fap->orig_packet, "NULL");
                }

#if 0
                /* This segfaults */
                store_packet(state, encap_fap);
#endif

                snprintf(buf, sizeof(buf), "%s->%s @%s: type: %d %s",
                         encap_fap->src_callsign,
                         dst_callsign,
                         format_msgtime(encap_fap->timestamp),
                         (encap_fap->type == NULL) ? -1 : *encap_fap->type,
                         encap_fap->orig_packet);


                printf("Processed thirdparty: %s\n", buf);fflush(stdout);

                _ui_send(state, "DB_PACKET", buf);
                fap_free_wrapper("Third Party", state, encap_fap);
        }
}

void webdisplay_unhandled(struct state *state, fap_packet_t *fap)
{
        char buf[512];

        printf("%s: Call to: %s, Msg to: %s, pkt: %s\n",
               __FUNCTION__, fap->dst_callsign, fap->destination, fap->orig_packet);

        snprintf(buf, sizeof(buf), "%s->%s@%s: type: %02x %s",
                 fap->src_callsign,
                 fap->destination,
                 format_msgtime(fap->timestamp),
                 *fap->type,
                 fap->orig_packet);
        _ui_send(state, "DB_PACKET", buf);
}

/*
 * Respond to an ack message request
 */
void send_msg_ack(struct state *state, fap_packet_t *fap, char * message_id)
{
        char aprs_ack[24]; /* largest APRS Message Ack is 19 bytes */

        sprintf(aprs_ack,":%-9s:ack%s",
                  fap->src_callsign,
                  message_id);

        pr_debug("Sending aprs ack: %s\n", aprs_ack);
        send_beacon(state, aprs_ack);
}

/*
 * Send APRS message packet out one of the interfaces:
 * serial, ax.25 or network stack
 */
void send_message(struct state *state, char *to_str, char *msg_str, char **build_msg)
{
        char *aprs_msg, *tokstr;
        int ack_ind;
        ack_outstand_t *ackout;

        /* disallow these 3 characters |,~ or { */
        if((tokstr=strstr(msg_str,"|")) != NULL ) {
                *tokstr=' ';
        }
        if((tokstr=strstr(msg_str,"~")) != NULL ) {
                *tokstr=' ';
        }
        if((tokstr=strstr(msg_str,"{")) != NULL) {
                *tokstr=' ';
        }

        if(state->conf.aprs_message_ack) {
                state->ack_msgid++; /* Bump APRS message number */
                /* Get index of outstanding messages */
                ack_ind = state->ack_msgid & (MAX_OUTSTANDING_MSGS - 1);

                /* initialize message ack state */
                state->ackout[ack_ind].ack_msgid = state->ack_msgid;
                state->ackout[ack_ind].needs_ack = true;


                /* create an APRS message with a ack request
                 * reference page 71 APRS protocol 1.0.1 */

                asprintf(&aprs_msg,":%-9s:%s{%02d",
                         to_str,
                         msg_str,
                         state->ack_msgid);

                /* save pointer to message in case a retry is required */
                ackout = &state->ackout[ack_ind];
                ackout->aprs_msg = aprs_msg;
                ackout->timestamp = time(NULL);
                pending_ack_timer(ackout, 0);
                state->outstanding_ack_timer_count++;
                if(state->outstanding_ack_timer_count > MAX_OUTSTANDING_MSGS) {
                        printf("Exceeded outstanding unacknowledged message limit\n");
                }
                printf("%s: Set pending_ack_timer for msg id %d, interval = %d seconds, fd = 0x%02x\n",
                       time2str(NULL, 0), ack_ind,
                       ACK_INTERVAL*retry_interval_mult[0],
                       state->ackout[ack_ind].timer_fd
                      );
        } else {
                asprintf(&aprs_msg,":%-9s:%s",
                         to_str,
                         msg_str);
        }
        printf("SENDING first aprs msg: %s\n", aprs_msg);
        send_beacon(state, aprs_msg);
        *build_msg = aprs_msg;
}

/*
 * Debug only - look at message packets
 */
void display_fap_message(struct state *state, fap_packet_t *fap)
{
        state->stats.inMsgCount++;

        printf("MESSAGE[%lu]: ", state->stats.inMsgCount);
        if(fap->messaging) {
                printf("msg capab: %s ", (*fap->messaging == 1) ? "yes" : "no");
        }
        if(fap->destination) {
                printf("dest: %s ", fap->destination);
        }
        if(fap->message_ack) {
                printf("ack: %s ", fap->message_ack);
        }
        if(fap->message_nack) {
                printf("nack: %s ", fap->message_nack);
        }
        if(fap->message_id) {
                printf("id: %s", fap->message_id);
        }
        if(fap->message) {
                printf("msg[%zu]: %s ", strlen(fap->message), fap->message);
        }
        printf("\n");
        fflush(stdout);
}

#endif /* WEBAPP */
