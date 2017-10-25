#!/bin/bash
#
# Minimal install of N7NIX version of dantracker

user=$(whoami)

package_name="dantracker"
FILELIST="aprs aprs-ax25"
JQUERY_DIR="/usr/share/$package_name/jQuery"
JQUERY_UI_FILES="jquery-ui-1.10.3.zip"
JQUERY_DT_FILES="DataTables-1.9.4.zip"
NODE_UI_DIR="/home/$user/projectlocker/udr56k/software/webdev/trunk/nodeUI"
#NODE_UI_DIR="../nodeUI"

#-- creates directory $1, if it does not exist
checkDir() {
    if [ ! -d "$1" ] ; then
        mkdir -p "$1"
    fi
}

for binfile in `echo ${FILELIST}` ; do
if [ ! -e "$binfile" ] ; then
  echo "file $binfile DOES NOT exist, run make"
  exit 1
fi
done

if [ ! -d "$NODE_UI_DIR" ] ; then
  echo "NodeUI source directory $NODE_UI_DIR DOES NOT exist"
  exit 1
fi

if [[ $EUID -ne 0 ]]; then
  echo "*** You must be root user ***" 2>&1
  exit 1
fi

# Test if directory /usr/share/dantracker exists
checkDir /usr/share/$package_name
# Test if directory /usr/share/dantracker/jQuery exists
checkDir $JQUERY_DIR

# Test if jquery javascript library has already been installed
if [ ! -e "$JQUERY_DIR/jquery.js" ] ; then
    echo "Installing jQuery directory"
    wget http://bit.ly/jqsource -O jquery.js
    jqversion=$(grep -m 1 -i core_version jquery.js | cut -d '"' -f2 | cut -d '"' -f1)
    echo "installing jquery version $jqversion"
    cp jquery.js $JQUERY_DIR
    echo "Installing jquery UI files"
    wget http://jqueryui.com/resources/download/$JQUERY_UI_FILES -P $JQUERY_DIR
    unzip $JQUERY_DIR/$JQUERY_UI_FILES -d $JQUERY_DIR
    echo "Installing jquery datatable files"
    wget http://www.datatables.net/releases/$JQUERY_DT_FILES -P $JQUERY_DIR
    unzip $JQUERY_DIR/$JQUERY_UI_FILES -d $JQUERY_DIR
else
    echo "jQuery libraries ALREADY installed"
fi

# Test if directory /etc/tracker exists
checkDir /etc/tracker

# Test if config files already exist
if [ ! -e "/etc/tracker/aprs_tracker.ini" ] ; then
        echo "Copying config files"
        rsync -av examples/aprs_spy.ini /etc/tracker
        cp examples/aprs_tracker.ini /etc/tracker
fi

cp scripts/tracker* /etc/tracker
cp scripts/.screenrc.trk /etc/tracker
cp scripts/.screenrc.spy /etc/tracker
rsync -av --cvs-exclude --include "*.js" --include "*.html" --include "*.css" --exclude "*" webapp/ /usr/share/$package_name/
rsync -av --cvs-exclude --include "*.png" --exclude "Makefile" images /usr/share/$package_name/

echo "Install tracker & spy binaries"
cp aprs /usr/local/bin
cp aprs-ax25 /usr/local/bin

echo "Install nodeUI"
rsync -av --cvs-exclude --include $NODE_UI_DIR/*.js --include $NODE_UI_DIR/*.html /usr/share/$package_name/
rsync -av --cvs-exclude $NODE_UI_DIR/Scripts /usr/share/$package_name/
rsync -av --cvs-exclude $NODE_UI_DIR/Styles  /usr/share/$package_name/
rsync -av --cvs-exclude $NODE_UI_DIR/images/*big2.png /usr/share/$package_name/images
rsync -av --cvs-exclude $NODE_UI_DIR/images/weblog*.png /usr/share/$package_name/images
rsync -av --cvs-exclude $NODE_UI_DIR/*.html /usr/share/$package_name/

exit 0
