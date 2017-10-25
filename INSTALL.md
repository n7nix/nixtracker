# dantracker installation instructions

* Get the install script and run it like this:

```
git clone https://github.com/nwdigitalradio/n7nix
cd n7nix/tracker
./tracker_install.sh
```
### Verify Install

```
# as root
cd /home/pi/bin
./iptable-check.sh
```
* Should see something like this
```
Chain INPUT (policy ACCEPT 200 packets, 35215 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain FORWARD (policy ACCEPT 0 packets, 0 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain OUTPUT (policy ACCEPT 165 packets, 35965 bytes)
    pkts      bytes target     prot opt in     out     source               destination
       0        0 DROP       2    --  *      ax0     0.0.0.0/0            224.0.0.22
       0        0 DROP       udp  --  *      ax0     0.0.0.0/0            224.0.0.251          udp dpt:5353
```
* Check if screen is running with 3 windows
```
# as root
screen -ls
```
result should be something like this:
```
There is a screen on:
	10096.Tracker	(09/05/2017 12:37:11 PM)	(Detached)
1 Socket in /var/run/screen/S-root.
```
* Verify 3 windows
```
screen -x 10096.Tracker
```
* Once screen is invoked you can check each window like this:
```
ctrl a w
ctrl a 0
ctrl a 1
ctrl a 2
```

* Now reboot & confirm that everything loads ok
  * run all _Verify Install_ commands

### Verify Browser App
* On your RPi appliance open a browser and type this for a URL:
```
localhost:8081/tracker.html
```
* If your RPi appliance is plugged into your home network
  * Using your computer [Linux/Windows/MAC] start your browser with this URL
```
# On your RPi get your Ethernet address
ifconfig eth0

# On your workstation or computer type this in your browser
your_RPi_IP_Address:8081/tracker.html
# example
10.0.42.131:8081/tracker.html

# Try it on anybrowser on your phone/pad/tv

```


###### Everything following is for your information only.
###### Just run the above script

----

### Pre-installation requirements

#### Dependencies:

##### libfap:
```
  http://pakettiradio.net/libfap/
  wget http://pakettiradio.net/downloads/libfap/1.5/libfap-1.5.tar.gz
  tar -zxvf libfap-1.5.tar.gz
  cd libfap-1.5
# apply fap patch
  patch -p2 < ./<path_to_tracker_src>/fap_patch.n7nix
  sudo cp src/fap.h /usr/local/include/
  ./configure
  make
  sudo make install
```
* NOTE: libraries install to /usr/local/lib

##### libiniparser:
```
  http://ndevilla.free.fr/iniparser/
  wget http://ndevilla.free.fr/iniparser/iniparser-3.1.tar.gz
  tar -zxvf iniparser-3.1.tar.gz

  cd iniparser
  sudo cp src/iniparser.h  /usr/local/include
  sudo cp src/dictionary.h /usr/local/include
  make
  sudo cp libiniparser.* /usr/local/lib
```

##### gpsd: __Optional for GPS daemon__
*  Build controlled by GPSD_ENABLE variable in Makefile
```
  http://www.catb.org/gpsd/installation.html
  Installed from package.
   - require: gpsd, libgps-dev
  Verify library libgps is installed: ldconfig -p | grep gps
  Test installation with something like this:
  gpsd -D 5 -N -n /dev/ttyUSB0
```

##### GTKAPP: Only used by gtk app, see Makefile
```
  gtk+-2.0
  libgtk2.0-dev
```

##### AX25: Only used with AX25 stack
* Build from source http://www.linux-ax25.org/wiki/CVS
```
  libax25-dev
```

##### WEBAPP: Only used by web app, see Makefile
###### node.js
* requires openssl libssl-dev
* link:  https://nodejs.org/en/download
  * Choose either LTS or Current
  * Raspberry Pi choose Linux Binaries (ARM) ARMv7

* Will install verify by running:

```
node -v
npm -v
```
######  json-c
* Location:    https://github.com/json-c/json-c
* Repository:  git clone git://github.com/json-c/json-c.git
```
    cd json-c
    sh autogen.sh
    ./configure
    make
    sudo make install
    ldconfig
# if /usr/local/include/json does not exist
 ln -s /usr/local/include/json-c /usr/local/include/json
```
* javascript libraries
  * Needs version 1.8.3 or greater
```
    wget http://bit.ly/jqsource -O jquery.js
    sudo cp jquery.js /usr/share/dantracker
```
* Above taken care of by install script.

##### Required node modules
```
ctype
iniparser
websocket
connect
serve-static
finalhandler
```
##### Required Build Tools:
```
build-essential (includes libc-dev, make, gcc, ... etc)
pkg-config
imagemagick
git
automake
autoconf
libtool
```

###  Basic Installation

