# AGENTS.md - Development Guidelines for PowMr ESP32 Inverter Monitor

This file contains development guidelines and commands for the PowMr ESP32-based inverter monitoring system programmed in Arduino as a Platformio project using VSCode IDE, there is also a MQTT/HTTP to InfluxDB data bridge in python code in the folder extras with some placeholders and docs

As a rule the default platfiormio environment is the one labeled OTA one (esp32_OTA), only use the USB labeled one when instructed or mentioned the usb connection to the board.

## Build/Lint/Test Commands

For platformio ran and command we must use the platformio installed env on the user folder rather than the installed on the system path, aka ~/.platformio folder

### PlatformIO (ESP32/Arduino) - Primary Development

```bash
# Build the project
pio run

# Build and upload to ESP32 via USB
pio run -t upload

# Build and upload via OTA (Over-The-Air)
pio run -t upload --environment esp32_OTA

# Monitor serial output
pio device monitor

# Clean build files
pio run -t clean

# Run all targets for default environment
pio run -t clean -t build -t upload -t monitor
```

**Note:** The project uses manual timing loops instead of SimpleTimer to ensure consistent execution intervals, preventing multiple consecutive calls during long operations like OTA updates.

### Python Bridge (Secondary) - Running the Application

```bash
# Run the bridge with default config
python mqtt_json_2_influx.py

# Run with custom config file
python mqtt_json_2_influx.py --config /path/to/config.ini
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
# Run all tests (if test files exist)
python -m pytest

# Run a specific test file
python -m pytest test_bridge.py

# Run a specific test function
python -m pytest test_bridge.py::test_mqtt_connection

# Run tests with coverage
python -m pytest --cov=mqtt_json_2_influx --cov-report=html
```

### Linting and Code Quality

#### Arduino/C++ Code Quality
```bash
# Check code style with clang-format (if configured)
clang-format -i src/*.cpp src/*.h include/*.h

# Static analysis with cppcheck
cppcheck --enable=all --std=c++11 --language=c++ src/ include/

# Arduino lint (if arduino-cli is installed)
arduino-cli compile --fqbn esp32:esp32:esp32
```

#### Python Code Quality
```bash
# Check code style with flake8
flake8 mqtt_json_2_influx.py

# Auto-format code with black
black mqtt_json_2_influx.py

# Check imports with isort
isort --check-only --diff mqtt_json_2_influx.py

# Auto-sort imports
isort mqtt_json_2_influx.py

# Type checking with mypy
mypy mqtt_json_2_influx.py

# Run all quality checks
flake8 mqtt_json_2_influx.py && black --check mqtt_json_2_influx.py && isort --check-only mqtt_json_2_influx.py && mypy mqtt_json_2_influx.py
```

### Dependencies

#### PlatformIO Dependencies
```bash
# Install/update PlatformIO
pip install -U platformio

# Install project dependencies (defined in platformio.ini)
pio lib install
```

#### Python Dependencies
```bash
# Install dependencies (create requirements.txt if needed)
pip install paho-mqtt influxdb-client requests

# Install development dependencies
pip install flake8 black isort mypy pytest pytest-cov
```

## Code Style Guidelines

