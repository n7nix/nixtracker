#!/bin/bash
#
# install_dantracker.sh
#
# Install script for Dantracker N7NIX branch on a Raspberry Pi
# with hostapd or dnsmasq
#
# Requires: whiptail
# Upon completion, please change these files:
# /home/pi/dantracker/aprs.ini,
# /etc/tracker/aprs_spy.ini
# /etc/tracker/aprs_tracker.ini
#
# 4th July 2013 - PA0ESH

# Color Codes
Reset='\e[0m'
Red='\e[31m'
Green='\e[30;42m' # Black/Green
Yellow='\e[33m'
YelRed='\e[31;43m' #Red/Yellow
Blue='\e[34m'
White='\e[37m'
BluW='\e[37;44m'

#
# ### Function raspiupdate
#

function raspiupdate {
echo "running Raspberry update & upgrade"
echo -e "\t \e[030;42m Updating Debian Wheezy package List... \t ${Reset}"
echo -e "\t${YelRed} This may take a while ${Reset}"
apt-get update -y > /dev/null
echo -e "\t \e[030;42m Upgrading Debian Wheezy package List... \t ${Reset}"
echo -e "\t${YelRed} This may take even longer ${Reset}"
apt-get dist-upgrade -y > /dev/null
echo -e "${Green} Upgrading Debian Wheezy completed ${Reset}"
}

#
# ### Function Ax25Install
#

