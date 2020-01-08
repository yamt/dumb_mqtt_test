#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mosquitto.h>

static void
on_connect(struct mosquitto *m, void *v, int rc) {
	printf("on_connect, rc=%d (%s)\n", rc, mosquitto_strerror(rc));
}

static void
on_disconnect(struct mosquitto *m, void *v, int rc) {
	printf("on_disconnect, rc=%d (%s)\n", rc, mosquitto_strerror(rc));
}

static void
on_publish(struct mosquitto *m, void *v, int mid) {
	printf("on_publish, mid=%d\n", mid);
}

static void
on_subscribe(struct mosquitto *m, void *v, int mid, int qos_count,
		const int *granted_qos) {
	printf("on_subscribe, mid=%d, qos_count=%d\n", mid, qos_count);
}

static void
on_message(struct mosquitto *m, void *v, const struct mosquitto_message *msg) {
	printf("on_message, topic='%s', qos=%d, payload='%.*s'\n", msg->topic, msg->qos, msg->payloadlen, msg->payload);

	// GET response
	// on_message, topic='$iothub/twin/res/200/?$rid=hey', qos=0, payload='{"desired":{"myUselessProperty":"Happy New Year 2020!","$version":15},"reported":{"uselessReportedValue":1034,"$version":89}}'
	//
	// device twins update notification
	// on_message, topic='$iothub/twin/PATCH/properties/desired/?$version=15', qos=0, payload='{"myUselessProperty":"Happy New Year 2020!","$version":15}'
	//
	// PATCH response
	// on_message, topic='$iothub/twin/res/204/?$rid=ho&$version=88', qos=0, payload=''
	//
	// "Message to device"
	// on_message, topic='devices/MySmartDevice/messages/devicebound/%24.to=%2Fdevices%2FMySmartDevice%2Fmessages%2FdeviceBound&foo=bar', qos=1, payload='hello'
}

const char *
xgetenv(const char *name) {
	const char *v = getenv(name);
	if (v == NULL) {
		errx(1, "%s is not set", name);
	}
	return v;
}

int
xgetenv_int(const char *name) {
	return atoi(xgetenv(name));
}

static unsigned long long
get_request_id(void) {
	static unsigned long long request_id = 10000;

	return request_id++;
}

static void
periodic_report(struct mosquitto *m) {
	char topic[1024]; // XXX
	char payload[1024]; // XXX
	ssize_t payloadlen;
	int mid;
	int rc;

	static time_t last_report;
	static int useless_value = 1000;

	if (!last_report) {
		last_report = time(NULL);
	}
	time_t now = time(NULL);
	if (now - last_report < 5) {
		return;
	}
	last_report = now;

	unsigned long long request_id = get_request_id();
	// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#update-device-twins-reported-properties
	snprintf(topic, sizeof(topic), "$iothub/twin/PATCH/properties/reported/?$rid=%llu", request_id);
	// XXX check snprintf failure
	payloadlen = snprintf(payload, sizeof(payload), "{ \"uselessReportedValue\": %d }", useless_value++);
	// XXX check snprintf failure
	printf("report topic=%s, payload=%s\n", topic, payload);
	rc = mosquitto_publish(m, &mid, topic, payloadlen, payload, 0, false);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_publish failed");
	}
	printf("(report) mosquitto_publish mid=%d\n", mid);
}

int
main(int argc, char **argv) {
	// XXX should not hardcode
	const char *host = xgetenv("MQTT_HOST");
	const int port = xgetenv_int("MQTT_PORT");
	const char *cafile = xgetenv("MQTT_CAFILE");
	const char *deviceid = xgetenv("DEVICEID");
	const char *username = xgetenv("USERNAME");
	const char *password = xgetenv("PASSWORD");
	const char *sub = xgetenv("SUB");

	struct mosquitto *m;
	int rc;

	mosquitto_lib_init();
	m = mosquitto_new(deviceid, false, NULL);
	if (m == NULL) {
		err(1, "mosquitto_new failed");
	}
	rc = mosquitto_tls_set(m, cafile, NULL, NULL, NULL, NULL);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_tls_set failed");
	}
	rc = mosquitto_username_pw_set(m, username, password);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_username_pw_set failed");
	}
	rc = mosquitto_int_option(m, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V311);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_int_option MOSQ_OPT_PROTOCOL_VERSION failed");
	}
	mosquitto_connect_callback_set(m, on_connect);
	mosquitto_disconnect_callback_set(m, on_disconnect);
	mosquitto_publish_callback_set(m, on_publish);
	mosquitto_subscribe_callback_set(m, on_subscribe);
	mosquitto_message_callback_set(m, on_message);

	rc = mosquitto_connect(m, host, port, 30);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_connect failed");
	}
	int mid;
	int qos = 1;
	rc = mosquitto_subscribe(m, &mid, sub, qos);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_subscribe failed");
	}
	printf("mosquitto_subscribe mid=%d\n", mid);

	// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#retrieving-a-device-twins-properties
	unsigned long long request_id = get_request_id();
	char topic[1024]; // XXX
	snprintf(topic, sizeof(topic), "$iothub/twin/GET/?$rid=%llu", request_id);
	// XXX check snprintf failure
	rc = mosquitto_publish(m, &mid, topic, 2, "{}", 1, false);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_publish failed");
	}
	printf("mosquitto_publish mid=%d\n", mid);

	for (;;) {
		rc = mosquitto_loop(m, -1, 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			errx(1, "mosquitto_loop rc=%d (%s)\n", rc, mosquitto_strerror(rc));
		}
		periodic_report(m);
	}

	errx(1, "should not reach here");
}
