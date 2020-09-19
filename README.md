# nixtracker

[![GitPitch](https://gitpitch.com/assets/badge.svg)](https://gitpitch.com/n7nix/dantracker/master)

* This code is a fork of [Dan Smith's dantracker](https://github.com/kk7ds/dantracker)

### Introduction

* [Script Installation Instructions](https://github.com/nwdigitalradio/n7nix/blob/master/tracker/README.md)
* For reference only: [Manual Installation Instructions](https://github.com/n7nix/dantracker/blob/master/INSTALL.md)

* Differences between this code (n7nix) and Dan Smith's KK7DS code are:
  * Display runs in a browser using node.js & websockets
    * kk7ds version displays using GTK+
  * Adds APRS messaging
  * Adds Browser interface to paclink-unix for Winlink messaging.
  * Adds shutdown reset control from the app.
* The server part can run on an RPi (kk7ds version can as well)
  * The client part can run on any browser on the network

### Screen shot of current nixtracker operating in a browser

![nixtracker](/images/nixtracker-1366x768.png)

### Photo of current physical setup in back of truck

![nixtracker](/images/img_2638_resize.jpg)


### How to run

* After installing to your Linux machine (Raspberry Pi, Beagle Bone or Workstation)
  * Open a browser in any computer/phone/pad/tv with this url:
```
# local to server
localhost:8081/tracker.html
# On a remote computer
your_RPi_IP_Address:8081/tracker.html
# example
10.0.42.131:8081/tracker.html
```

### Config
* Config file lives here:
  *  /etc/tracker/aprs_tracker.ini
* Verify [station] mycall
* When running with no GPS or GPS cannot lock sufficient sats set [gps] type=static
* When running in vehicle set [gps] type=serial

### Dan Smith's blog
* [A custom APRS tracker with a real screen](http://www.danplanet.com/blog/2011/03/20/a-custom-aprs-tracker-with-a-real-screen/) March 20, 2011

### kk7ds dantracker Links
* [Dan Smith KK7DS, My APRS Tracker Project](https://www.youtube.com/watch?v=JOaTdWAwdUQ&t=8s) March 20, 2011
* [Eric Schott PA0ESH, kk7ds tracker on an RPi](https://www.youtube.com/watch?v=HMsYk5gaoNs) April 10, 2013
* [VR2XKP](https://www.youtube.com/watch?v=tKTR8vCEDxg) October 8, 2016

### n7nix dantracker Links
* [Eric Schott PA0ESH, n7nix tracker on Ubuntu workstation](https://www.youtube.com/watch?v=isUSpFrZ504) April 21, 2013
* [Eric Schott PA0ESH n7nix tracker on Ubuntu workstation](https://www.youtube.com/watch?v=Pg-buSHbZVc) June 17, 2013
* [Dantracker on a Raspberry PI (Last updated 03-09-2017)](https://www.pa0esh.com/?page_id=59)