function Ax25Install {
whiptail --backtitle "Dantracker N7NIX branch" --title "Ax.25 Install" \
--msgbox "This script will Install Ax.25 for the Raspberry Pi \n\
using official Ax.25 repository for Debian Wheezy \n\
to be used by Dantracker - N7NIX branch. " 9 60

echo -e "\t \e[030;42m Ax.25 is being installed, please wait... \t ${Reset}"
apt-get install -y libax25 libax25-dev ax25-apps ax25-tools > /dev/null

# download start up files

cd /etc/init.d

file="/etc/init.d/ax25"
if [ -f "$file" ]
then
	rm $file
	wget -qt3 http://www.pa0esh.nl/aprs/ax25 && chmod 755 ax25
else
	wget -qt3 http://www.pa0esh.nl/aprs/ax25 && chmod 755 ax25
fi

cd /etc/ax25
rm -rf /etc/ax25/* > /dev/null
wget -qt3 http://www.pa0esh.nl/aprs/ax25-up
wget -qt3 http://www.pa0esh.nl/aprs/ax25-down
wget -qt3 http://www.pa0esh.nl/aprs/axports
wget -qt3 http://www.pa0esh.nl/aprs/ax25d.conf && chmod 755 ax*
cd /usr/sbin/
echo -e "\t \e[030;42m Ax.25 is successfully installed \t ${Reset}"


  whiptail --backtitle "Dantracker N7NIX branch" --title "Ax2.5 Install - boot startup selection" \
--yesno " Should ax.25 start at boot time ?" 9 60
sel=$?

if [ $sel = 0 ]; then
echo -e "${Green} Ax.25 service will start at boot. ${Reset}"
update-rc.d ax25 defaults 93 5
else
echo -e "${Green} Ax.25 service will NOT start at boot. ${Reset}"
update-rc.d ax25 remove
fi

result=$(tempfile) ; chmod go-rw $result
whiptail --title "axports configuration - TNC baudspeed to Raspberry" --inputbox "Please enter your call sign -without the SSID." 10 50 2>$result
exitstatus=$?

if [ $exitstatus = 0 ]; then
sed -i.bak "s/N0CALL/$(cat $result)/g" /etc/ax25/axports
else
echo -e "${YelRed} Callsign not changed ${Reset}"fi
fi
rm $result
result=$(tempfile) ; chmod go-rw $result
whiptail --title "axports configuration - TNC baudspeed to Raspberry" --inputbox "Please give the baudspeed of the tnc !t" 10 50 2>$result
exitstatus=$?
if [ $exitstatus = 0 ]; then
sed -i "s/9600/$(cat $result)/g" /etc/ax25/axports
else
echo -e "${YelRed} Baudspeed not changed ${Reset}"
fi

echo -e "${Green} This is the new axports file ${Reset}"
echo -e "${Green} You can always modify /etc/ax25/axports manually ! ${Reset}"
cat /etc/ax25/axports
rm $result
# (End of AX25Script - thanks to k4gbb)
}

#
# ### Function AP_Install
#

function AP_Install {
dpkg -P --force-all nfs-common
echo -e "${YelRed} Due to a bug, nfs-common is forcely removed. ${Reset}"
echo -e "${Green} hostapd is now being installed. ${Reset}"
apt-get install -f -y hostapd >/dev/null
echo -e "${Green} dnsmasq is now being installed. ${Reset}"
apt-get install -f -y dnsmasq >/dev/null

# changing the various config files
# check if files have been updated already

if grep -Fq "Dantracker" /etc/dnsmasq.conf
then
whiptail --backtitle "Dantracker N7NIX branch" --title "hostapd & dnsmasq configuration" --msgbox "It seems that you have configured the file /etc/dnsmasq.conf already. \n\
Please change /etc/dnsmasq.conf manually with an editor if necessary,\n\
or remove the file and re-run the dantracker installation for AP." 10 78
else
whiptail --title "hostapd & dnsmasq configuration" --msgbox "The script will now configure hostapd and dnsmasq \n\
The ip adress for the wlan0 will be 192.168.42.1 \n\
with netmask 255.255.255.0 '\n\
/etc/default/ifplugd will be modified as well. \n\
wlan0 will be given ip adres 192.168.42.1." 10 78

# Configuration file for dnsmasq.
# updated line 131
sed -i.bak "s/# Configuration file for dnsmasq./# Configuration file for dnsmasq. - Dantracker/1" /etc/dnsmasq.conf
sed -i 's/#interface=/interface=wlan0/1' /etc/dnsmasq.conf

sed -i 's/#dhcp-range=192.168.0.50,192.168.0.150,12h/dhcp-range=wlan0,192.168.42.50,192.168.42.150,12h/1' /etc/dnsmasq.conf
sed -i 's/#listen-address=/listen-address=127.0.0.1/1' /etc/dnsmasq.conf
/etc/init.d/dnsmasq restart
ifdown wlan0 > /dev/null
ifconfig wlan0 192.168.42.1 >/dev/null
fi

sed -i.bak 's/INTERFACES="auto"/INTERFACES="eth0"/1' /etc/default/ifplugd
sed -i 's/HOTPLUG_INTERFACES="all"/HOTPLUG_INTERFACES="none"/1' /etc/default/ifplugd

if grep -Fq "Dantracker" /etc/network/interfaces
then
whiptail --title "Hostapd & dnsmasq configuration" --msgbox "It seems that you have configured the files for /etc/network/interfaces already. \n\
Please change /etc/dnsmasq.conf manually with an editor if necessary,\n\
or remove the file and re-run the dantracker installation for AP." 12 78
else
sed -i.bak "s/auto lo/#Configuration file for Dantracker \nauto lo/1" /etc/network/interfaces
sed -i 's/iface wlan0 inet manual/#iface wlan0 inet manual/1' /etc/network/interfaces
sed -i 's/iface default inet dhcp/#iface default inet dhcp/1' /etc/network/interfaces
sed -i 's/wpa-roam/#wpa-roam/1' /etc/network/interfaces
sed -i "s/allow-hotplug wlan0/allow-hotplug wlan0 \niface wlan0 inet static \naddress 192.168.42.1 \nnetmask 255.255.255.0 \n/" /etc/network/interfaces
fi
#check if /etc/hostapd/hostapd.conf exists
cd /etc/hostapd

if [ -f /etc/hostapd/hostapd.conf ]
	then
    whiptail --title "hostapd configuration" --msgbox "It seems that you have configured the config file for hostapd already. \n\
Please update the configuration manually with an editor if necessary,\n\
or remove the file /etc/hostapd/hostapd.conf and re-run the AP installation." 12 78
	else
    whiptail --title "hostapd configuration" --msgbox "The script will now configure hostapd with the following specs\n\
The name of the Wi-Fi network will be     : APRS\n\
The password of the Wi-Fi network will be : hamradioaprs\n\
The ip address of the dantracker will be  : 192.168.42.1\n\
/etc/default/ifplugd will be modified as well to avoid lossing the ip adres during boot.." 12 78
	wget -qt3 http://www.pa0esh.nl/aprs/hostapd.conf
	chmod 755 /etc/hostapd/hostapd.conf
	echo -e "${YelRed} hostapd.conf created. ${Reset}"
fi

#check if /etc/default/hostapd exists
echo -e "${Green} Checking existence of /etc/default/hostapd ${Reset}"
cd /etc/default/
if [ -f /etc/default/hostapd ]
then
	if grep -Fq "Dantracker" /etc/default/hostapd
	then
    whiptail --title "default hostapd configuration" --msgbox "It seems that you have configured the config file /etc/default/hostapd already. \n\
Please update the configuration manually with an editor if necessary,\n\
or remove the file /etc/default/hostapd and re-run the AP installation." 12 78
	fi

else
	wget -qt3 http://www.pa0esh.nl/aprs/hostapd
	chmod 755 /etc/default/hostapd
	echo -e "${YelRed} /etc/default/hostapd created. ${Reset}"
fi

cd /home/pi >/dev/null
echo -e "${Green} Starting the hostapd service manually - look for errors  -press Ctrl-C to end and continue. ${Reset}"
/usr/sbin/hostapd /etc/hostapd/hostapd.conf
echo -e "${Green} Starting the hostapd & dnsmasq services and enable autostart..... ${Reset}"
ifconfig wlan0 192.168.42.1
service hostapd start
service dnsmasq start
update-rc.d hostapd enable
update-rc.d dnsmasq enable

echo -e "${Green} Hostapd and dnsmasq now configured and running. ${Reset}"
}

#
# ### Function N7NIX_install
#

function N7NIX_install {
# installation of core of Dantracker N7NIX
echo -e "${Green} The core of dantracker will now be installed, this make take several hours.... ${Reset}"

cd /home/pi/
sudo apt-get install -y screen git python-serial libgtk2.0-dev gtk+-2.0 build-essential gcc pkg-config imagemagick automake autoconf libtool cvs curl libncurses-dev libssl-dev
git clone https://github.com/n7nix/dantracker
cd /home/pi/dantracker/
sed -i.bak 's/kernel_ax25.h/ax25.h/1' aprs-ax25.c
wget http://pakettiradio.net/downloads/libfap/1.3/libfap-1.3.tar.gz
tar xvzf libfap-1.3.tar.gz
cp libfap-1.3/src/fap.h /usr/local/include/
cd libfap-1.3
patch -p2 < /home/pi/dantracker/fap_patch.n7nix
sudo cp src/fap.h /usr/local/include/
./configure
make
make install
ldconfig
cd ..
wget http://ndevilla.free.fr/iniparser/iniparser-3.1.tar.gz
tar -xvzf iniparser-3.1.tar.gz
cd iniparser
cp src/iniparser.h /usr/local/include
cp src/dictionary.h /usr/local/include
make
cp libiniparser.* /usr/local/lib
ldconfig
cd ..
wget http://nodejs.org/dist/v0.10.12/node-v0.10.12.tar.gz
tar -xvzf node-v0.10.12.tar.gz
cd node-v0.10.12
echo -e "${Green} The next step may take approx 4 hours, do not despair.... ${Reset}"
./configure
make
make install
ldconfig
cd ..
wget https://s3.amazonaws.com/json-c_releases/releases/json-c-0.11.tar.gz
tar -xzvf json-c-0.11.tar.gz
cd json-c-0.11
./configure
make
make install
ldconfig
cd ..
wget http://code.jquery.com/jquery-1.10.1.min.js
echo -e "${Green} Setting up the nodes, please wait some more.. ${Reset}"
echo -e "${Green} The errors shown are just for information.... ${Reset}"

npm -g install ctype
npm -g install iniparser
npm -g install websocket
npm -g install connect

cd /home/pi/dantracker/
touch .revision
make
# modifying the tracker script - adding the logfile....
if grep -Fq "touch" /home/pi/dantracker/scripts/tracker-up
then
echo -e "${Green} tracker-up script already adapted for Raspberry.. ${Reset}"
else
sed -i 's/echo "Starting Tracker"/echo "Starting Tracker"\ntouch \/tmp\/aprs_tracker.log/1' /home/pi/dantracker/scripts/tracker-up
fi
# modifying the tracker.htm & spy.html files for jquery
if grep -Fq "jQuery/jquery.js" /home/pi/dantracker/webapp/tracker.html
then
sed -i 's/jQuery\/jquery.js/jquery.js/1' /home/pi/dantracker/webapp/tracker.html
fi

if grep -Fq "jQuery/jquery.js" /home/pi/dantracker/webapp/spy.html
then
sed -i 's/jQuery\/jquery.js/jquery.js/1' /home/pi/dantracker/webapp/spy.html
fi

./install.sh > /dev/null

cp *.js /usr/share/dantracker/
cp /etc/tracker/tracker /etc/init.d/
chmod 755 /etc/init.d/tracker

src="jQuery/jquery.js"

sed -i 's/ax25/$ax25/g' /etc/init.d/tracker

  whiptail --title "Dantracker N7NIX installation" \
--yesno " Should the Dantracker start at boot time ?" 7 40
sel=$?
case $sel in
   0) echo " Dantracker autostart selected "
      update-rc.d tracker defaults 95 5 ;;
   1) echo "No autostart for Dantracker"
    update-rc.d -f tracker remove ;;
esac

if grep -Fq "Dantracker" /etc/init.d/tracker
then
echo -e "${Green} /etc/init.d/tracker already modified for Raspberry. ${Reset}"
else
sed -i.bak "s/# Short-Description: Start/stop APRS tracker/# Short-Description: Start/stop APRS Dantracker/1" /etc/init.d/tracker
sed -i 's/echo "heartbeat"/#echo "heartbeat"/' /etc/init.d/tracker
sed -i 's/echo "default-on"/#echo "default-on"/' /etc/init.d/tracker
echo -e "${Green} /etc/init.d/tracker modified for Raspberry. ${Reset}"
fi
/home/pi/dantracker/detect_gps.py
echo -e "${BluW} Now reboot, and change the /home/pi/dantracker/aprs.ini, /etc/tracker/aprs_spy.ini and aprs_tracker.ini files ${Reset}"
echo -e "${BluW} Many thanks to all that have helped, especially Basil N7NIX. ${Reset}"
echo -e "${Green} Here are your usb - serial ports setting for gps and radio. ${Reset}"

echo -e "${BluW} For more info: http://www.pa0esh.nl/index.php?option=com_content&view=article&id=89&Itemid=106 ${Reset}"
echo -e "${BluW} Pull your updates from https://github.com/n7nix/dantracker ${Reset}"

}

#
# main loop of install script....
#

# Make sure only root can run our script
if [[ $EUID -ne 0 ]]; then
echo "This script must be run as root" 1>&2
   sudo su && /home/pi/test_install.sh
fi
whiptail --backtitle "Dantracker N7NIX branch" --title "Dantracker N7NIX branch installation - rev: 3rd July 2013" --yesno "This script installs the Dantracker - N7NIX branch on a Raspberry Pi using Debian Wheezy \n\
Source code at https://github.com/n7nix/dantracker \n\
Project description at www.pa0esh.nl \n\
Use a fresh formatted sd card with the latest Debian Wheezy (www.raspberry.org) \n\
Using parts of scripts written by K4GBB" --yes-button "Continue" --no-button "Abort" 12 80
exitstatus=$?
if [ $exitstatus = 0 ]; then

sleep 1
else
echo -e "${YelRed} Installation aborted. ${Reset}"
    exit
fi

whiptail --backtitle "Dantracker N7NIX branch" --title "Dantracker project installation choices." --checklist --separate-output "Make your choice:" 12 80 4 \
"Raspberry" "update & Upgrade" on \
"Ax.25" "installation" on \
"AP" "Access Point installation" on \
"Dantracker" "N7NIX installation" on 2>results

while read choice
do
case $choice in
                Raspberry) raspiupdate
                ;;
                Ax.25) Ax25Install
                ;;
                AP) AP_Install
                ;;
                Dantracker) N7NIX_install
                ;;
                *)
                ;;
        esac
done < results

# End of main loop
