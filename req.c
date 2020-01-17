
#include <stdlib.h>

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
	return req;
}

void
request_free(struct request *req)
{
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
