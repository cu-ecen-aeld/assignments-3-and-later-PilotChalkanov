#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Error: Exactly 2 arguments are required."
    exit 1
else
    dirpath=$(dirname "$1")
    filename=$(basename "$1")

    if [ ! -d "$dirpath" ]; then
        mkdir -p "$dirpath"
        if [ $? -ne 0 ]; then
            echo "Error: Could not create directory $dirpath"
            exit 1
        fi
    fi

    cd "$dirpath"
    if [ $? -ne 0 ]; then
        echo "Error: Could not change to directory $dirpath"
        exit 1
    fi

    echo "Creating new file: $filename"
    echo "Writing $2 to path: $filename"
    echo "$2" > "$filename"
    if [ $? -ne 0 ]; then
        echo "Error: Could not create file $filename"
        exit 1
    fi

    chmod a+w "$filename"
    if [ $? -ne 0 ]; then
        echo "Error: Could not change permissions for file $filename"
        exit 1
    fi
fi