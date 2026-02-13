# AGENTS.md - Development Guidelines for PowMr ESP32 Inverter Monitor

This file contains comprehensive development guidelines and commands for the PowMr ESP32-based inverter monitoring system. The project consists of two main components:

1. **ESP32 Firmware (Primary)**: Arduino/PlatformIO-based firmware that interfaces with PowMr hybrid inverter via Modbus RTU
2. **Python Bridge (Secondary)**: MQTT/HTTP to InfluxDB data bridge for long-term storage and visualization

As a rule, the default PlatformIO environment is `esp32_OTA`. Only use `esp32_USB` when explicitly required for direct USB serial connection.

---

## Project Structure

```
PowMr/
├── AGENTS.md                    # This file - development guidelines
├── README.md                    # Project documentation
├── platformio.ini               # PlatformIO configuration
├── .gitignore                   # Git ignore rules
├── src/
│   ├── main.cpp                 # Entry point: setup() and loop() only
│   ├── config.h                 # All #defines, constants, thresholds
│   ├── data.h                   # Data structures (ACData, DCData, InverterData)
│   ├── globals.h                # Global variables (extern declarations)
│   ├── modbus.cpp/h             # Modbus RTU communication
│   ├── energy.cpp/h             # Energy/battery calculations
│   ├── json_utils.cpp/h         # JSON serialization
│   ├── webserver.cpp/h          # Web server endpoints & SPIFFS handlers
│   ├── ota.cpp/h                # OTA update handling
│   ├── wifi.cpp/h               # WiFi management
│   ├── utils.cpp/h              # Utilities (EWMA, timing, uptime)
│   └── wifi.h                   # WiFi credentials (NOT tracked in git)
├── include/
│   └── README                   # Include directory placeholder
├── lib/                         # PlatformIO libraries (auto-managed)
├── data/                        # SPIFFS web files
│   ├── index.html               # Web dashboard HTML
│   ├── app.js                   # JavaScript for dashboard
│   ├── style.css                # Dashboard styling
│   └── names.json               # Field names/descriptions for UI
├── extras/
│   ├── config.ini               # Python bridge configuration template
│   ├── mqtt_json_2_influx.py    # Python data bridge script
│   └── registers-map.md         # Modbus register documentation
└── images/
    └── web-server.png           # Screenshot of web interface
```

---

## Quick Start

### Hardware Requirements
- ESP32 development board (ESP32-WROOM-32 or similar)
- PowMr hybrid inverter (tested with POW-LVM3.6M-24V and HVM2.4H)
- RS-232 to TTL serial adapter (e.g., MAX232 module, POW-LVM3.6M-24V uses Modbus over RS232, not RS485)
- 3.3V/5V logic level converter (if needed)
- 12V-24V battery bank (for testing)

### Initial Setup

1. **Create WiFi configuration file** (`src/wifi.h`):
```cpp
/****** wifi config *******************/
// Client WiFi settings (for normal operation)
const char *c_ssid = "Your_WiFi_Network_Name";
const char *c_password = "Your_WiFi_Password";

// AP fallback settings (when client WiFi unavailable)
const char *s_ssid = "ESP32-PowMr";
const char *s_password = "Your_AP_Password";

// Hostname for mDNS and network discovery
const char *hostname = "ESP32-PowMr";
```

2. **Configure hardware connections**:
- GPIO 16 (RXD2) → RS232 module TX
- GPIO 17 (TXD2) → RS232 module RX
- GND and +Vcc as needed
 
3. **Build and upload**:
```bash
pio run -t upload --environment esp32_OTA  # OTA upload
# OR
pio run -t upload -e esp32_USB             # USB upload
```

---

## Build/Lint/Test Commands

### PlatformIO (ESP32/Arduino) - Primary Development

**Note**: Use PlatformIO python venv installed in user folder (`~/.platformio/penv`) to ran pio commands rather than system-installed pio version.

```bash
# Build the project
pio run

# Build and upload to ESP32 via USB
pio run -t upload -e esp32_USB

# Build and upload via OTA (Over-The-Air)
pio run -t upload --environment esp32_OTA

# Monitor serial output
pio device monitor

# Clean build files
pio run -t clean

# Run all targets for default environment
pio run -t clean -t build -t upload -t monitor
```

**Important Notes**:
- The project uses **manual timing loops** instead of SimpleTimer to ensure consistent execution intervals
- This prevents multiple consecutive calls during long operations like OTA updates
- Default environment is `esp32_OTA` (defined in platformio.ini)

### Python Bridge (Secondary) - Running the Application

**Note**: This does not need to be ran for testing, unless the request has modified the file explicitly and yet ask the user about first.

