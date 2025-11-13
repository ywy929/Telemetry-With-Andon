# Telemetry With Andon

An industrial IoT telemetry system for Raspberry Pi that monitors environmental sensors (temperature, humidity, light, and air quality) and publishes real-time data to MQTT brokers with intelligent alarm management.

## Overview

TelemetryAndon is a production-ready environmental monitoring solution designed for manufacturing and logistics environments. It provides continuous sensor monitoring, MQTT-based data publishing, remote configuration management, and threshold-based alarm functionality.

## Features

- **Multi-Sensor Support**: Temperature, humidity, light (lux), and PM2.5 air quality monitoring
- **MQTT Integration**: Real-time data publishing with remote configuration updates
- **Intelligent Alarm System**: Threshold-based alerting with time-window scheduling and false-alarm prevention
- **Web Configuration Interface**: Bootstrap-based UI for easy parameter management
- **Remote Management**: MQTT-based configuration updates and service control
- **High Reliability**: Service auto-recovery, graceful restart mechanisms, and rolling averages

## System Architecture

```
┌─────────────────────────────────────────────────────┐
│         Raspberry Pi (Linux 32-bit Bullseye)        │
├─────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────┐   │
│  │      Main Telemetry Service (C)              │   │
│  │  ├─ Sensor Reading (AHT20/AM2315/BH1750)    │   │
│  │  ├─ MQTT Publishing                          │   │
│  │  ├─ Configuration Management                 │   │
│  │  └─ Alarm/Buzzer Control                     │   │
│  └──────────────────────────────────────────────┘   │
│           ↓              ↓              ↓            │
│      [I2C Bus]   [GPIO Pins]    [config.json]       │
│           ↓              ↓                           │
│  ┌────────────────┐  ┌─────────────┐               │
│  │  I2C Sensors   │  │  GPIO Pins  │               │
│  │  - AHT20       │  │  - LED      │               │
│  │  - BH1750      │  │  - BUZZER   │               │
│  │  - AM2315      │  │  - ALARM    │               │
│  └────────────────┘  └─────────────┘               │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │    Flask Web UI (Python)                     │   │
│  │    http://localhost:8080                     │   │
│  │    └─ Configuration Management               │   │
│  │    └─ Service Control                        │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
         ↓
    ┌─────────────┐
    │ MQTT Broker │
    │ (IoT Hub)   │
    └─────────────┘
```

## Project Structure

```
TelemetryAndon-main/
├── TelemetryAndon/
│   ├── src/                       # C source code (Production)
│   │   ├── main.c                 # Main telemetry service (~1309 lines)
│   │   ├── am2315.c               # AM2315 sensor driver
│   │   └── am2315.h               # AM2315 sensor header
│   ├── bin/                       # Binaries and runtime files
│   │   ├── Telemetry              # Compiled C executable (Production)
│   │   ├── config.json            # Configuration file
│   │   ├── start.sh               # Service startup script
│   │   ├── AHT20.py               # Python AHT20 sensor driver (Experimental)
│   │   ├── aht.py                 # Python test script (Incomplete)
│   │   └── log.txt                # Service log file
│   ├── frontend/                  # Web UI (Production)
│   │   ├── app.py                 # Flask configuration interface
│   │   └── templates/
│   │       └── index.html         # Configuration web page
│   ├── Install.sh                 # Installation script
│   └── Instructions.txt           # Setup guide
└── README.md                      # This file
```

## Hardware Requirements

### Raspberry Pi
- Model: Raspberry Pi 3/4 (32-bit Bullseye Linux)
- I2C interface enabled
- GPIO pins available

### Supported Sensors
- **AHT20**: Temperature/Humidity sensor (I2C address: 0x38) - Primary
- **AM2315**: Temperature/Humidity sensor (I2C address: 0x5c) - Fallback
- **BH1750**: Light/Lux sensor (I2C address: 0x23)
- **PM2.5**: Air quality sensor (GPIO-based detection)

### GPIO Pin Configuration
- **LED Pin**: GPIO21 (WiringPi 29)
- **BUZZER Pin**: GPIO20 (WiringPi 28, Physical 38)
- **ALARM Pin**: GPIO26 (WiringPi 25, Physical 37)

## Software Requirements

### C Implementation (Production)
- WiringPi library
- Paho MQTT C client
- json-c library
- I2C development libraries
- Standard POSIX C libraries

### Python Components (Experimental/Web UI)
- Python 3.x
- Flask framework
- smbus2 (for AHT20 driver)

## Installation

### 1. Clone the Repository
```bash
cd /home/pi
git clone <repository-url>
cd TelemetryAndon-main/TelemetryAndon
```

### 2. Run Installation Script
```bash
chmod +x Install.sh
./Install.sh
```

This will:
- Install required dependencies
- Compile the C telemetry service
- Set up necessary permissions
- Configure I2C interface

### 3. Configure Settings
Edit [bin/config.json](TelemetryAndon/bin/config.json) with your MQTT broker details:

