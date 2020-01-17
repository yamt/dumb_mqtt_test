#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mosquitto.h>
#include <openssl/sha.h>
#include "parson.h"

#include "req.h"
#include "fetcher.h"

#define	REPORT_INTERVAL	10	// in seconds

struct global {
	JSON_Value *desired;
	JSON_Value *reported;
	JSON_Value *current;
} global;

static void
dump_global()
{
	if (global.desired) {
		char *p = json_serialize_to_string_pretty(global.desired);
		printf("DESIRED: %s\n", p);
		json_free_serialized_string(p);
	}
	if (global.current) {
		char *p = json_serialize_to_string_pretty(global.current);
		printf("CURRENT: %s\n", p);
		json_free_serialized_string(p);
	}
}

static void
on_connect(struct mosquitto *m, void *v, int rc)
{
	printf("on_connect, rc=%d (%s)\n", rc, mosquitto_strerror(rc));
}

static void
on_disconnect(struct mosquitto *m, void *v, int rc)
{
	printf("on_disconnect, rc=%d (%s)\n", rc, mosquitto_strerror(rc));
}

static void
on_publish(struct mosquitto *m, void *v, int mid)
{
	printf("on_publish, mid=%d\n", mid);
}

static void
on_subscribe(struct mosquitto *m, void *v, int mid, int qos_count,
    const int *granted_qos)
{
	printf("on_subscribe, mid=%d, qos_count=%d\n", mid, qos_count);
}

static int
parse_response_topic(const char *topic, int *statusp, request_id_t * reqidp)
{
	// $iothub/twin/res/200/?$rid=request_id

	// XXX maybe shouldn't use sscanf
	int status;
	unsigned long long reqid;
	int ret;
	ret = sscanf(topic, "$iothub/twin/res/%d/?$rid=%llu", &status, &reqid);
	if (ret != 2) {
		return 1;
	}
	*statusp = status;
	*reqidp = reqid;
	return 0;
}

static int
parse_patch_topic(const char *topic, unsigned int *versionp)
{
	// on_message, topic='$iothub/twin/PATCH/properties/desired/?$version=15', qos=0, payload='{"myUselessProperty":"Happy New Year 2020!","$version":15}'

	// XXX maybe shouldn't use sscanf
	unsigned int version;
	int ret;
	ret =
	    sscanf(topic, "$iothub/twin/PATCH/properties/desired/?$version=%u",
	    &version);
	if (ret != 1) {
		return 1;
	}
	*versionp = version;
	return 0;
}

static int
parse_patch_payload(const char *payload0, size_t payloadlen)
{
	// on_message, topic='$iothub/twin/PATCH/properties/desired/?$version=15', qos=0, payload='{"myUselessProperty":"Happy New Year 2020!","$version":15}'

	const char *payload;
	JSON_Value *root;

	if (global.desired == NULL) {
		printf("Ignoring a PATCH before the initial GET response\n");
		return 0;
	}

	payload = strndup(payload0, payloadlen);
	root = json_parse_string(payload);
	free((void *) payload);	// discard const
	JSON_Object *rootobj = json_value_get_object(root);
	if (rootobj == NULL) {
		goto bail;
	}

	JSON_Object *curobj = json_value_get_object(global.desired);
	double curversion = json_object_get_number(curobj, "$version");
	double version = json_object_get_number(rootobj, "$version");
	if (version > curversion) {
		// apply patch
		size_t sz = json_object_get_count(rootobj);
		unsigned int i;
		for (i = 0; i < sz; i++) {
			const char *name = json_object_get_name(rootobj, i);
#if 0
			if (name[0] == '$') {	// skip $version, $metadata, etc
				continue;
			}
#endif
			JSON_Value *value =
			    json_object_get_value_at(rootobj, i);
#if 1
			char *p = json_serialize_to_string_pretty(value);
			printf("JSON %s=%s\n", name, p);
			json_free_serialized_string(p);
#endif
			if (json_value_get_type(value) == JSONNull) {
				json_object_remove(curobj, name);
			} else {
				JSON_Value *newvalue =
				    json_value_deep_copy(value);
				if (newvalue == NULL) {
					// XXX leaving partial update
					goto bail;
				}
				json_object_set_value(curobj, name, newvalue);
			}
		}
		dump_global();
	} else {
		printf("ignoring a stale update\n");
	}

	json_value_free(root);
	return 0;

bail:
	json_value_free(root);
	return 1;
}

static void
on_message(struct mosquitto *m, void *v, const struct mosquitto_message *msg)
{
	printf("on_message, topic='%s', qos=%d, payload='%.*s'\n", msg->topic,
	    msg->qos, msg->payloadlen, (const char *) msg->payload);

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

	int status;
	request_id_t id;
	if (!parse_response_topic(msg->topic, &status, &id)) {
		struct request *req = request_remove(id);
		if (req == NULL) {
			errx(1, "unknown request id %llu\n", id);
		} else {
			printf
			    ("got a response for request id %llu, status %d\n",
			    id, status);
			if (status / 100 * 100 != 200) {
				errx(1, "unexpected status %d\n", status);
			}
			if (req->callback) {
				char *p0 =
				    strndup(msg->payload, msg->payloadlen);
				req->callback(req->id, req->callback_data, p0);
				free(p0);
			}
			request_free(req);
		}
		return;
	}

	unsigned int version;	// XXX is int wide enough?
	if (!parse_patch_topic(msg->topic, &version)) {
		printf("got an update notification\n");
		parse_patch_payload(msg->payload, msg->payloadlen);
		return;
	}

	printf("unknown topic\n");
}

