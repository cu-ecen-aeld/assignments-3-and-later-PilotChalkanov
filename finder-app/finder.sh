#!/bin/bash

filesdir=$1
searchstr=$2
if [ -z $1 ] || [ -z $2 ]
  then
      echo "Error: Missing required arguments."
      exit 1
elif [ "$#" -ne 2 ]; then
      echo "Error: Exactly 2 arguments are required."
      exit 1
elif [ ! -d "$filesdir" ]
  then
    echo "file $filesdir doesnt exist"
    exit 1
else
    files_cnt=0
    match_count=0

    for file in "$filesdir"/*; do
            if [ -f "$file" ]; then
                files_cnt=$((files_cnt + 1))
                cnt_lines=$(grep -c "$searchstr" "$file")
                match_count=$((match_count + cnt_lines))
            fi
        done
    echo  "The number of files are $files_cnt and the number of matching lines are $cnt_lines"
fi
