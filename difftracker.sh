#!/bin/bash
#
# file: diffrepo.sh
# Output a unified diff of reference files
#
# $Id: difftracker.sh 401 2013-05-14 22:02:10Z basil@pacabunga.com $
#

user=$(whoami)

#REFDIR="/home/$user/dev/projectlocker/udr56k/software/apps/trunk/aprs/tracker"
#BASEDIR="/home/$user/dev/projectlocker/udr56k/software/apps/trunk/aprs/plutracker"
REFDIR="/home/$user/dev/github/dantracker"
BASEDIR="/home/$user/dev/github/nixtracker"

RSYNC_TMP_FILENAME="/home/$user/tmp/diffrsyncout.txt"


# find . -type f -printf "%p\n"

rsync -rvnc --delete  --cvs-exclude $REFDIR/ $BASEDIR/ > $RSYNC_TMP_FILENAME


filecnt=0
filecnt_different=0

while read line ; do
	# Check first character of line
	firstchar=$(echo $line | cut -c1)

	# Check for a NULL line
	if [ -z "$firstchar" ] ; then
	    continue
	fi

	# Check for a comment character
	if [ "$firstchar" == "#" ] ; then
	    continue
	fi

	# Check for a dot character
	if [ "$firstchar" == "." ] ; then
	    continue
	fi

	file_name=$(echo $line | grep " ")

	#  Test for a non empty string
	if [ -n "$file_name" ] ; then
		extra_name=$(echo $file_name | grep -i "deleting" | cut -d' ' -f2)
		if [ -n "$extra_name" ] ; then
			echo "Extra file in $BASEDIR: $extra_name"
		fi
		continue;
	fi

	file_name="`basename $line`"
#	echo "Filename base: $file_name"

	# Get rid of any text following filename
	file_name=$(echo $line | cut -d' ' -f1)

	#  Test if file exists in base directory
	if [ ! -e "$BASEDIR/$file_name" ] ; then
			echo "Extra file in $REFDIR: $file_name"
		continue;
	fi
	#  Test if file exists in repo
	if [ -e "$REFDIR/$file_name" ] ; then
#		echo "$file_name exists!"
		diff -qbBw $BASEDIR/$file_name $REFDIR/$file_name
		# Exit status is 0 if inputs are the same
		if [ $? -ne 0 ]; then
			echo "=============== $file_name ==============="
			echo "$file_name are different"
			filecnt_different=$((filecnt_different+1))
#			diff -u $BASEDIR/$file_name $REFDIR/$file_name
		fi

		filecnt=$((filecnt+1))
	else
	        echo "*************** $file_name ***************"
		echo "Need to create $REFDIR/$file_name"
		diff -bBw $REFDIR/$file_name $BASEDIR/$file_name
	fi

done < $RSYNC_TMP_FILENAME

echo
echo "Files processed $filecnt, $filecnt_different files need updating"

exit 0

