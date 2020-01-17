What this does
--------------

Connect to Azure via MQTT and watch the device twins.
If someone put a "test" object like the following in the device twins,

	"desired": {
		"test": {
			"version": "6",
			"url": "https://www.midokura.com/wp-content/themes/midokura2k14/images/hw_logo.png",
			"sha256": "0875928fb0c8b838d69e1f8db7fa11cfe4b27a946477a35b6022f8b6d9db5e74"
		}
	}

This program downloads the blob from the given url and
verify sha256 hash of it.
And then report the success by updating the "reported" property.

	"reported": {
		"test": {
			 "version": "6",
			 "status": "ok"
		}
	}

Or, an error like the following.

	"reported": {
		"test": {
			 "version": "5",
			 "status": "hash mismatch"
		}
	}

Environment variables
---------------------

This program consumes the following variables.

* MQTT_HOST

  xxxx.azure-devices.net

* MQTT_PORT

  usually 8883

* MQTT_CAFILE

  path to the pem file for MQTT TLS

* DEVICEID, USERNAME

  See https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#using-the-mqtt-protocol-directly-as-a-device

* PASSWORD

  a SAS token

  See https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#using-the-mqtt-protocol-directly-as-a-device
  and https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-security#security-tokens

* MQTT_CLIENT_CERT_FILE, MQTT_CLIENT_KEY_FILE

  client cert and key files

Dependencies
------------

* Mosquitto
* libcurl
* OpenSSL
* https://github.com/kgabis/parson (This repo has a copy)
* BSD queue.h (This repo has a copy)

In case of ubuntu/bionic:

	apt-get install -y libmosquitto-dev libcurl4-openssl-dev libssl-dev