```json
{
  "username": "your-mqtt-username",
  "password": "your-mqtt-password",
  "host": "mqtt-broker-address",
  "port": 1883,
  "clientid": "Tele001",
  "interval": 60,
  "luxoffset": 0,
  "tempoffset": "0",
  "humoffset": 0,
  "pm25offset": 0,
  "tempminlimit": 22,
  "tempmaxlimit": 35,
  "humminlimit": 10,
  "hummaxlimit": 80,
  "buzzerenabled": "1",
  "starthour": 12,
  "startminute": 0,
  "endhour": 12,
  "endminute": 0,
  "maxbuzzerduration": 3
}
```

### 4. Start the Service
```bash
cd bin
./start.sh
```

### 5. Access Web Configuration Interface (Optional)
```bash
cd frontend
python3 app.py
```

Access at: `http://raspberry-pi-ip:8080`

## Usage

### Starting the Telemetry Service

**As a foreground process:**
```bash
cd TelemetryAndon/bin
./Telemetry
```

**As a background service:**
```bash
./start.sh
```

**As a systemd service (production):**
```bash
./Telemetry -service
```

### Stopping the Service

Send `Q` or `q` message to MQTT topic:
```
Topic: mqtt/telemetry/<clientid>/setting
Message: Q
```

Or kill the process manually.

### Web Configuration Interface

1. Start the Flask application:
   ```bash
   cd frontend
   python3 app.py
   ```

2. Open browser to `http://<raspberry-pi-ip>:8080`

3. Update configuration parameters via the web form

4. Click "Save Settings" - the service will automatically restart with new configuration

## MQTT Communication Protocol

### Published Data (Topic: `mqtt/telemetry/<clientid>`)

Every `interval` seconds, the service publishes sensor readings:

```json
{
  "messageId": 1,
  "date": "13-11-2025 14:30:45",
  "devicename": "Tele016",
  "operator": "update",
  "info": [
    {
      "temp": "24.5",
      "hum": "65.3",
      "pm25": "0",
      "lux": "450"
    }
  ]
}
```

### Configuration Updates (Topic: `mqtt/telemetry/<clientid>/setting`)

Subscribe to receive configuration updates:

```json
{
  "deviceid": "Tele016",
  "operator": "config",
  "interval": 30,
  "offset": [{"temp": 0.5, "hum": 2, "pm25": 0, "lux": 10}],
  "tempminlimit": 20,
  "tempmaxlimit": 35,
  "humminlimit": 10,
  "hummaxlimit": 80,
  "buzzerenabled": 1,
  "starttime": "12:00",
  "endtime": "18:00",
  "maxbuzzerduration": 5,
  "stopbuzzeronce": 1
}
```

### Special Commands

| Command | Description |
|---------|-------------|
| `Q` or `q` | Graceful shutdown |
| `?` | Query device IP address |

## Alarm System

### Threshold-Based Alerting

The system monitors temperature and humidity against configured limits:
- **Temperature**: `tempminlimit` to `tempmaxlimit`
- **Humidity**: `humminlimit` to `hummaxlimit`

### False Alarm Prevention

- **180-second rolling average**: Prevents momentary spikes from triggering alarms
- Only sustained threshold violations activate the buzzer

### Time-Window Scheduling

Configure buzzer activation windows:
- **Start time**: `starthour:startminute`
- **End time**: `endhour:endminute`
- Example: No buzzer between 12:00 and 18:00

### Maximum Duration Limit

- **maxbuzzerduration**: Maximum minutes buzzer can remain active
- Prevents excessive noise pollution

### Manual Override

**Method 1: MQTT Message**
```json
{
  "deviceid": "Tele016",
  "operator": "config",
  "stopbuzzeronce": 1
}
```

**Method 2: File Flag**
Create or touch the file:
```bash
touch bin/stopbuzzeronce.txt
```

### LED Indicator

- **LED blinking**: Indicates buzzer is active
- Visual confirmation of alarm state

## Configuration Parameters

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| `username` | string | MQTT broker username | "master" |
| `password` | string | MQTT broker password | - |
| `host` | string | MQTT broker IP/hostname | - |
| `port` | integer | MQTT broker port | 1883 |
| `clientid` | string | Unique device identifier | "Tele001" |
| `interval` | integer | Sensor reading interval (seconds) | 60 |
| `luxoffset` | float | Light sensor calibration offset | 0 |
| `tempoffset` | float | Temperature calibration offset | 0 |
| `humoffset` | float | Humidity calibration offset | 0 |
| `pm25offset` | float | PM2.5 sensor calibration offset | 0 |
| `tempminlimit` | float | Minimum temperature threshold (°C) | 22 |
| `tempmaxlimit` | float | Maximum temperature threshold (°C) | 35 |
| `humminlimit` | float | Minimum humidity threshold (%) | 10 |
| `hummaxlimit` | float | Maximum humidity threshold (%) | 80 |
| `buzzerenabled` | string | Enable/disable buzzer ("1"/"0") | "1" |
| `starthour` | integer | Buzzer time window start (hour) | 12 |
| `startminute` | integer | Buzzer time window start (minute) | 0 |
| `endhour` | integer | Buzzer time window end (hour) | 12 |
| `endminute` | integer | Buzzer time window end (minute) | 0 |
| `maxbuzzerduration` | integer | Maximum buzzer duration (minutes) | 3 |