* Change, _cd_, to the directory containing the source code
  * then type _make_ to compile, link & create png image files.

#### fap Installation

Before building the fap library run the fap patch in the tracker
repository against its source tree. You need to supply the directory
path to dantracker source.

```
cd libfap-1.5
patch -p2 < ./<path_to_tracker_src>/fap_patch.n7nix
```

### For dantracker n7nix ONLY

#### Web app install notes

* Once core apps, aprs for tracker & aprs-ax25 for spy are built need to
do the following:

* Check that the _screen_ package is installed:
* Install node.js & npm (package manager for node)
* Install node.js modules globally:
```
npm -g install <module name>
npm list -g

    ctype
    iniparser
    websocket
    connect
```

* As root run install.sh found in tracker dir.
  * This will copy binary, javascript & config files to appropriate directories.

* Confirm javascript library jquery-1.8.3.min.js exists here:
```
/usr/share/dantracker/
```

##### Edit config files

* /etc/tracker/aprs_tracker.ini
  * Optional: /etc/tracker/aprs_spy.ini
  * change [station] mycall

* If you don't have a gps connected change [gps] type from serial or gpsd to static
  * If gps is connected set serial port and set type to serial

* For AX.25 operation change config for [ax25] port = xxx
This should be the same port name used in /etc/ax25/axports

* As root run /etc/tracker/tracker-up

* As root run screen -ls

* should see something similar to:
```
There are screens on:
	30680.Tracker	(04/15/2013 06:10:32 PM)	(Detached)
	21348.Spy	(04/15/2013 01:00:42 PM)	(Detached)
2 Sockets in /var/run/screen/S-root.
```

##### Attach to a screen terminal
```
screen -r Spy

# or

screen -r Tracker
```

* If you are successful in getting these screen sessions running & you
are attached use the following _screen_ commands:

* list windows:
```
ctrl a w
```
* change windows
```
ctrl a 0
ctrl a 1
ctrl a 2
```
* leave session by detaching:
```
ctrl a d
```
* kill session:
```
ctrl a \
```
* Open up your browser
  * Is your browser web socket enabled?
    * Most likely as web sockets is no longer that new but check by following this link:
```
http://caniuse.com/websockets
```
##### web url for tracker
```
<your_machine_name>:8081/tracker.html
# or
localhost:8081/tracker.html
```
##### web url for spy
```
<your_machine_name>:8081/spy.html
# or
localhost:8081/spy.html
```
###### Below is deprecated

----

The install script does not setup apps to start on a power up or
reboot.  To have the spy & tracker apps startup at boot time do the
following. As root copy tracker startup script to the init.d dir then
use udpate-rc.d to add the daemon.
```
su
cd /etc/tracker
cp tracker /etc/init.d/
```
* Add a daemon with sequence of 95
```
update-rc.d tracker defaults 95
```
The daemon script supports start, stop, restart & status
For example as root:
```
/etc/init.d/tracker status
```
----

### Starting tracker or spy from command line without using script

* Make sure tracker or spy is not already running
```
screen -ls

screen -S Tracker -c /etc/tracker/.screenrc.trk

# or

screen -S Spy -c /etc/tracker/.screenrc.spy
```

## Console Programs


* Stand alone programs displaying to console only.

##### ./aprs-is

```
./aprs-is -c <path_to_ini_config_file>/aprs.ini
```
###### Display packets from an APRS server.
* Need to specify in ini file:
```
[net] server_host_address, server_port and range
[static] lat and long
[gps] type=static
```

##### ./aprs-ax25
```
./aprs-ax25 -c <path_to_ini_config_file>/aprs.ini
```
###### Display packets from AX.25 stack
* Need to edit compile time define aprs-ax25.c and remove **#define
PKT_SOCK_ENABLE**


##### ./fakegps

* Continuously display gps GGA & RMC sentences that will be used if
[gps]:type=static is configured.


##### ./faptest < file_containing_aprs_packet

* Reads a packet from stdin and passes to fap library.



## For dantracker kk7ds ONLY

### Configuration

Copy one of the ini files from examples directory to same directory
as aprs executable and rename to aprs.ini


### Running dantracker kk7dds edition

* Stress-test by using examples/aprstest.ini (modified as appropriate), which
will attempt to eat the entire APRS-IS world feed without crashing.


### Test Programs

* Test stand alone programs displaying to console.
```
./aprs-is
./aprs-ax25
```

* Test display interface.

Using socket connect with AF_INET
./ui -i with ./uiclient -i [NAME] [VALUE]

Using socket connect wit AF_UNIX
./ui -w with ./uiclient -w [NAME] [VALUE]


### Normal operation

* UI socket connect via AF_INET
```
./ui -i with ./aprs -d 127.0.0.1
```

* UI socket connect via AF_UNIX
```
./ui -w with ./aprs
```
