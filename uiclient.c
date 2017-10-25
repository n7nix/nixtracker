/* -*- Mode: C; tab-width: 8;  indent-tabs-mode: nil; c-basic-offset: 8; c-brace-offset: -8; c-argdecl-indent: 8 -*- */
/* Copyright 2012 Dan Smith <dsmith@danplanet.com> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <json/json.h>

#include "aprs.h"
#include "ui.h"
#include "util.h"
#include "aprs-msg.h"


int ui_sock_cfg(struct state *state)
{
        struct sockaddr *dest;
        int sock;
        struct sockaddr_un sun;

        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, state->conf.ui_sock_path);

        dest=(struct sockaddr *)&sun;
        sock = socket(dest->sa_family, SOCK_STREAM, 0);
        if (sock < 0) {
                perror("socket");
                return -errno;
        }

        if (connect(sock, dest, sizeof(sun))) {
                fprintf(stderr, "%s: connect error on ui socket %d, path: %s, error: %s\n",
                          __FUNCTION__, sock,  state->conf.ui_sock_path, strerror(errno));

                return -errno;
        }

        return sock;
}

void ui_unix_sock_wait(struct state *state)
{
        int retcode;
        struct stat sts;

        /* get rid of the Unix socket */
        unlink(state->conf.ui_sock_path);
        /* verify Unix socket removal was a success */
        retcode = stat(state->conf.ui_sock_path, &sts);
        if(retcode != -1 || errno != ENOENT) {
                printf("Problem deleting Unix socket %s, stat returned an errno of 0x%02x %s\n",
                       state->conf.ui_sock_path, errno, strerror(errno));
        }

        /* Wait for the socket to be created by the node script */
        printf("Waiting for node script to create a Unix socket\n");
        while (stat(state->conf.ui_sock_path, &sts) == -1 && errno == ENOENT) {
                sleep(1);
        }
        sleep(2);
}

int ui_connect(struct state *state)
{
        int sock;
        char buf[32];
        struct sockaddr *dest =  &state->conf.display_to.afinet;
        unsigned int dest_len = sizeof(struct sockaddr);

        if(state->conf.display_to.afinet.sa_family == AF_UNIX) {
                /* clean-up previous socket */
                ui_unix_sock_wait(state);
                dest_len = sizeof(struct sockaddr_un);
        }

        sock = socket(dest->sa_family, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -errno;
	}

	if (connect(sock, dest, dest_len)) {
                if(state->debug.verbose_level > 3) {
                        fprintf(stderr, "%s: Failed to connect to UI socket %d: %s\n",
                                  __FUNCTION__, sock, strerror(errno));
                }
		close(sock);
		return -errno;
        } else {
                sprintf(buf, "%d", state->conf.ui_inet_port);
                printf("UI Socket %s, connected with sock %d\n",
                       state->conf.display_to.afinet.sa_family == AF_UNIX ?
                       state->conf.ui_sock_path : buf,
                       sock);
        }

	return sock;
}

json_object *build_browser_msg(const char *name, const char *value)
{

        json_object *aprs_object;

        /* Create a json object */
        aprs_object = json_object_new_object();

        json_object_object_add(aprs_object, "aprs", json_object_new_string(name));
        json_object_object_add(aprs_object, "data", json_object_new_string(value));

#if 0
        printf("Json object created %s\n", json_object_to_json_string(aprs_object));
#endif

        return (aprs_object);
}

struct ui_msg *build_lcd_msg(uint16_t msg_type, const char *name, const char *value)
{
	struct ui_msg *msg;
	int len;
	int offset;

	len = sizeof(*msg) + strlen(name) + strlen(value) + 2;
	msg = malloc(len);
	if(msg == NULL) {
		printf("%s: malloc error: %s\n",
		       __FUNCTION__, strerror(errno));
		return msg;
	}

	msg->type = msg_type;
	msg->length = len;
	msg->name_value.name_len = strlen(name) + 1;
	msg->name_value.valu_len = strlen(value) + 1;

	offset = sizeof(*msg);
	memcpy((char*)msg + offset, name, msg->name_value.name_len);

