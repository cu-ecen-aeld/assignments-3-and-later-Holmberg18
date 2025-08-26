#!/bin/sh


#Check if both parameters are provided
if [ $# -lt 2 ]; then
    if [ -z "$1" ] && [ -z "$2" ]; then
        echo "Error: Both directory name and search string are missing" >&2
    elif [ -z "$1" ]; then
        echo "Error: Directory name is missing" >&2
    else
        echo "Error: Search string is missing" >&2
    fi


    echo "Usage: $0 <filesdir> <searchstr>" >&2
    exit 1
fi


filesdir="$1"
searchstr="$2"


#Check if directory exists and is readable
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a valid directory." >&2
    exit 1
fi


#Get number of files containing the search string
X=$(grep -rl --binary-files=without-match "$searchstr" "$filesdir" | wc -l)


#Get total number of matching lines
Y=$(grep -r --binary-files=without-match "$searchstr" "$filesdir" | wc -l)


#Print results
echo "The number of files are $X and the number of matching lines are $Y"