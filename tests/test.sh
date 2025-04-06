#!/bin/bash
echo "Shell env: ${SHELL}"

SILENT=false
if [ -z "$1" ]; then
    echo "Usage $0 <seconds> [silent]"
    exit 1
fi

if [ ! -z "$2" ]; then
    SILENT=true
fi

seconds=$1

while [ $seconds -gt 0 ]; do

    if [ "$SILENT" = false ]; then
        echo -ne "Remaining time: ${seconds}s       \r"
    fi

    sleep 1
    ((seconds--))
done

if [ "$SILENT" = false ]; then
    echo -ne "Remaining time: 0s - End!\n"
fi
echo "Exited... my pid was: " $$;
echo ""