```bash
# Custom config file for testing
cd ./extras
python mqtt_json_2_influx.py --config ./config.ini

# Using default config location, not for testing this is production one, user's business
cd ./extras
python mqtt_json_2_influx.py

```

### Testing

#### PlatformIO Tests (Unit Tests)
```bash
# Run unit tests
pio test

# Run tests for specific environment
pio test -e esp32_OTA

# Run tests with native platform (faster for development)
pio test -e native
```

#### Python Tests
```bash
# Run all tests
python -m pytest

# Run a specific test file
python -m pytest test_bridge.py

# Run a specific test function
python -m pytest test_bridge.py::test_mqtt_connection

# Run tests with coverage
python -m pytest --cov=mqtt_json_2_influx --cov-report=html
```

---

## Configuration

### ESP32 Firmware Configuration

Key configuration constants in `src/config.h`:

```cpp
// Serial configuration
#define MONITOR_SERIAL_SPEED 9600  // Debug serial speed
#define TXD2   GPIO_NUM_17         // Modbus TX pin
#define RXD2   GPIO_NUM_16         // Modbus RX pin

// Battery configuration
const float MAXIMUM_ENERGY = 12.8 * 100 * 2;  // Wh (12.8V * 100Ah * 2 batteries)
const float MINIMUM_VOLTAGE = 22.0;            // V (discharged)
const float MAXIMUM_VOLTAGE = 28.8;            // V (fully charged)

// Autonomy calculation
#define AUTONOMY_MAX_DAYS 2              // Maximum autonomy cap (days)
#define AUTONOMY_EFFICIENCY_CAP 93.0     // Maximum efficiency cap (%)
#define AUTONOMY_WINDOW_MINUTES 5.0      // Time window for averaging (minutes)

// Modbus configuration
#define MBUS_REGISTERS 61        // Number of registers to read (4501-4561)
#define CHUNK_SIZE 3            // Registers per read operation
#define RETRY_COUNT 4           // Retry attempts per chunk
#define CHUNK_DELAY_US 10       // Delay between chunks (microseconds)

// Dynamic read interval
#define INITIAL_READ_INTERVAL 5.0  // Seconds (after failures, minimum 5s, maximum 30s)
```

### Python Bridge Configuration

Edit `extras/config.ini`:

```ini
[source]
type = http  # or 'mqtt'

[http]
url = http://192.168.1.101/api/status
poll_interval = 15

[mqtt]
host = 192.168.1.100
port = 1883
username = your_username
password = your_password
client_id = mqtt_influx_bridge

[topics]
topics = /powmr/inverter.status
    /powmr/dc.voltage
    ; Add more topics as needed

[influxdb]
url = http://localhost:8086
org = your_org
bucket = powmr
token = your_token

# Optional cloud backup
[influxdb_cloud]
url = https://us-west-2-1.aws.cloud2.influxdata.com
token = your_cloud_token
org = your_org
bucket = powmr_cloud
```

---

## API Endpoints

The ESP32 web server provides the following endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web dashboard (index.html) |
| `/api/status` | GET | JSON data in InfluxDB line protocol format |
| `/style.css` | GET | Dashboard stylesheet |
| `/app.js` | GET | Dashboard JavaScript |
| `/names.json` | GET | Field names and descriptions for UI |

### Example JSON Response

```json
{
  "ac": {
    "input_voltage": 122.4,
    "input_freq": 60.7,
    "output_voltage": 121.5,
    "output_freq": 60.7,
    "output_va": 352,
    "output_watts": 341,
    "output_load_percent": 9,
    "power_factor": 0.97
  },
  "dc": {
    "pv_voltage": 88.3,
    "pv_power": 404,
    "pv_current": 4.58,
    "pv_energy_produced": 1250.5,
    "voltage": 27.4,
    "voltage_corrected": 27.4,
    "charge_current": 0,
    "discharge_current": 0,
    "charge_power": 0,
    "discharge_power": 0
  },
  "inverter": {
    "valid_info": 1,
    "op_mode": 4,
    "soc": 100,
    "gas_gauge": 85,
    "battery_energy": 2176.0,
    "temp": 44,
    "read_interval": 15,
    "read_time": 8.5,
    "read_time_mean": 7.2,
    "charger": 13,
    "eff_w": 84.4,
    "energy_spent_ac": 5432.1,
    "energy_source_ac": 0,
    "energy_source_batt": 50,
    "energy_source_pv": 50,
    "autonomy": 1440,
    "uptime": 86400
  }
}
```

**Important**: Always check `inverter.valid_info == 1` to confirm data is valid.

---

