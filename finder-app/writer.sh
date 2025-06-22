#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Please provide both arguments - writefile and writestr"
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
