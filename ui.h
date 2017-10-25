/* Copyright 2012 Dan Smith <dsmith@danplanet.com> */

#ifndef __UI_H
#define __UI_H

#include <stdint.h>
#include <sys/socket.h>

#define MAXMSG 2048

#define UI_MSG_NAME_STATION_INFO "STATIONINFO"
#define UI_MSG_NAME_BEACON "BEACONNOW"
#define UI_MSG_NAME_KISS "INITKISS"
#define UI_MSG_NAME_SEND "APRSMSG"
#define UI_MSG_NAME_SETCFG "APRSSETCFG"
#define UI_MSG_NAME_GETCFG "APRSGETCFG"
#define UI_MSG_NAME_SYSCTRL "SYSCTRL"

#define UI_MSG_VALUE_SYSCTRL_SHUTDOWN "shutoff"
#define UI_MSG_VALUE_SYSCTRL_RESET "reset"

enum {
	MSG_SETVALUE,
	MSG_SEND,
	MSG_MAX
};

struct ui_msg {
	uint16_t type;
	uint16_t length;
	union {
		struct {
			uint16_t name_len;
			uint16_t valu_len;
		} name_value;
	};
};

int ui_connect(struct state *state);
int ui_send(int sock, const char *name, const char *value);
int ui_send_to(struct sockaddr *dest, unsigned int dest_len,
	       const char *name, const char *value);
int ui_get_msg(struct state *state, struct ui_msg **msg);
char *ui_get_msg_name(struct ui_msg *msg);
char *ui_get_msg_valu(struct ui_msg *msg);

#endif
