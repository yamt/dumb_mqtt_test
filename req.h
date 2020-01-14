
#include "queue.h"

typedef unsigned long long request_id_t;

request_id_t request_id_alloc();

struct request {
	TAILQ_ENTRY(request)	q;
	request_id_t			id;
};

struct request *request_alloc();
void request_free(struct request *);
void request_insert(struct request *);
struct request *request_remove(request_id_t);
