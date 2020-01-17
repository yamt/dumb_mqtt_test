
#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "req.h"

static
TAILQ_HEAD(, request)
    reqq = TAILQ_HEAD_INITIALIZER(reqq);

request_id_t
request_id_alloc()
{
	static request_id_t request_id = 10000;

	return request_id++;
}

struct request *
request_alloc()
{
	struct request *req = malloc(sizeof(*req));

	req->id = request_id_alloc();
	req->callback = NULL;
	req->callback_data = NULL;
	req->payload = "";
	req->payload_free = NULL;
	req->when = 0;
	return req;
}

void
request_free(struct request *req)
{
	if (req->payload_free) {
		req->payload_free((void *) req->payload);
	}
	free(req);
}

void
request_insert(struct request *req)
{
	TAILQ_INSERT_TAIL(&reqq, req, q);
}

struct request *
request_remove(request_id_t id)
{
	struct request *req;

	TAILQ_FOREACH(req, &reqq, q) {
		if (req->id == id) {
			TAILQ_REMOVE(&reqq, req, q);
			return req;
		}
	}
	return NULL;
}

#include <mosquitto.h>

static void
request_send(struct mosquitto *m, struct request *req)
{
	char topic[1024];	// XXX
	int mid;
	int rc;

	// XXX check snprintf failure
	snprintf(topic, sizeof(topic), req->topic_template, req->id);

	size_t payloadlen = strlen(req->payload);
	rc = mosquitto_publish(m, &mid, topic, payloadlen, req->payload, 0,
	    false);
	if (rc != MOSQ_ERR_SUCCESS) {
		errx(1, "mosquitto_publish failed");
	}
	printf("%s mid=%d, topic=%s, payload=%s\n",
	    (req->when == 0) ? "SEND" : "RESEND", mid, topic, req->payload);
}

void
resend_requests(struct mosquitto *m)
{
	struct request *req;
	time_t now = time(NULL);

	TAILQ_FOREACH(req, &reqq, q) {
		if (req->when == 0 || now - req->when > 5) {
			// XXX should renew req->id when resending?
			request_send(m, req);
			req->when = now;
		}
	}
}
