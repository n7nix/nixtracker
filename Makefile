SHELL:=/bin/bash

# Set DEBUG_ON to YES or NO
#  DEBUG_ON=YES enables pr_debug macro
DEBUG_ON=YES
GTKAPP_ENABLE=NO
WEBAPP_ENABLE=YES
GPSD_ENABLE=YES

DEST="root@beagle:carputer"

# The icon size is used by:
#  - convert in the images Makefile,
#  - ui.c as APRS_IMG_MULT
#  - tracker-frontend.js as APRS_IMG_MULT
APRS_IMG_MULT=7
APRS_ICON_SIZE=${APRS_IMG_MULT}00%
# Make available to Makefile in images directory
export APRS_ICON_SIZE

CFLAGS = -Wall -Werror
CFLAGS_NOWEB = -Wall -Werror

TARGETS = aprs fakegps aprs-is faptest aprs-spy

ifeq ($(GTKAPP_ENABLE), YES)
GTK_CFLAGS = `pkg-config --cflags 'gtk+-2.0'`
GTK_LIBS = `pkg-config --libs 'gtk+-2.0'`
TARGETS += ui uiclient
endif

LIB_GPS =
ifeq ($(GPSD_ENABLE), YES)
LIB_GPS = -lgps
CFLAGS+= -DHAVE_GPSD_LIB
endif

LIBAX25=$(shell ./conftest.sh)

# If not building on a dev machine get build number from header file
ifeq (,$(wildcard .git))
	BUILD:="$(shell grep "TRACKER_BUILD" aprs.h | cut -d' ' -f3)"
else
	BUILD:="$(shell LC_ALL=C git log --oneline | wc -l)"
endif

CFLAGS+=-DBUILD=$(BUILD)
CFLAGS_NOWEB+=-DBUILD=$(BUILD)

ifeq ($(LIBAX25), -lax25)
TARGETS+= aprs-ax25
CFLAGS+= -DHAVE_AX25_TRUE
LIBS+=$(LIBAX25)

CFLAGS_NOWEB+= -DHAVE_AX25_TRUE
LIBS_NOWEB+=$(LIBAX25)
else
$(error "AX.25 not installed!")
endif

# -g option compiles with symbol table
ifeq ($(DEBUG_ON), YES)
CFLAGS+=-DDEBUG -g
CFLAGS_NOWEB+=-DDEBUG -g
endif

# Flags & libs for apps that don't use webapp
CFLAGS_NOWEB+= -DCONSOLE_SPY

ifeq ($(WEBAPP_ENABLE), YES)
CFLAGS+=-DWEBAPP
LIBS+=-lfap -liniparser
LIBS+=-ljson-c
endif


.PHONY :  images clean sync install

all : $(TARGETS) images

aprs.o: aprs.c uiclient.c serial.c nmea.c aprs-is.c aprs-ax25.c aprs-msg.c conf.c util.o util.h aprs.h Makefile ax25dump.c ipdump.c
uiclient.o: uiclient.c ui.h util.c util.h Makefile
aprs-is.o: aprs-is.c conf.c util.c aprs-is.h util.h aprs.h Makefile
aprs-ax25.o: aprs-ax25.c aprs-ax25.h aprs.h conf.c util.c aprs-is.h util.h Makefile ax25dump.c ipdump.c
aprs-spy.o: aprs-ax25.c aprs-ax25.h aprs.h util.c aprs-is.h util.h Makefile ax25dump.c ipdump.c Makefile
serial.o: serial.c serial.h
nmea.o: nmea.c nmea.h
util.o: util.c util.h
conf.o: conf.c util.c util.h aprs.h aprs-is.h
ax25dump.o: ax25dump.c crc.c util.c monixd.h util.h
crc.o: crc.c
faptest.o: faptest.c


aprs: aprs.o uiclient.o nmea.o aprs-is.o serial.o aprs-ax25.o aprs-msg.o conf.o util.o ax25dump.o ipdump.o crc.o
#	@echo "libs: $(LIBS), cflags: $(CFLAGS), libax25: $(LIBAX25), build: $(BUILD)"
	@if [ "$(LIBAX25)" = "" ]; then echo; echo "AX.25 stack not installed";echo; fi
	$(CC) $(CFLAGS) -o $@ $^ -lm $(LIBS) $(LIB_GPS)

ui: ui.c uiclient.o aprs.h ui.h util.c util.h
	$(CC) $(CFLAGS) -DAPRS_IMG_MULT=${APRS_IMG_MULT} $(GTK_CFLAGS) $(GLIB_CFLAGS) $^ -o $@ $(GTK_LIBS) $(GLIB_LIBS) $(LIBS)

uiclient: uiclient.c ui.h util.c util.h
	$(CC) $(CFLAGS) -DMAIN $< -o $@ util.o -lm $(LIBS)

aprs-is: aprs-is.c conf.o util.o util.h aprs-is.h aprs.h
	$(CC) $(CFLAGS) -DMAIN  $< -o $@ conf.o util.o -lm $(LIBS)

aprs-ax25: aprs-ax25.c conf.o util.o util.h aprs-is.h aprs-ax25.h aprs.h ax25dump.o ipdump.o crc.o
	$(CC) $(CFLAGS) -DMAIN  $< -o $@ conf.o util.o ax25dump.o ipdump.o crc.o -lm $(LIBS)

aprs-spy: aprs-ax25.c util.o util.h aprs-is.h aprs-ax25.h aprs.h ax25dump.o ipdump.o crc.o  Makefile
	$(CC) $(CFLAGS_NOWEB) -DMAIN $< -o $@ util.o ax25dump.o ipdump.o crc.o -lm $(LIBS_NOWEB)

fakegps: fakegps.c
	$(CC) $(CFLAGS) -o $@ $< -lm

faptest: faptest.c
	$(CC) $(CFLAGS) -o $@ $< -lm $(LIBS)

images:
	$(MAKE) -C images

clean:
	rm -f $(TARGETS) aprs-spy *.o *~ ./images/*_big.png

sync:
	scp -r *.c *.h Makefile tools images $(DEST)

buildnum:
	@echo $(BUILD)

# copy node.js files to /usr/share/dantracker/
# use rsync to create possible missing directories
install:
	rsync -a examples/aprs_spy.ini /etc/tracker
	cp examples/aprs_spy.ini /etc/tracker
	cp scripts/tracker* /etc/tracker
	cp scripts/.screenrc.trk /etc/tracker
	cp scripts/.screenrc.spy /etc/tracker
	rsync -a --cvs-exclude --include "*.js" --include "*.html" --exclude "*" webapp/ /usr/share/dantracker/
	cp aprs /usr/local/bin
	cp aprs-ax25 /usr/local/bin
