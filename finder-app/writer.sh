#!/bin/sh

if [ $# -ne 2 ]; then
    echo "2 arguments are required: writefile and writestr"
    exit 1
fi

writefile=$1
writestr=$2

writedir=$(dirname "$writefile")

if ! mkdir -p "$writedir"; then
    echo "Could not create directory $writedir"
    exit 1
fi

if ! echo "$writestr" > "$writefile"; then
    echo "Could not create file $writefile"
    exit 1
fi