	offset += msg->name_value.name_len;
	memcpy((char*)msg + offset, value, msg->name_value.valu_len);

#ifdef DEBUG_VERBOSE_1
	printf("%s: type: %d, len: %d, name_len: %d, value_len: %d, str_name: %s, str_value: %s\n",
	       __FUNCTION__,
	       msg->type,
	       msg->length,
	       msg->name_value.name_len,
	       msg->name_value.valu_len,
	       name,
	       value
	       //	       (char*)msg + sizeof(struct ui_msg)
	      );
#endif /* DEBUG */

	return msg;
}

int ui_send(int sock, const char *name, const char *value)
{
        int ret;

#ifdef WEBAPP
        json_object *json_msg;
        char *str_msg;

        json_msg = build_browser_msg(name, value);
        str_msg = (char *)json_object_to_json_string(json_msg);
        /* send a message on a socket */
        ret = send(sock, str_msg, strlen(str_msg), MSG_NOSIGNAL);

        /* decrement the reference count of json object, & free if 0 */
        json_object_put(json_msg);

#else
        struct ui_msg *msg;

        msg = build_lcd_msg(MSG_SETVALUE, name, value);

	if(msg == NULL) {
                return -ENOMEM;
	}
        /* send a message on a socket */
	ret = send(sock, msg, msg->length, MSG_NOSIGNAL);

        free(msg);

#endif

	return ret;
}


#ifdef WEBAPP
#define RBUFSIZE 1024

int ui_get_json_msg(struct state *state, struct ui_msg **msg, struct ui_msg *hdr)
{
	char *buf = (char *)hdr;
	char json_str[RBUFSIZE];
        char aprs_msg[128]; /* largest legal APRS Message is 84 bytes */
        char *aprs_msg_ptr;
	int json_len;
        int ret;
        int sock = state->dspfd;
        bool found;

	memset(json_str, 0, RBUFSIZE);

	strncat(json_str, buf, sizeof(struct ui_msg));
	json_len = strlen(json_str);

	ret = read(sock, &json_str[json_len], RBUFSIZE-json_len);

	if(ret > 0) {
		json_object *new_obj, *data_obj, *to_obj, *msg_obj, *type_obj;
		char *to_str,  *msg_str, *type_str;

		if(ret == RBUFSIZE-json_len) {
			printf("%s: Warning sock read size suspiciously large\n",
			       __FUNCTION__);
		}

		new_obj = json_tokener_parse(json_str);
		printf("json str: %s\n", json_str);
		printf("new_obj.to_string()=%s\n", json_object_to_json_string(new_obj));
                found = json_object_object_get_ex(new_obj, "type", &type_obj);
                if(!found) {
                        fprintf(stderr, "%s: Failed to fetch 'type' object\n",
                                  __FUNCTION__);
                }

                found = json_object_object_get_ex(new_obj, "data", &data_obj);
                if(!found) {
                        fprintf(stderr, "%s: Failed to fetch 'data' object\n",
                                  __FUNCTION__);
                }

                type_str = (char *)json_object_get_string(type_obj);
#ifdef DEBUG_VERBOSE
                printf("Got message type: %s\n", type_str);
#endif /*  DEBUG_VERBOSE */

		if(STREQ(type_str, "message")) {
#if 0 /* reference, currently not used */
			json_object *from_obj;
                        found = json_object_object_get_ex(data_obj, "from", &from_obj);
                        if(!found) {
                                fprintf(stderr, "%s: Failed to fetch 'from' object\n",
                                          __FUNCTION__);
                        }
#endif /* reference */

                        found = json_object_object_get_ex(data_obj, "sendto", &to_obj);
                        if(!found) {
                                fprintf(stderr, "%s: Failed to fetch 'sendto' object\n",
                                          __FUNCTION__);
                        }

                        found = json_object_object_get_ex(data_obj, "text", &msg_obj);
                        if(!found) {
                                fprintf(stderr, "%s: Failed to fetch 'text' object\n",
                                          __FUNCTION__);
                        }
#ifdef DEBUG_VERBOSE
			printf("json to: %s, msg: %s\n",
			       json_object_get_string(to_obj),
			       json_object_get_string(msg_obj));
#endif /*  DEBUG_VERBOSE */
			to_str = (char *)json_object_get_string(to_obj);
			/* qualify APRS addressee string */
			strupper(to_str); /* APRS requires upper case call signs */
			if(strlen(to_str) > MAX_CALLSIGN ) {
				*(to_str + MAX_CALLSIGN)='\0';
			}
			msg_str = (char *)json_object_get_string(msg_obj);

			/* qualify APRS message text string */
			if(strlen(msg_str) > MAX_APRS_MSG_LEN) {
				*(msg_str + MAX_APRS_MSG_LEN)='\0';
                        }

                        send_message(state, to_str, msg_str, &aprs_msg_ptr);

                        *msg = build_lcd_msg(MSG_SEND, UI_MSG_NAME_SEND, aprs_msg_ptr);
                        if(!state->conf.aprs_message_ack) {
                                free(aprs_msg_ptr);
                        }

		} else if(STREQ(type_str, "setconfig")) {

			char *data_str;

			data_str = (char *)json_object_get_string(data_obj);
                        state->conf.aprs_message_ack = STREQ(data_str, "ack on");

			printf("DEBUG: ACK is turned %s\n", state->conf.aprs_message_ack ? "ON" : "OFF");

			*msg = build_lcd_msg(MSG_SEND, UI_MSG_NAME_SETCFG, aprs_msg);

                } else if(STREQ(type_str, "getconfig")) {

                        char *data_str;

                        data_str = (char *)json_object_get_string(data_obj);

                        printf("DEBUG: getconfig request for %s, verified: %s\n",
                               data_str, STREQ(data_str, "source_callsign") ? "yes" : "no");
                        strcpy(aprs_msg, data_str);
                        *msg = build_lcd_msg(MSG_SEND, UI_MSG_NAME_GETCFG, aprs_msg);

                } else if(STREQ(type_str, "sysctrl")) {

                        char *data_str;

                        data_str = (char *)json_object_get_string(data_obj);

                        printf("DEBUG: sysctrl request for %s\n",
                               data_str);
                        strcpy(aprs_msg, data_str);
                        *msg = build_lcd_msg(MSG_SEND, UI_MSG_NAME_SYSCTRL, aprs_msg);

                } else {
			printf("Unhandled message type: %s\n", type_str);
		}

	} else {
		printf("%s: socket read error: %s\n",
		       __FUNCTION__, strerror(errno));
	}

	return ret;
}
#endif /* WEBAPP */

