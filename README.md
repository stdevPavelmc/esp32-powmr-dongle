# PowMr POW-LVM3.6M-24V ESP32 based dongle

Based on the work of [Leo Designer](https://github.com/leodesigner/powmr_comm)

## Features

This code will do:
- Connect to your WiFi network (see [WiFi](#wifi) section)
- Connect to a influxdb cloud instane with indlux v2.x protocol (see [InfluxDB](#influxdb) section)
- Make available a `/api/status` json endpoint in json format to consume and use on the index.html page (see [Json](#json) sections)

## WiFi

You need to create a file with this content (adjust to your credentials on src/wifi.h):

```cpp
/****** wifi config *******************/
// client wifi settings
const char *c_ssid = "TP-LINK_D3A2D4";
const char *c_password = "175mkHR1yMmFpEuzhCIm81TO";

// server if no client settings
const char *s_ssid = "ESP32-PowMr";
const char *s_password = "f77kh3r8sji24";

// hostname either way
const char *hostname = "ESP32-PowMr";

```

You can see there is a server section, that is a hostpot, if the configured network is not available or got down it will start the hostspot with this credentials, and if the network came back it will re-connect to it, ditching the hostpot.

The default IP on hostpot mode is "192.168.4.1" and there it will register a mDNS http server as 'hostname'.local, aka: `ESP32-PowMr.local` by default.

## InfluxDB

You need to create a file with this content (adjust to your credentials on src/mqtt.h):

```cpp
/****** MTWW server config *******************/
// Server settings
const char* mqtt_server = "192.168.1.102";
const int mqtt_server_port = 1883;

// MQTT topics
// const char* publishTopic = "mtww";
char* subscribeTopic = "/dummy";

```

This assumes a local mqtt server with no auth; the "/dummy/" topic is that, a dummy one to send messages and test the pub-sub features.

You can disable the mqtt for good if you will don't use it by commenting the PUBSUB define on top of the main.cpp file, but you need to define the src/mqtt.h file of comment the #include statement for it.

# Json

The `/api/status` will produce a json like this:

```json
{
	"ac":{
		"input_freq":60.7,
		"input_voltage":122.4,
		"output_freq":60.7,
		"output_load_percent":9,
		"output_va":352,
		"output_voltage":121.5,
		"output_watts":341
	},
	"dc":{
		"batt_v_compensation_k":0.01,
		"charge_current":0,
		"charge_power":0,
		"discharge_current":0,
		"discharge_power":0,
		"new_k":0,
		"pv_current":4.575311,
		"pv_voltage":88.3,
		"pv_power":404,
		"voltage":27.4,
		"voltage_corrected":27.4
	},
	"inverter":{
        "read_interval_ms":15000, 
		"charger":13,
		"charger_source_priority":1,
		"eff_va":87.12872,
		"eff_w":84.40594,
		"op_mode":4,
		"output_source_priority":1,
		"soc":100,
		"temp":44,
		"valid_info":1
	}
}
```

Please notice the `inverter.valid_info` variable, the data is actual and valis only if this parameter is `1`; and the `inverter.read_interval_ms` variable tha reflects the time between measuremenst (it's a #define on the file, read takes between 6-12 seconds with retries)

WARNING: This json data will change as this is a work in progress...
