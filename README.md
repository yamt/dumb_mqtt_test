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