## Implementation Status

### ✅ C Implementation (Production Ready)

**Location:** [src/main.c](TelemetryAndon/src/main.c)

**Status:** Complete and fully functional

**Features:**
- ✅ Multi-sensor support (AHT20, AM2315, BH1750, PM2.5)
- ✅ MQTT publishing and subscription
- ✅ Configuration management with hot-reload
- ✅ Intelligent alarm/buzzer system
- ✅ GPIO control (LED, buzzer, alarm)
- ✅ Service mode with auto-recovery
- ✅ Rolling average false-alarm prevention
- ✅ Time-window scheduling
- ✅ Remote configuration updates
- ✅ Network information query

**Compilation:**
```bash
cd TelemetryAndon
gcc -o bin/Telemetry src/main.c src/am2315.c -lwiringPi -ljson-c -lpaho-mqtt3c -lm
```

### ⚠️ Python Implementation (Experimental/Incomplete)

**Location:** [bin/AHT20.py](TelemetryAndon/bin/AHT20.py), [bin/aht.py](TelemetryAndon/bin/aht.py)

**Status:** Partial implementation, not production-ready

**What's Implemented:**
- ✅ AHT20 sensor driver class ([AHT20.py](TelemetryAndon/bin/AHT20.py))
- ✅ Basic sensor reading functionality

**What's Missing:**
- ❌ MQTT integration
- ❌ Alarm/buzzer logic
- ❌ Multi-sensor support (BH1750, PM2.5)
- ❌ Configuration management
- ❌ GPIO control
- ❌ Service mode operation
- ❌ Remote configuration updates

**Note:** The Python version serves as an experimental proof-of-concept. For production deployments, use the C implementation.

### ✅ Flask Web UI (Production Ready)

**Location:** [frontend/app.py](TelemetryAndon/frontend/app.py)

**Status:** Complete and functional

**Features:**
- ✅ Web-based configuration interface
- ✅ Bootstrap 4 responsive design
- ✅ Configuration file management
- ✅ Automatic service restart on settings change
- ✅ RESTful API endpoints

## Troubleshooting

### I2C Sensor Not Detected
```bash
# Enable I2C interface
sudo raspi-config
# Navigate to: Interface Options > I2C > Enable

# Check connected I2C devices
i2cdetect -y 1
```

Expected output should show sensors at:
- 0x23 (BH1750)
- 0x38 (AHT20)
- 0x5c (AM2315, if installed)

### MQTT Connection Failed
- Verify broker credentials in [config.json](TelemetryAndon/bin/config.json)
- Check network connectivity: `ping <mqtt-broker-ip>`
- Verify broker is running and port 1883 is accessible
- Check firewall settings

### Buzzer Not Working
- Verify GPIO pin connections
- Check `buzzerenabled` parameter in config
- Verify time-window settings (`starthour`, `endhour`)
- Check if manual override is active ([stopbuzzeronce.txt](TelemetryAndon/bin/stopbuzzeronce.txt))

### Service Won't Start
```bash
# Check if process is already running
ps aux | grep Telemetry

# Check log file for errors
cat bin/log.txt

# Verify file permissions
chmod +x bin/Telemetry
chmod +x bin/start.sh
```

### Configuration Changes Not Applied
- Service must restart for config changes to take effect
- Verify [config.json](TelemetryAndon/bin/config.json) has valid JSON syntax
- Check log file for parsing errors
- Use web interface to ensure proper formatting

## Development

### Building from Source

```bash
cd TelemetryAndon
gcc -o bin/Telemetry src/main.c src/am2315.c \
    -lwiringPi -ljson-c -lpaho-mqtt3c -lm \
    -I/usr/local/include \
    -L/usr/local/lib
```

### Debug Mode

Run without `-service` flag to see verbose output:
```bash
cd bin
./Telemetry
```

### Testing MQTT Communication

Using mosquitto clients:
```bash
# Subscribe to telemetry data
mosquitto_sub -h <broker> -u <username> -P <password> -t "mqtt/telemetry/#"

# Send configuration update
mosquitto_pub -h <broker> -u <username> -P <password> \
  -t "mqtt/telemetry/Tele001/setting" \
  -m '{"deviceid":"Tele001","operator":"config","interval":30}'
```

## API Reference

### Flask Web UI Endpoints

**GET /** - Configuration web interface
```
Response: HTML configuration page
```

**GET /config** - Get current configuration
```json
Response: {
  "username": "master",
  "password": "******",
  "host": "101.99.74.167",
  "clientid": "Tele016",
  ...
}
```

**POST /settings** - Update configuration
```json
Request Body: {
  "username": "new-username",
  "password": "new-password",
  ...
}

Response: {
  "success": true,
  "message": "Settings saved successfully!"
}
```
