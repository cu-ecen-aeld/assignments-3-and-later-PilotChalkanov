#!/bin/bash

filesdir=$1
searchstr=$2
if [ "$#" -ne 2 ]; then
      echo "Error: Exactly 2 arguments are required."
      exit 1
elif [ ! -d "$filesdir" ]
  then
    echo "file $filesdir doesnt exist"
    exit 1
else
    files_cnt=$(find "$filesdir" -type f | wc -l)
    lines_cnt=$(grep -Rl "$searchstr" "$filesdir" | wc -l)
    echo  "The number of files are $files_cnt and the number of matching lines are $lines_cnt"
fi