/*
 * Read socket sourced from displays of either web app or lcd hardware
 *
 * lcd hardware sends struct ui_msg
 * web apps sends json which gets converted to struct ui_msg
 *
 */
int ui_get_msg(struct state *state, struct ui_msg **msg)
{
	struct ui_msg hdr;
        int ret;
        int sock = state->dspfd;

	ret = read(sock, &hdr, sizeof(hdr));
	if (ret <= 0)
		return ret;

#ifdef WEBAPP
        /*
         * Retain compatibility with previous code
         * Read the rest of the message as JSON
         */
        {
                char *buf = (char *)&hdr;
                int i;

                for(i=0; i < 7; i++) {
                        printf("%02x ", buf[i]);
                }
                printf("\n");

                if(STRNEQ(buf, "{\"type\":", sizeof(hdr))) {
                        ret = ui_get_json_msg( state, msg, &hdr);
                } else {
                        *msg = malloc(hdr.length);
                        if (*msg == NULL) {
                                printf("%s: malloc error: %s\n",
                                       __FUNCTION__, strerror(errno));
                                return -ENOMEM;
                        }

                        memcpy(*msg, &hdr, sizeof(hdr));

                        ret = read(sock, ((char *)*msg)+sizeof(hdr), hdr.length - sizeof(hdr));
                }
        }
#endif /* WEBAPP */
	return 1;
}

char *ui_get_msg_name(struct ui_msg *msg)
{
	if (msg->type != MSG_SETVALUE &&
	   msg->type != MSG_SEND) {
		printf("msg type not 0x%02x but is 0x%02x\n",
		       MSG_SETVALUE, msg->type);
		return NULL;
	}

	return (char*)msg + sizeof(*msg);
}

void filter_to_ascii(char *string)
{
	int i;

	for (i = 0; string[i]; i++) {
		if ((string[i] < 0x20 || string[i] > 0x7E) &&
		    (string[i] != '\r' && string[i] != '\n'))
			string[i] = ' ';
	}
}

char *ui_get_msg_valu(struct ui_msg *msg)
{
	if (msg->type != MSG_SETVALUE &&
	   msg->type != MSG_SEND) {
		return NULL;
	}

	filter_to_ascii((char *)msg + sizeof(*msg) + msg->name_value.name_len);

	return (char*)msg + sizeof(*msg) + msg->name_value.name_len;
}

