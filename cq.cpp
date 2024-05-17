#include "include/verbs.h"
#include "include/log.h"

#include <sstream>

int ib_uverbs_create_cq(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===CREATE_CQ===");
	//req_body = malloc(sizeof(struct IBV_CREATE_CQ_REQ));
	struct ibv_comp_channel *channel;
	struct IBV_CREATE_CQ_REQ *request = (struct IBV_CREATE_CQ_REQ*)req_body;
	struct IBV_CREATE_CQ_RSP *response = (struct IBV_CREATE_CQ_RSP *)rsp;

	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("DESTROY_CQ: Failed to read the request body."); 
		return -1;
	}

	if (request->channel_fd < 0) {
		channel = NULL;
	} else {
		channel = ffr->channel_map[request->channel_fd];
		if (channel == NULL) {
			LOG_ERROR("Failed to get channel with fd " << request->channel_fd);
			return -2;
		}
	}

	struct ibv_cq *cq = ibv_create_cq(ffr->rdma_data.ib_context, request->cqe, NULL, channel, request->comp_vector);
	if (cq->handle >= MAP_SIZE) {
		LOG_ERROR("CQ handle (" << cq->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	} else {
		ffr->cq_map[cq->handle] = cq;
	}

	//rsp = malloc(sizeof(struct IBV_CREATE_CQ_RSP));
	response->cqe = cq->cqe;
	response->handle = cq->handle;

	LOG_DEBUG("Create CQ: cqe = " << cq->cqe << "handle = " << cq->handle);

	return sizeof(*response);
}

int ib_uverbs_destroy_cq(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===DESTROY_CQ===");

	struct IBV_DESTROY_CQ_REQ *request = (struct IBV_DESTROY_CQ_REQ*)req_body;
	//req_body = malloc(sizeof(struct IBV_DESTROY_CQ_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("DESTROY_CQ: Failed to read the request body."); 
		return -1;
	}

	LOG_DEBUG("Destroy CQ: cq handle = " << request->cq_handle);

	struct ibv_cq *cq = ffr->cq_map[request->cq_handle];
	if (cq == NULL) {
		LOG_ERROR("Failed to get cq with cq_handle " << request->cq_handle);
		return -2;
	}

	int ret = ibv_destroy_cq(cq);
	ffr->cq_map[request->cq_handle] = NULL;

	//rsp = malloc(sizeof(struct IBV_DESTROY_CQ_RSP));
	((struct IBV_DESTROY_CQ_RSP *)rsp)->ret = ret;
	return sizeof(struct IBV_DESTROY_CQ_RSP);
}

int ib_uverbs_poll_cq(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===IBV_POLL_CQ===");

	struct ibv_cq *cq = NULL;
	struct IBV_POLL_CQ_REQ *request = (struct IBV_POLL_CQ_REQ *)req_body;
	//req_body = malloc(sizeof(struct IBV_POLL_CQ_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("POLL_CQ: Failed to read request body.");
		return -1;
	}

	LOG_TRACE("CQ handle to poll: " << request->cq_handle);

	if (request->cq_handle >= MAP_SIZE) {
		LOG_ERROR("CQ handle (" << request->cq_handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	} else {
		cq = ffr->cq_map[request->cq_handle];
	}

	if (cq == NULL) {
		LOG_ERROR("cq pointer is NULL.");
		return -2;
	}

	//rsp = malloc(sizeof(struct FfrResponseHeader) + request->ne * sizeof(struct ibv_wc));
	struct FfrResponseHeader *response = (struct FfrResponseHeader *)rsp;
	struct ibv_wc *wc_list = (struct ibv_wc*)((char *)rsp + sizeof(*response));
	int count = ibv_poll_cq(cq, request->ne, wc_list);
	int size = 0;

	if (count <= 0) {
		LOG_DEBUG("The return of ibv_poll_cq is " << count);
		size = sizeof(*response);
		response->rsp_size = 0;    
	} else {
		size = sizeof(*response) + count * sizeof(struct ibv_wc);
		response->rsp_size = count * sizeof(struct ibv_wc);
	}

	// for (i = 0; i < count; i++) {
	// 	LOG_DEBUG("======== wc =========");
	// 	LOG_DEBUG("wr_id=" << wc_list[i].wr_id);
	// 	LOG_DEBUG("status=" << wc_list[i].status);
	// 	LOG_DEBUG("opcode=" << wc_list[i].opcode);
	// 	LOG_DEBUG("vendor_err=" << wc_list[i].vendor_err);
	// 	LOG_DEBUG("byte_len=" << wc_list[i].byte_len);
	// 	LOG_DEBUG("imm_data=" << wc_list[i].imm_data);
	// 	LOG_DEBUG("qp_num=" << wc_list[i].qp_num);
	// 	LOG_DEBUG("src_qp=" << wc_list[i].src_qp);
	// 	LOG_DEBUG("wc_flags=" << wc_list[i].wc_flags);
	// 	LOG_DEBUG("pkey_index=" << wc_list[i].pkey_index);
	// 	LOG_DEBUG("slid=" << wc_list[i].slid);
	// 	LOG_DEBUG("sl=" << wc_list[i].sl);
	// 	LOG_DEBUG("dlid_path_bits=" << wc_list[i].dlid_path_bits);
	// }
	return size;
}