## Modbus Registers

### Read Registers (4501-4561)

The firmware reads 61 Modbus holding registers starting at address 4501. Key registers include:

| Register | Description | Type |
|----------|-------------|------|
| 4501 | Output Source Priority | settings |
| 4502 | AC Input Voltage | measurement |
| 4503 | AC Input Frequency | measurement |
| 4504 | PV Voltage | measurement |
| 4505 | PV Power (charging) | measurement |
| 4506 | Battery Voltage | measurement |
| 4508 | Battery Charge Current | measurement |
| 4509 | Battery Discharge Current | measurement |
| 4510 | Load Voltage | measurement |
| 4511 | Load Frequency | measurement |
| 4512 | Load Power (VA) | measurement |
| 4513 | Load Power (W) | measurement |
| 4514 | Load Percentage | measurement |
| 4555 | Charger Status (0=Off, 1=Idle, 2=Active) | measurement |
| 4557 | Temperature Sensor | measurement |

**Reference**: See `extras/registers-map.md` for complete register map.

---

## Code Style Guidelines

### General Principles
- **Clarity over cleverness**: Code should be readable and maintainable
- **Fail fast**: Validate inputs early and handle errors gracefully
- **Single responsibility**: Each function/method should do one thing well
- **DRY (Don't Repeat Yourself)**: Avoid code duplication
- **Explicit is better than implicit**: Be clear about what the code does

### Arduino/C++ Standards (Primary)

```cpp
// Classes: PascalCase
class InverterMonitor { };

// Functions/Methods: camelCase
void readInverterData() { }
void setupWifi() { }

// Variables: snake_case
float battery_voltage;
unsigned long last_read_time;

// Constants: UPPER_SNAKE_CASE
const unsigned long READ_INTERVAL_MS = 15000;

// Private members: prefix with underscore
class MyClass {
  private:
    int _privateVariable;
    void _privateMethod();
};
```

- Use 2-4 spaces for indentation (no tabs)
- Line length: 120 characters maximum
- Add comments for complex logic and hardware interactions

### Python Standards (Secondary)

```python
# Classes: PascalCase
class DataBridge:
    pass

# Functions/Methods: snake_case
def check_and_write(self, measurement, field, value):
    pass

# Variables: snake_case
current_values = {}
last_write_time = {}

# Constants: UPPER_SNAKE_CASE
HEARTBEAT_INTERVAL = 60

# Private attributes: prefix with underscore
class MyClass:
    def __init__(self):
        self._private_attribute = None
```

- Follow PEP 8 style guide
- Use 4 spaces for indentation
- Line length: 88 characters (Black default)
- Add docstrings to all public functions and classes

### Python Import Order
```python
# Standard library
import sys
import time
from datetime import datetime
from threading import Thread, Event

# Third-party
import paho.mqtt.client as mqtt
import requests
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# Local imports last
# from .local_module import LocalClass
```

---

## Key Algorithms

### Dynamic Read Interval

The ESP32 automatically adjusts its read interval based on communication success:

```cpp
float calculateNextInterval() {
  // Round up to next 5 second multiple based on average read time
  float base = ceil(inverter.read_time_mean / 5.0) * 5.0;
  
  // Clamp between 5 and 30 seconds
  return constrain(base, 5.0, 30.0);
}
```

- On repeated failures (3+), resets to initial interval (5 seconds)
- On success, gradually increases interval up to 30 seconds

### Battery Autonomy Calculation

Uses **Exponentially Weighted Moving Average (EWMA)** for accurate runtime estimation:

```cpp
void calculateAutonomy() {
  // Calculate dynamic alpha for 5-minute window
  float alpha = 2.0 / ((60.0 / dynamic_read_interval) * 5.0 + 1.0);
  
  // Cap efficiency at 93%
  float efficiency = min(inverter.eff_w, 93.0);
  
  // EWMA for efficiency and power
  autonomy_efficiency_ewma = alpha * efficiency + (1 - alpha) * autonomy_efficiency_ewma;
  autonomy_watts_ewma = alpha * ac.output_watts + (1 - alpha) * autonomy_watts_ewma;
  
  // Calculate DC watts accounting for efficiency
  float dc_watts = autonomy_watts_ewma / (autonomy_efficiency_ewma / 100.0);
  
  // Calculate remaining time
  float hours = inverter.battery_energy / dc_watts;
  inverter.autonomy = min(hours * 60, AUTONOMY_MAX_DAYS * 24 * 60);
}
```

### Energy Tracking

Energy values are:
- Accumulated using trapezoidal integration
- Persisted to ESP32 Preferences (NVS) when thresholds exceeded
- Restored on startup from Preferences
- Reset to zero at night (after 6 hours of zero PV voltage)

---

## Linting and Code Quality

### C++ Code Quality
```bash
# Check code style with clang-format
clang-format -i src/*.cpp src/*.h include/*.h

# Static analysis with cppcheck
cppcheck --enable=all --std=c++11 --language=c++ src/ include/

# Arduino compilation check
arduino-cli compile --fqbn esp32:esp32:esp32
```

### Python Code Quality
```bash
# Check style with flake8
flake8 mqtt_json_2_influx.py

# Auto-format with black
black mqtt_json_2_influx.py

# Check imports
isort --check-only --diff mqtt_json_2_influx.py

# Type checking
mypy mqtt_json_2_influx.py

# All checks combined
flake8 mqtt_json_2_influx.py && black --check mqtt_json_2_influx.py && isort --check-only mqtt_json_2_influx.py && mypy mqtt_json_2_influx.py
```

---

## Dependencies

### PlatformIO Dependencies

Declared in `platformio.ini`:
- ModbusMaster - Modbus RTU communication
- ArduinoJson - JSON serialization
- AsyncTCP - Async TCP for ESP32
- ESPAsyncWebServer - Async web server
- WebSerial - Serial over web

```bash
# Install/update PlatformIO
pip install -U platformio

# Install project dependencies
pio lib install
```

### Python Dependencies

```bash
# Runtime dependencies
pip install paho-mqtt influxdb-client requests

# Development dependencies
pip install flake8 black isort mypy pytest pytest-cov
```

---

## Architecture Notes

### System Overview

```
┌─────────────────┐     RS232      ┌─────────────────┐
│  PowMr Inverter │◄──────────────►│     ESP32       │
│                 │   Modbus RTU   │  (Dongle)       │
└─────────────────┘                └────────┬────────┘
                                            │
                              ┌─────────────┴─────────────┐
                              │                           │
                       ┌──────▼──────┐            ┌──────▼──────┐
                       │   MQTT      │            │    HTTP     │
                       │   Broker    │            │   Server    │
                       └──────┬──────┘            └─────────────┘
                              │                          
                       ┌──────▼──────────────────────┐
                       │   Python Bridge             │
                       │   (mqtt_json_2_influx.py)   │
                       └──────┬──────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
       ┌──────▼──────┐ ┌──────▼──────┐ ┌──────▼──────┐
       │  InfluxDB   │ │  InfluxDB   │ │             │
       │  (Local)    │ │  (Cloud)    │ │  Dashboard  │
       └─────────────┘ └─────────────┘ └─────────────┘
```

### Data Flow

1. **ESP32 collects data** from inverter via Modbus RTU (registers 4501-4561)
2. **ESP32 serves data** via HTTP API (`/api/status`) and optional MQTT
3. **Python Bridge receives data** via MQTT subscription or HTTP polling
4. **Python Bridge writes to InfluxDB** with change detection and heartbeat

### ESP32 Components

- **WiFi Management**: Connects to network, falls back to AP mode on failure
- **Modbus Communication**: Interfaces with PowMr inverter via RS232
- **Web Server**: Provides HTTP API and SPIFFS-hosted web interface
- **MQTT Client**: Publishes data to broker (optional)
- **OTA Updates**: Over-the-air firmware updates via ArduinoOTA
- **Preferences**: Persistent storage for energy data

### Python Bridge Components

- **DataBridge**: Main application class
- **MQTT Handler**: Subscribes to inverter topics
- **HTTP Poller**: Polls `/api/status` endpoint
- **InfluxDB Writer**: Writes data with change detection
- **Heartbeat Thread**: Maintains data continuity
- **Cloud Sync Thread**: Syncs to cloud backup every 30 minutes

---

## Heartbeat and Change Detection

### Python Bridge Logic

1. **Change Detection**: Only writes to InfluxDB when values change
2. **Initialization**: On first run, stores all non-zero values without writing
3. **Zero Handling**: Zero values are sent only once (not repeatedly in heartbeat)
4. **Stale Data**: Only heartbeats values successfully read within last 2 minutes

```python
# Only heartbeat if:
# 1. Value was read within last 2 minutes (not stale)
# 2. More than heartbeat_interval since last write
# 3. Value is not zero
if (now - last_read < 120 and 
    now - last_write >= heartbeat_interval and
    value != 0):
    write_heartbeat()
```

### Cloud Synchronization

- Runs every 30 minutes in background thread
- Compares local and cloud data for last 6 hours
- Automatically syncs missing data points
- Cloud write failures are logged but don't stop operation
- Designed for unreliable internet connections

---

## Error Handling

### ESP32 Error Handling

```cpp
// Modbus read with retry logic
uint8_t readRegistersChunked(uint16_t startAddr, uint16_t totalRegs, uint16_t *data) {
  uint16_t chunks = (totalRegs + CHUNK_SIZE - 1) / CHUNK_SIZE;
  
  for (uint16_t chunk = 0; chunk < chunks; chunk++) {
    uint8_t attempts = 0;
    uint8_t success = 0;
    
    while (attempts <= RETRY_COUNT && !success) {
      uint8_t result = node.readHoldingRegisters(currentAddr, regsToRead);
      if (result == node.ku8MBSuccess) {
        success = 1;
      } else {
        attempts++;
        delayMicroseconds(CHUNK_DELAY_US);
      }
    }
    
    if (!success) {
      sprint("Failed to read chunk after attempts");
      return 0;
    }
  }
  return 1;
}
```

### Python Error Handling

```python
try:
    result = risky_operation()
except SpecificError as e:
    logger.error(f"Operation failed: {e}")
    # Handle gracefully
except Exception as e:
    logger.error(f"Unexpected error: {e}")
    raise  # Re-raise if unexpected
```

---

## Testing

### Unit Test Structure

```python
def test_check_and_write_new_value(self):
    """Test that new values are written to InfluxDB"""
    self.bridge.initialized = True
    self.bridge.current_values = {}
    
    self.bridge._check_and_write('inverter', 'temp', 25.0)
    
    # Should have written to InfluxDB
    self.mock_write_api.write.assert_called_once()

def test_check_and_write_unchanged_value(self):
    """Test that unchanged values are NOT written"""
    self.bridge.initialized = True
    self.bridge.current_values = {'inverter.temp': 25.0}
    
    self.bridge._check_and_write('inverter', 'temp', 25.0)
    
    # Should NOT have written to InfluxDB
    self.mock_write_api.write.assert_not_called()
```

---

## Security Considerations

- **Never commit secrets**: WiFi credentials, API tokens, and passwords must never be committed to version control
- **Use `.gitignore`**: Ensure `src/wifi.h` and any config files with secrets are ignored
- **Secure defaults**: Use TLS for MQTT and HTTPS for InfluxDB when possible
- **Input validation**: Validate all data received from MQTT/HTTP before processing
- **Minimal logging**: Don't log sensitive information (passwords, tokens)

---

## Troubleshooting

### Common Issues

1. **ESP32 won't connect to WiFi**
   - Check `src/wifi.h` credentials are correct
   - Verify WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
   - Check antenna connection

2. **No Modbus data**
   - Verify RS232 connections (RX>TX, TX>RX)
   - Verify inverter is powered on
   - Try different baud rate (9600 is standard for PowMr)

3. **Invalid data (valid_info = 0)**
   - This is normal on first boot before first successful read
   - Check Modbus communication with serial monitor
   - Verify retry count and timing

4. **Python bridge not receiving data**
   - Check MQTT topics match ESP32 published topics
   - Verify HTTP URL is correct
   - Check firewall/network connectivity

5. **InfluxDB write failures**
   - Verify token has write permissions
   - Check bucket name and organization
   - Check network connectivity to InfluxDB server

### Debug Flags

In `src/config.h`, enable debug output:

```cpp
#define WEBSERIAL 1      // Enable WebSerial debug
#define VERBOSE_SERIAL 1 // Enable verbose logging
// #define DEBUG_AC 1     // Debug AC data
// #define DEBUG_DC 1     // Debug DC data
// #define DEBUG_INVERTER 1 // Debug inverter data
```

---

## Git Workflow

1. **Create feature branch**: `git checkout -b feature/your-feature`
2. **Make changes**: Implement your feature or fix
3. **Test locally**: Build and verify functionality
4. **Commit with descriptive message**: `git commit -m "Add feature description"`
5. **Push branch**: `git push origin feature/your-feature`
6. **Create PR**: Open pull request for review

### Commit Message Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

Example:
```
feat(modbust): add support for register 4560

Add reading of new register for battery temperature.
Update register map documentation.

Closes #123
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

---

## Version Information

- **Firmware Version**: Defined in `src/config.h` as `VERSION`
- **PlatformIO**: Uses `~/.platformio` installation
- **Python**: Requires Python 3.7+

---

## Additional Resources

- [PlatformIO Documentation](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [ModbusMaster Library](https://github.com/4-20ma/ModbusMaster)
- [InfluxDB Python Client](https://github.com/influxdata/influxdb-client-python)
- [PowMr Inverter Documentation](https://github.com/leodesigner/powmr_comm)