static void
on_log(struct mosquitto *m, void *v, int level, const char *msg)
{
	printf("on_log, level=%d, msg=%s\n", level, msg);
}

const char *
xgetenv(const char *name)
{
	const char *v = getenv(name);
	if (v == NULL) {
		errx(1, "%s is not set", name);
	}
	return v;
}

int
xgetenv_int(const char *name)
{
	// XXX better to use strtoul and check errors
	return atoi(xgetenv(name));
}

static void
periodic_report(struct mosquitto *m)
{
	static time_t last_report;

	if (!last_report) {
		last_report = time(NULL);
	}
	time_t now = time(NULL);
	if (now - last_report < REPORT_INTERVAL) {
		return;
	}
	last_report = now;

	// XXX cancel the previous report if it hasn't been acked yet

	// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#update-device-twins-reported-properties
	struct request *req = request_alloc();
	req->topic_template = "$iothub/twin/PATCH/properties/reported/?$rid=%llu";
	char *payload = json_serialize_to_string(global.current);
	req->payload = payload;
	req->payload_free = json_free_serialized_string;
	request_insert(req);
}

static void
get_done(request_id_t id, void *_unused, void *payload)
{
	// on_message, topic='$iothub/twin/res/200/?$rid=hey', qos=0, payload='{"desired":{"myUselessProperty":"Happy New Year 2020!","$version":15},"reported":{"uselessReportedValue":1034,"$version":89}}'

	JSON_Value *root = json_parse_string(payload);
	if (root == NULL) {
		goto bail;
	}
	JSON_Object *rootobj = json_value_get_object(root);
	if (rootobj == NULL) {
		goto bail;
	}
	JSON_Value *desired = json_object_get_value(rootobj, "desired");
	JSON_Object *desiredobj = json_value_get_object(desired);
	if (desiredobj == NULL) {
		goto bail;
	}
	JSON_Value *reported = json_object_get_value(rootobj, "reported");
	JSON_Object *reportedobj = json_value_get_object(reported);
	if (reportedobj == NULL) {
		goto bail;
	}
	desired = json_value_deep_copy(desired);
	reported = json_value_deep_copy(reported);
	if (desired && reported) {
		json_value_free(global.desired);
		global.desired = desired;
		json_value_free(global.reported);
		global.reported = reported;
	} else {
		json_value_free(desired);
		json_value_free(reported);
	}
	json_value_free(root);
	dump_global();
	return;

bail:
	errx(1, "unexpected json: %s", (const char *) payload);
}

static void
fill_obj(JSON_Value * value, const char *version, const char *status)
{
	const char *verkey = "version";
	const char *statuskey = "status";

	JSON_Object *obj = json_value_get_object(value);
	json_object_set_string(obj, verkey, version);
	json_object_set_string(obj, statuskey, status);
}

static void
reconcile()
{
	/*
	 *      {
	 *              "test": {
	 *                      "version": "1",
	 *                      "url": "https://www.midokura.com/wp-content/themes/midokura2k14/images/hw_logo.png",
	 *                      "sha256": "0875928fb0c8b838d69e1f8db7fa11cfe4b27a946477a35b6022f8b6d9db5e74"
	 *              }
	 *      }
	 */
	const char *key = "test";
	const char *verkey = "version";
	const char *urlkey = "url";
	const char *sha256key = "sha256";

	JSON_Object *desiredobj = json_value_get_object(global.desired);
	JSON_Object *currentobj = json_value_get_object(global.current);
	JSON_Object *desired = json_object_get_object(desiredobj, key);
	JSON_Object *current = json_object_get_object(currentobj, key);

	const char *ver = json_object_get_string(desired, verkey);
	const char *curver = json_object_get_string(current, verkey);

	char *buf = NULL;
	FILE *fp = NULL;

#if 0
	if (desired == NULL) {
		return;
	}
	char *p =
	    json_serialize_to_string_pretty(json_object_get_wrapping_value
	    (desired));
	printf("reconcile to: %s\n", p);
	json_free_serialized_string(p);
#endif

	if (ver == NULL) {
		return;
	}
	if (curver != NULL && !strcmp(ver, curver)) {
		return;
	}

	const char *url = json_object_get_string(desired, urlkey);
	const char *sha256 = json_object_get_string(desired, sha256key);
	if (url == NULL || sha256 == NULL) {
		return;
	}

	printf("url: %s\n", url);
	printf("sha256: %s\n", sha256);

	// XXX the following process should be non-blocking
	JSON_Value *new;
	if (current == NULL) {
		new = json_value_init_object();
	} else {
		new =
		    json_value_deep_copy(json_object_get_wrapping_value
		    (current));
	}
	if (new == NULL) {
		err(1, "malloc");
	}
	size_t size;
	fp = open_memstream(&buf, &size);
	if (fp == NULL) {
		errx(1, "open_memstream");
	}
	if (fetch(url, NULL, fp)) {
		warnx("fetch failure");
		fill_obj(new, NULL, "fetch failed");
		goto report;
	}
	unsigned char hash[32];
	char hashstr[32 * 2 + 1];
	fclose(fp);
	fp = NULL;
	printf("downloaded %zu bytes successfully\n", size);
	SHA256_CTX c;
	SHA256_Init(&c);
	SHA256_Update(&c, buf, size);
	SHA256_Final(hash, &c);

	int i;
	for (i = 0; i < sizeof(hash); i++) {
		sprintf(hashstr + i * 2, "%02x", hash[i]);
	}
	printf("computed hash: %s\n", hashstr);
	if (strcmp(hashstr, sha256)) {
		printf("hash mismatch: %s != %s\n", hashstr, sha256);
		fill_obj(new, NULL, "hash mismatch");
		goto report;
	}

	printf("hash is ok\n");

	// XXX do something meaningful with the downloaded blob

	// report success
	fill_obj(new, ver, "ok");
report:
	if (fp) {
		fclose(fp);
	}
	if (buf) {
		free(buf);
	}
	json_object_set_value(json_value_get_object(global.current), key, new);
	dump_global();
}

