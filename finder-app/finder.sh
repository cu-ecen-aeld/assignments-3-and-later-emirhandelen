#!/bin/sh

if [ $# -ne 2 ]; then
    echo "Please enter 2 arguments: filesdir and searchstr"
    exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]; then
    echo "$filesdir is not a directory"
    exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)

matching_lines=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $file_count and the number of matching lines are $matching_lines"
