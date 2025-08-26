#!/bin/sh
# Tester script for assignment 1 and assignment 2
# Author: Siddhant Jajoo
# Modified for Assignment 4 Buildroot integration


set -e
set -u


NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data


# Read username from the correct location (installed by Buildroot)
username=$(cat /etc/finder-app/conf/username.txt)


# Use the cross-compiled applications installed by Buildroot
WRITER_APP="writer"
FINDER_SCRIPT="finder.sh"


if [ $# -lt 3 ]
then
	echo "Using default value ${WRITESTR} for string to write"
	if [ $# -lt 1 ]
	then
		echo "Using default value ${NUMFILES} for number of files to write"
	else
		NUMFILES=$1
	fi	
else
	NUMFILES=$1
	WRITESTR=$2
	WRITEDIR=/tmp/aeld-data/$3
fi


MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"


echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"


# Remove any previous build artifacts (don't try to build natively)
rm -rf "${WRITEDIR}"


# Create the write directory
mkdir -p "$WRITEDIR"
if [ -d "$WRITEDIR" ]
then
	echo "$WRITEDIR created"
else
	echo "Failed to create $WRITEDIR"
	exit 1
fi


# Use the cross-compiled writer utility from PATH
for i in $(seq 1 $NUMFILES)
do
	$WRITER_APP "$WRITEDIR/${username}$i.txt" "$WRITESTR"
done


# Use the finder script from PATH
OUTPUTSTRING=$($FINDER_SCRIPT "$WRITEDIR" "$WRITESTR")


# Write result to file for assignment 4 requirement
echo "$OUTPUTSTRING" > /tmp/assignment4-result.txt
echo "Result written to /tmp/assignment4-result.txt"


# Clean up
rm -rf /tmp/aeld-data


set +e
echo "${OUTPUTSTRING}" | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
	echo "success"
	exit 0
else
	echo "failed: expected ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
	exit 1
fi