void
init_global()
{
	global.current = json_value_init_object();
	if (global.current == NULL) {
		errx(1, "json_value_init_object");
	}

	const char *key = "test";
	const char *version = "initial dummy version";	// XXX
	JSON_Value *new = json_value_init_object();
	fill_obj(new, version, "unknown");
	json_object_set_value(json_value_get_object(global.current), key, new);
}

int
main(int argc, char **argv)
{
	// XXX should not hardcode
	const char *host = xgetenv("MQTT_HOST");
	const int port = xgetenv_int("MQTT_PORT");
	const char *cafile = xgetenv("MQTT_CAFILE");
	const char *deviceid = xgetenv("DEVICEID");
	const char *username = xgetenv("USERNAME");
	const char *password = getenv("PASSWORD");
	const char *client_cert_file = getenv("MQTT_CLIENT_CERT_FILE");
	const char *client_key_file = NULL;

	if (client_cert_file != NULL) {
		client_key_file = xgetenv("MQTT_CLIENT_KEY_FILE");
	}

	init_global();

	struct mosquitto *m;
	int rc;

	mosquitto_lib_init();
	m = mosquitto_new(deviceid, false, NULL);
	if (m == NULL) {
		err(1, "mosquitto_new failed");
	}
	rc = mosquitto_tls_set(m, cafile, NULL, client_cert_file,
	    client_key_file, NULL);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_tls_set failed");
	}
	rc = mosquitto_username_pw_set(m, username, password);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_username_pw_set failed");
	}
#if 0				// use mosquitto_opts_set for now as mosquitto_int_option is too new
	rc = mosquitto_int_option(m, MOSQ_OPT_PROTOCOL_VERSION,
	    MQTT_PROTOCOL_V311);
#else
	int v = MQTT_PROTOCOL_V311;
	rc = mosquitto_opts_set(m, MOSQ_OPT_PROTOCOL_VERSION, &v);
#endif
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1,
		    "mosquitto_int_option MOSQ_OPT_PROTOCOL_VERSION failed");
	}
	mosquitto_connect_callback_set(m, on_connect);
	mosquitto_disconnect_callback_set(m, on_disconnect);
	mosquitto_publish_callback_set(m, on_publish);
	mosquitto_subscribe_callback_set(m, on_subscribe);
	mosquitto_message_callback_set(m, on_message);
	mosquitto_log_callback_set(m, on_log);

	// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-device-twins#device-reconnection-flow
	rc = mosquitto_connect(m, host, port, 30);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_connect failed");
	}
	int mid;
	int qos = 1;
	rc = mosquitto_subscribe(m, &mid,
	    "$iothub/twin/PATCH/properties/desired/#", qos);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_subscribe failed");
	}
	rc = mosquitto_subscribe(m, &mid, "$iothub/twin/res/#", qos);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_subscribe failed");
	}
	printf("mosquitto_subscribe mid=%d\n", mid);

	// XXX should we wait for SUBACKs before sending the following GET request?
	// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-mqtt-support#retrieving-a-device-twins-properties
	struct request *req = request_alloc();
	req->topic_template = "$iothub/twin/GET/?$rid=%llu";
	req->callback = get_done;
	request_insert(req);

	for (;;) {
		rc = mosquitto_loop(m, -1, 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			errx(1, "mosquitto_loop rc=%d (%s)\n", rc,
			    mosquitto_strerror(rc));
		}
		reconcile();
		periodic_report(m);
		resend_requests(m);
	}

	errx(1, "should not reach here");
}