#ifdef MAIN
#include <getopt.h>
#include <sys/un.h>
#include <netdb.h>
#include <netinet/in.h>

#include "util.h"

struct opts {
	int addr_family;
	char name[64];
	char value[64];
};

int parse_ui_opts(int argc, char **argv, struct opts *opts)
{
	int retval = 1;

	static struct option lopts[] = {
		{"window",    0, 0, 'w'},
		{"inet",      0, 0, 'i'},
		{NULL,        0, 0,  0 },
	};

	memset(opts, 0, sizeof(opts));

	/* Set default options */
	opts->addr_family = AF_INET;
	strcpy(opts->name, "AI_CALLSIGN");
	strcpy(opts->value, "DEFAULT");

	while (1) {
		int c;
		int optidx;

		c = getopt_long(argc, argv, "wi", lopts, &optidx);
		if (c == -1)
			break;

		switch(c) {
			case 'w':
				opts->addr_family = AF_UNIX;
				break;
			case 'i':
				opts->addr_family = AF_INET;
		}
	}

	/*
	 * Set positional args
	 */
	/* get the NAME */
	if(optind < argc) {
		strcpy(opts->name, argv[optind]);
		strupper(opts->name);
		optind++;
	}

	/* get the VALUE */
	if(optind < argc) {
		strcpy(opts->value, argv[optind]);
		optind++;
	}

	if(argc == 1)
		retval = 0;
	else if(argc < 4)
		printf("Using defaults\n");

	return (retval);
}

int ui_send_unix(struct opts *opts, struct state *state)
{
        int sock;
        int ret;
        struct sockaddr_un sun;
        struct sockaddr *dest = (struct sockaddr *)&sun;
        unsigned int dest_len = sizeof(sun);

        const char *name = opts->name;
        const char *value = opts->value;

        ui_unix_sock_wait(state);

        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, state->conf.ui_sock_path);
        return ui_send_to((struct sockaddr *)&sun, sizeof(sun),
                          opts->name, opts->value);
        if (strlen(name) == 0)
                return -EINVAL;

        sock = ui_connect(dest, dest_len);
        if (sock < 0)
                return sock;

        ret = ui_send(sock, name, value);

        close(sock);

        return ret;
}

int ui_send_inet(struct opts *opts, struct state *state)
{
        int sock;
        int ret;
        char hostname[]="127.0.0.1";
        struct sockaddr_in sin;
        struct sockaddr *dest = (struct sockaddr *)&sin;
        unsigned int dest_len = sizeof(sin);

        struct hostent *host;
        char *name = opts->name;
        const char *value = opts->value;


        sin.sin_family = AF_INET;
        sin.sin_port = htons(state->conf.ui_inet_port);

        host = gethostbyname(hostname);
        if (!host) {
                perror(hostname);
                return -errno;
        }

        if (host->h_length < 1) {
                fprintf(stderr, "No address for %s\n", hostname);
                return -EINVAL;
        }
        memcpy(&sin.sin_addr, host->h_addr_list[0], sizeof(sin.sin_addr));


        if (strlen(name) == 0)
                return -EINVAL;

        sock = ui_connect(dest, dest_len);
        if (sock < 0)
                return sock;

        ret = ui_send(sock, name, value);

        close(sock);

        return ret;
}

int ui_send_default(struct opts *opts, struct state *state)
{

        if(opts->addr_family == AF_INET) {
                printf("using AF_INET\n");
                return (ui_send_inet(struct opts *opts, struct state *state));
	} else {
                printf("using AF_UNIX\n");
                return (ui_send_unix(struct opts *opts, struct state *state));
	}
}

int main(int argc, char **argv)
{
        int ret;
        struct opts opts;
        struct state state;

        memset(&state, 0, sizeof(state));

	if(!parse_ui_opts(argc, argv, &opts)) {
		printf("Usage: %s -<i><w> [NAME] [VALUE]\n", argv[0]);
		return 1;
        }

        if (parse_ini(state.conf.config ? state.conf.config : "aprs.ini", &state)) {
                printf("Invalid config\n");
                exit(1);
        }

	printf("%d, %s %s\n", opts.addr_family, opts.name, opts.value);

        ret = ui_send_default(&opts, &state);
        ini_cleanup(&state);

        return(ret);
}
#endif
