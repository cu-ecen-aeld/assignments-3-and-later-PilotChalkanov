#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Error: Exactly 2 arguments are required."
    exit 1
else
    dirpath=$(dirname "$1")

    if [ ! -d "$dirpath" ]; then
        mkdir -p "$dirpath"
        if [ $? -ne 0 ]; then
            echo "Error: Could not create directory $dirpath"
            exit 1
        fi
    fi
    echo "Creating new file: $1"
    echo "Writing $2 to path: $1"
    echo "$2" > "$1"
    if [ $? -ne 0 ]; then
        echo "Error: Could not create file $1"
        exit 1
    fi
fi