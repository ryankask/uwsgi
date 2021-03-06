#include <uwsgi.h>

extern struct uwsgi_server uwsgi;


static int uwsgi_rpc_request(struct wsgi_request *wsgi_req) {

	// this is the list of args
	char *argv[UMAX8];
	// this is the size of each argument
	uint16_t argvs[UMAX8];
	// maximum number of supported arguments
	uint8_t argc = 0xff;
	// response output
	char response_buf[UMAX16];

	/* Standard RPC request */
        if (!wsgi_req->uh->pktsize) {
                uwsgi_log("Empty RPC request. skip.\n");
                return -1;
        }

	if (uwsgi_parse_array(wsgi_req->buffer, wsgi_req->uh->pktsize, argv, argvs, &argc)) {
                uwsgi_log("Invalid RPC request. skip.\n");
                return -1;
	}

	// call the function (output will be in wsgi_req->buffer)
	wsgi_req->uh->pktsize = uwsgi_rpc(argv[0], argc-1, argv+1, argvs+1, response_buf);

	// using modifier2 we may want a raw output
	if (wsgi_req->uh->modifier2 == 0) {
		if (uwsgi_response_write_body_do(wsgi_req, (char *) wsgi_req->uh, 4)) {
			return -1;
		}
	}
	// write the response
	uwsgi_response_write_body_do(wsgi_req, response_buf, wsgi_req->uh->pktsize);
	
	return UWSGI_OK;
}

#ifdef UWSGI_ROUTING
static int uwsgi_routing_func_rpc(struct wsgi_request *wsgi_req, struct uwsgi_route *ur) {
	int ret = -1;
	// this is the list of args
        char *argv[UMAX8];
        // this is the size of each argument
        uint16_t argvs[UMAX8];
	// this is a placeholder for tmp uwsgi_buffers
	struct uwsgi_buffer *ubs[UMAX8];

	char **r_argv = (char **) ur->data2;
	uint16_t *r_argvs = (uint16_t *) ur->data3;

	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	uint64_t i;
	for(i=0;i<ur->custom;i++) {
		ubs[i] = uwsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, r_argv[i], r_argvs[i]);
		if (!ubs[i]) goto end;
		argv[i] = ubs[i]->buf;
		argvs[i] = ubs[i]->pos;
	}

	// ok we now need to check it it is a local call or a remote one
	char *func = uwsgi_str(ur->data);
	char *remote = NULL;
	char *at = strchr(func, '@');
	if (at) {
		*at = 0;
		remote = at+1;
	}
	uint16_t size;
	char *response = uwsgi_do_rpc(remote, func, ur->custom, argv, argvs, &size);
	free(func);
	if (!response) goto end;

	ret = UWSGI_ROUTE_BREAK;

	if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) {free(response) ; goto end;}
        if (uwsgi_response_add_content_length(wsgi_req, size)) {free(response) ; goto end;}
	uwsgi_response_write_body_do(wsgi_req, response, size);
	free(response);

end:
	for(i=0;i<ur->custom;i++) {
		if (ubs[i] != NULL) {
			uwsgi_buffer_destroy(ubs[i]);
		}
	}
	return ret;
}

/*
	ur->data = the func name
        ur->custom = the number of arguments
	ur->data2 = the pointer to the args
	ur->data3 = the pointer to the args sizes
*/
static int uwsgi_router_rpc(struct uwsgi_route *ur, char *args) {
        ur->func = uwsgi_routing_func_rpc;
	ur->custom = 0;
	ur->data2 = uwsgi_calloc(sizeof(char *) * UMAX8);
	ur->data3 = uwsgi_calloc(sizeof(uint16_t) * UMAX8);
	char *p = strtok(args, " ");
	while(p) {
		if (!ur->data) {
			ur->data = p;
		}
		else {
			if (ur->custom >= UMAX8) {
				uwsgi_log("unable to register route: maximum number of rpc args reached\n");
				free(ur->data2);
				free(ur->data3);
				return -1;
			}
			char **argv = (char **) ur->data2;
			uint16_t *argvs = (uint16_t *) ur->data3;
			argv[ur->custom] = p;
			argvs[ur->custom] = strlen(p);
			ur->custom++;	
		}
		p = strtok(NULL, " ");
	}

	if (!ur->data) {
		uwsgi_log("unable to register route: you need to specify an rpc function\n");
		free(ur->data2);
		free(ur->data3);
		return -1;
	}
	return 0;
}

static void router_rpc_register() {
        uwsgi_register_router("call", uwsgi_router_rpc);
        uwsgi_register_router("rpc", uwsgi_router_rpc);
}
#endif

struct uwsgi_plugin rpc_plugin = {

	.name = "rpc",
	.modifier1 = 173,
	
	.request = uwsgi_rpc_request,
#ifdef UWSGI_ROUTING
	.on_load = router_rpc_register,
#endif
};
