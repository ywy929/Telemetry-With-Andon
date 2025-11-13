#!/bin/sh
until ./Telemetry -service; do
    echo " $?.  Respawning.." >&2
    sleep 1
done
