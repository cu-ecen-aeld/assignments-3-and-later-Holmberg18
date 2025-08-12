#!/bin/bash

#Check if both parameters are provided
if [ $# -lt 2 ]; then
    if [ -z "$1" ] && [ -z "$2" ]; then
        echo "Error: Both writefile and writestr are missing" >&2
    elif [ -z "$1" ]; then
        echo "Error: Writefile is missing" >&2
    else
        echo "Error: Writestr is missing" >&2
    fi

    echo "Usage: $0 <writefile> <writestr>" >&2
    exit 1
fi

writefile="$1"
writestr="$2"


# Create parent directories if missing, exit on failure
if ! mkdir -p "$(dirname "$writefile")"; then
    echo "Error: Failed to create the directory path for '$writefile'" ?&2
    exit 1
fi

# Overwrite file or create a new file, exit on failure
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Failed to write to '$writefile'" >&2
    exit 1
fi

echo "A file called '$writefile' containing new content: '$writestr' has been created/updated"

