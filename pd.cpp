#include "include/verbs.h"
#include "include/log.h"

int ib_uverbs_alloc_pd(Router *ffr, void *rsp)
{
	LOG_TRACE("===ALLOC_PD===");
	//rsp = malloc(sizeof(struct IBV_ALLOC_PD_RSP));
	struct ibv_pd *pd = ibv_alloc_pd(ffr->rdma_data.ib_context); 
	if (pd->handle >= MAP_SIZE) {
		LOG_ERROR("PD handle is no less than MAX_QUEUE_MAP_SIZE. pd_handle=" << pd->handle); 
	} else {
		ffr->pd_map[pd->handle] = pd;
	}
	((struct IBV_ALLOC_PD_RSP *)rsp)->pd_handle = pd->handle;
	LOG_DEBUG("Alloc PD: handle = " << pd->handle); 

	return sizeof(struct IBV_ALLOC_PD_RSP);
}

int ib_uverbs_dealloc_pd(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===DEALLOC_PD===");

	struct IBV_DEALLOC_PD_REQ *request = (struct IBV_DEALLOC_PD_REQ*)req_body;
	//req_body = malloc(sizeof(struct IBV_DEALLOC_PD_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("Failed to read the request body.");
		return -1;
	}

	LOG_DEBUG("Dealloc PD: handle = " << request->pd_handle); 

	struct ibv_pd *pd = ffr->pd_map[request->pd_handle];
	if (pd == NULL) {
		LOG_ERROR("Failed to get pd with pd_handle " << request->pd_handle);
		return -2;
	}
	int ret = ibv_dealloc_pd(pd);
	ffr->pd_map[request->pd_handle] = NULL;
	//rsp = malloc(sizeof(struct IBV_DEALLOC_PD_RSP));
	((struct IBV_DEALLOC_PD_RSP *)rsp)->ret = ret;

	return sizeof(struct IBV_DEALLOC_PD_RSP);
}