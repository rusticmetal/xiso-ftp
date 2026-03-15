#!/bin/bash

# Run this script to extract and transfer all .iso files in ./SOURCE_FOLDER/ to your Xbox. .7z archives containing .iso files also work.
# Don't put anything else in ./SOURCE_FOLDER/ besides .iso and .7z files.
# Your iso/archive files to be transferred must be in the ./SOURCE_FOLDER/ directory, which is set up to be a subdirectory located within the same directory as this script.
# This script will also move all the isos to ./TRANSFERRED_ISOS_FOLDER/ after to prevent confusion.
# ../build/xiso-ftp.exe must also exist in a relative location to this script, but can be changed of course.
# Remember to set up all the values below with your own

IP="10.0.0.1"
USER="xbox"
PASS="xbox"
REMOTE_PARENT_FOLDER="F:/games/" #this is your master game folder on your xbox
SOURCE_FOLDER="games"
TRANSFERRED_ISOS_FOLDER="transferred_games"
XISO_FTP_LOCATION="../build/xiso-ftp.exe"

SCRIPT_DIRECTORY="$(dirname "$(realpath "$0")")"
mkdir -p $SCRIPT_DIRECTORY/$SOURCE_FOLDER
mkdir -p $SCRIPT_DIRECTORY/$TRANSFERRED_ISOS_FOLDER

for GAME_FILE in "$SCRIPT_DIRECTORY/$SOURCE_FOLDER"/*; do
    if [ ! -f "$GAME_FILE" ]; then
        continue
    fi

    case "$GAME_FILE" in
        *.iso) 
            FLAGS="-f"
            ;;
        *.7z)
            FLAGS="-a -f"
            ;;
        *) 
            continue
            ;;
    esac

    echo "$SCRIPT_DIRECTORY/$XISO_FTP_LOCATION $FLAGS $REMOTE_PARENT_FOLDER $GAME_FILE"

    "$SCRIPT_DIRECTORY/$XISO_FTP_LOCATION" $FLAGS "$REMOTE_PARENT_FOLDER" "$GAME_FILE" <<EOF
$IP
$USER
$PASS
EOF

    mv "$GAME_FILE" "$SCRIPT_DIRECTORY/$TRANSFERRED_ISOS_FOLDER/"
done