### General Principles
- **Clarity over cleverness**: Code should be readable and maintainable
- **Fail fast**: Validate inputs early and handle errors gracefully
- **Single responsibility**: Each function/method should do one thing well
- **DRY (Don't Repeat Yourself)**: Avoid code duplication, reuse all functions and procedures possible for same operations
- **Explicit is better than implicit**: Be clear about what the code does

### Arduino/C++ Standards (Primary)
- Follow Arduino coding standards and ESP32 best practices
- Use consistent indentation (2-4 spaces, no tabs)
- Line length: 120 characters maximum
- Use descriptive variable and function names
- Add comments for complex logic and hardware interactions

### Naming Conventions (C++)
- **Classes**: PascalCase (e.g., `InverterMonitor`, `WifiManager`)
- **Functions/Methods**: camelCase (e.g., `readInverterData`, `setupWifi`)
- **Variables**: snake_case (e.g., `battery_voltage`, `last_read_time`)
- **Constants**: UPPER_SNAKE_CASE (e.g., `READ_INTERVAL_MS`)
- **Private members**: Prefix with underscore (e.g., `_mqttClient`)

### Python Standards (Secondary)
- Follow PEP 8 style guide
- Use 4 spaces for indentation (no tabs)
- Line length: 88 characters (Black default)
- Use descriptive variable and function names
- Add docstrings to all public functions and classes

### Python Imports
```python
# Standard library imports first
import sys
import time
from datetime import datetime
from threading import Thread, Event

# Third-party imports second
import paho.mqtt.client as mqtt
import requests
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# Local imports last (if any)
# from .local_module import LocalClass
```

- Group imports by type with blank lines between groups
- Use absolute imports
- Avoid wildcard imports (`from module import *`)
- Sort imports alphabetically within each group

### Python Naming Conventions
- **Classes**: PascalCase (e.g., `DataBridge`, `MqttClient`)
- **Functions/Methods**: snake_case (e.g., `check_and_write`, `setup_mqtt`)
- **Variables**: snake_case (e.g., `current_values`, `last_write_time`)
- **Constants**: UPPER_SNAKE_CASE (e.g., `HEARTBEAT_INTERVAL`)
- **Private attributes**: Prefix with underscore (e.g., `_mqtt_client`)

### Error Handling
- Use specific exception types, not bare `except:` when possible
- Log errors with appropriate levels (DEBUG, INFO, WARNING, ERROR)
- Provide meaningful error messages
- Clean up resources in finally blocks or context managers
- Don't suppress exceptions unless you have a good reason

```python
try:
    # Risky operation
    result = risky_function()
except SpecificError as e:
    logger.error(f"Operation failed: {e}")
    # Handle the error appropriately
except Exception as e:
    logger.error(f"Unexpected error: {e}")
    raise
```

### Logging
- Use the logging module, not print statements
- Use main two groups of logging units
  - The main program config reading, setup and configuration, etc.
  - The main goal of the program or running code
- Create a switch to enable/disable logging per groups, enabled by default on both.
- Set appropriate log levels
- Include relevant context in log messages
- Use f-strings for string formatting in logs

```python
logger.debug(f"Processing message: {topic} = {payload}")
logger.info(f"Connected to MQTT broker at {host}:{port}")
logger.error(f"Failed to write to InfluxDB: {e}")
```

### Type Hints
- Use type hints for function parameters and return values
- Import types from `typing` module when needed
- Use Union for multiple possible types
- Use Optional for nullable types

```python
from typing import Dict, Optional, Any

def process_data(self, data: Dict[str, Any]) -> Optional[float]:
    # Function implementation
    pass
```

### Threading and Concurrency
- Use threading.Lock() for shared mutable state
- Avoid global variables
- Handle thread interruption gracefully
- Use Event objects for coordination
- Document thread safety guarantees

### Configuration
- Use configparser for configuration files
- Provide sensible defaults
- Validate configuration on startup
- Document all configuration options

### Testing
- Write unit tests for all public functions
- Use descriptive test names
- Test both success and failure cases
- Mock external dependencies (MQTT, InfluxDB, HTTP requests)
- Aim for high test coverage (>80%)

```python
def test_check_and_write_new_value(self):
    # Test implementation
    pass

def test_check_and_write_unchanged_value(self):
    # Test implementation
    pass
```

### Documentation
- Add docstrings to all classes and public methods
- Use Google-style docstrings
- Document parameters, return values, and exceptions
- Keep README.md up to date

```python
def _check_and_write(self, measurement: str, field: str, value: float) -> None:
    """Check if value changed and write to InfluxDB if needed.

    Args:
        measurement: The measurement name (e.g., 'inverter')
        field: The field name (e.g., 'temperature')
        value: The numeric value to check and potentially write

    This method implements the core logic for determining when to write
    data points to InfluxDB, ensuring we don't write duplicate values
    unnecessarily while maintaining data continuity through heartbeats.
    """
```

### Security Considerations
- Never log sensitive information (passwords, tokens, keys)
- Validate all inputs from external sources
- Use secure defaults for network communications
- Handle authentication credentials securely
- Avoid exposing internal system information

### Performance
- Minimize InfluxDB writes (use change detection)
- Use efficient data structures (dicts for O(1) lookups)
- Avoid blocking operations in threads
- Profile performance-critical code
- Consider memory usage for long-running processes

### Code Organization
- Group related functionality into classes
- Keep methods small and focused
- Use private methods for internal implementation details
- Separate concerns (MQTT handling, HTTP polling, InfluxDB writing)
- Make the code testable by avoiding tight coupling

### Modbus Register Documentation
**IMPORTANT**: When modifying Modbus register reads in `src/main.cpp`, preserve and update register labels/comments. Each `mbusData[index]` read must have a comment above it indicating:
- Register number (4501 + index)
- Register description from `extras/registers-map.md`
- Register type (measurement/settings/binary_flags)

Example:
```cpp
// Register 4501: Output Source Priority (settings)
inverter.op_mode = (float)htons(mbusData[0]);
```

These labels ensure code maintainability and help correlate raw register values with their documented meanings. Always reference `extras/registers-map.md` for accurate register information.

### Autonomy Calculation
The battery autonomy and read time features use **Exponentially Weighted Moving Average (EWMA)** for calculating time-based averages:

- **Dynamic EWMA Alpha**: Calculated to cover `AUTONOMY_WINDOW_MINUTES = 5.0` minutes based on current read interval
- **Formula**: `alpha = 2 / (readings_in_5min_window + 1)` where `readings_in_5min_window = (300000 / dynamic_read_interval_ms)`
- **Efficiency Cap**: Maximum 93% efficiency limit
- **Benefits**: Adapts to different read intervals, continuous updating, no hard resets for both autonomy and read time calculations

### EWMA Functions
- `calculateEWMA()`: Generic EWMA calculation with initialization handling
- `calculateDynamicAlpha()`: Calculates dynamic alpha based on read interval for 5-minute window (used by both autonomy and read time)

### Git Workflow
- Use descriptive commit messages
- Write commits that are atomic and focused
- Use feature branches for new functionality
- Keep the main branch stable
- Add tests for new features

### Deployment
- Use environment variables for sensitive configuration
- Provide clear setup and run instructions
- Handle graceful shutdowns
- Monitor resource usage
- Plan for configuration management

---

## Architecture Notes

### System Overview
1. **ESP32 Inverter Monitor (Primary)**: Arduino-based firmware that interfaces with PowMr hybrid inverter via Modbus
2. **Data Bridge (Secondary)**: Python application that receives data via MQTT/HTTP and forwards to InfluxDB

### Data Flow
1. **ESP32 collects data** from inverter via Modbus protocol
2. **ESP32 publishes data** via MQTT or serves via HTTP API
3. **Python Bridge receives data** from MQTT broker or HTTP polling
4. **Python Bridge writes data** to InfluxDB with change detection and heartbeat

### ESP32 Components
- **WiFi Management**: Connects to network, fallback to AP mode
- **Modbus Communication**: Interfaces with PowMr inverter
- **Web Server**: Provides HTTP API and web interface
- **MQTT Client**: Publishes data to broker
- **OTA Updates**: Over-the-air firmware updates

### Python Bridge Components
- `DataBridge`: Main application class coordinating MQTT/HTTP input and InfluxDB output
- Uses threading for concurrent heartbeat and main processing
- Implements change detection to avoid unnecessary writes

### Configuration Sections
- `[source]`: type (mqtt/http)
- `[mqtt]`: host, port, username, password, topics
- `[http]`: url, poll_interval
- `[influxdb]`: url, token, org, bucket (primary/local server)
- `[influxdb_cloud]`: url, token, org, bucket (secondary/cloud server, optional)

### Heartbeat Logic
- Only sends values that were successfully read within last 2 minutes
- Respects the configured heartbeat interval
- Never sends zero values repeatedly (only once when they change)

### Cloud Synchronization
- Runs in background thread every 30 minutes
- Compares data between local and cloud servers for last 6 hours
- Automatically syncs missing data points to cloud
- Cloud write failures are logged but don't stop the bridge
- Designed for unreliable internet connections</content>
<parameter name="filePath">AGENTS.md