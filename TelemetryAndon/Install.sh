#!/bin/sh
apt-get update  # To get the latest package lists
apt-get install libi2c-dev libjson-c-dev libpaho-mqtt-dev -y

wget https://project-downloads.drogon.net/wiringpi-latest.deb
dpkg -i wiringpi-latest.deb
rm wiringpi-latest.deb

chmod u+x *.sh
cd src
cc -Wall main.c am2315.c -li2c -lwiringPi -ljson-c -lpaho-mqtt3c -o Telemetry
mv Telemetry ../bin/Telemetry
chmod u+x ../bin/Telemetry 
chmod u+x ../bin/start.sh


#!/usr/bin/bash

##
## Creates Service file 
cd ..
cd frontend
P_FILE=$(pwd)
FILE="/usr/bin/python3 ${P_FILE}/app.py"

# check if argument is passed
if test -f "$FILE"; then
    echo "file not present stopping"
    exit 1
fi

# remove the double quotes
DESCRIPTION="Telemetry Service"
SERVICE_NAME="Telemetry"

# check if service is active
IS_ACTIVE=$(sudo systemctl is-active $SERVICE_NAME)
if [ "$IS_ACTIVE" = "active" ]; then
    # restart the service
    echo "Service is running"
    echo "Stopping service"
    sudo systemctl stop $SERVICE_NAME
    echo "Stopped restarted"
fi
    # create service file
echo "Creating service file"
sudo rm /etc/systemd/system/${SERVICE_NAME}.service
sudo cat > /etc/systemd/system/${SERVICE_NAME}.service << EOF
[Unit]
After=network.target

[Service]
WorkingDirectory=$P_FILE
ExecStart=$FILE
Restart=always

[Install]
WantedBy=multi-user.target
EOF
    # restart daemon, enable and start service
    echo "Reloading daemon and enabling service"
    sudo systemctl daemon-reload 
    sudo systemctl enable ${SERVICE_NAME} # remove the extension
    sudo systemctl start ${SERVICE_NAME}
    echo "Service Started"


exit 0

