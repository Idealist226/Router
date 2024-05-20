#include "include/verbs.h"
#include "include/log.h"

#include <sstream>

int ib_uverbs_create_qp(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===CREATE_QP===");

	struct IBV_CREATE_QP_REQ *request = (struct IBV_CREATE_QP_REQ*)req_body;
	struct IBV_CREATE_QP_RSP *response = (struct IBV_CREATE_QP_RSP*)rsp;
	//req_body = malloc(sizeof(struct IBV_CREATE_QP_REQ));
	if ((read(client_sock, req_body, sizeof(*request))) < sizeof(*request)) {
		LOG_ERROR("CREATE_CQ: Failed to read the request body."); 
		return -1;
	}

	struct ibv_qp_init_attr init_attr;
	bzero(&init_attr, sizeof(init_attr));
	
	init_attr.qp_type = request->qp_type;
	init_attr.sq_sig_all = request->sq_sig_all;

	init_attr.srq = ffr->srq_map[request->srq_handle];
	init_attr.send_cq = ffr->cq_map[request->send_cq_handle];
	init_attr.recv_cq = ffr->cq_map[request->recv_cq_handle];

	init_attr.cap.max_send_wr = request->cap.max_send_wr;
	init_attr.cap.max_recv_wr = request->cap.max_recv_wr;
	init_attr.cap.max_send_sge = request->cap.max_send_sge;
	init_attr.cap.max_recv_sge = request->cap.max_recv_sge;
	init_attr.cap.max_inline_data = request->cap.max_inline_data;

	LOG_TRACE("init_attr.qp_type=" << init_attr.qp_type); 
	LOG_TRACE("init_attr.sq_sig_all=" << init_attr.sq_sig_all); 
	LOG_TRACE("init_attr.srq=" << request->srq_handle); 
	LOG_TRACE("init_attr.send_cq=" << request->send_cq_handle); 
	LOG_TRACE("init_attr.recv_cq=" << request->recv_cq_handle); 
	LOG_TRACE("init_attr.cap.max_send_wr=" << init_attr.cap.max_send_wr); 
	LOG_TRACE("init_attr.cap.max_recv_wr=" << init_attr.cap.max_recv_wr); 
	LOG_TRACE("init_attr.cap.max_send_sge=" << init_attr.cap.max_send_sge); 
	LOG_TRACE("init_attr.cap.max_recv_sge=" << init_attr.cap.max_recv_sge); 
	LOG_TRACE("init_attr.cap.max_inline_data=" << init_attr.cap.max_inline_data); 
				
	struct ibv_pd *pd = ffr->pd_map[request->pd_handle];
	LOG_TRACE("Create QP: Get pd " << pd << "from pd_handle " << request->pd_handle);             

	struct ibv_qp *qp = ibv_create_qp(pd, &init_attr);
	if (qp == NULL) {
		LOG_ERROR("Failed to create a QP.");             
		return -2;
	}
	LOG_DEBUG("Create QP: qp handle = " << qp->handle);

	if (qp->handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] QP handle (" << qp->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	} else {
		ffr->qp_map[qp->handle] = qp;
	}

	response->qp_num = qp->qp_num;
	response->handle = qp->handle;
	response->cap.max_send_wr = init_attr.cap.max_send_wr;
	response->cap.max_recv_wr = init_attr.cap.max_recv_wr;
	response->cap.max_send_sge = init_attr.cap.max_send_sge;
	response->cap.max_recv_sge = init_attr.cap.max_recv_sge;
	response->cap.max_inline_data = init_attr.cap.max_inline_data;

	return sizeof(*response);
}

int ib_uverbs_destroy_qp(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===DESTROY_QP===");

	struct IBV_DESTROY_QP_REQ *request = (struct IBV_DESTROY_QP_REQ*)req_body;
	//req_body = malloc(sizeof(struct IBV_DESTROY_QP_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("Failed to read the request body."); 
		return -1;
	}

	int qp_handle = request->qp_handle;
	struct ibv_qp *qp = ffr->qp_map[qp_handle];
	if (qp == NULL) {
		LOG_ERROR("Failed to get qp with qp_handle " << qp_handle);
		return -2;
	}

	int ret = ibv_destroy_qp(qp);
	ffr->qp_map[qp_handle] = NULL;

	//rsp = malloc(sizeof(struct IBV_DESTROY_QP_RSP));
	((struct IBV_DESTROY_QP_RSP *)rsp)->ret = ret;
	return sizeof(struct IBV_DESTROY_QP_RSP);
}

int ib_uverbs_modify_qp(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===MODIFY_QP===");

	//req_body = malloc(sizeof(struct IBV_MODIFY_QP_REQ));
	struct IBV_MODIFY_QP_REQ *request = (struct IBV_MODIFY_QP_REQ *)req_body;
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("MODIFY_QP: Failed to read request body.");
		return -1;
	}

	LOG_TRACE("QP handle to modify: " << request->handle);
	if (request->handle >= MAP_SIZE) {
		LOG_ERROR("QP handle (" << request->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	}

	struct ibv_qp *qp = ffr->qp_map[request->handle];
	struct ibv_qp_attr *init_attr = &request->attr;

	/*if (init_attr->qp_state == IBV_QPS_RTR && !ffr->ibv_gid_init && init_attr->ah_attr.grh.dgid.global.subnet_prefix)
	{
		memcpy(&ffr->gid, &init_attr->ah_attr.grh.dgid, sizeof(union ibv_gid));
		ffr->ibv_gid_init = 1;
	}

	if (init_attr->qp_state == IBV_QPS_RTR && ffr->ibv_gid_init && !init_attr->ah_attr.grh.dgid.global.subnet_prefix)
	{
		memcpy(&init_attr->ah_attr.grh.dgid, &ffr->gid, sizeof(union ibv_gid));
		init_attr->ah_attr.grh.hop_limit = 1; 
	}*/

	int ret = ibv_modify_qp(qp, &request->attr, request->attr_mask);
	if (ret != 0) {
		LOG_ERROR("Modify QP (" << qp->handle << ") fails. ret = " << ret << "errno = " << errno);
	}

	LOG_DEBUG("---------- QP=" << request->handle << " -----------");
	LOG_DEBUG("attr.qp_state=" << init_attr->qp_state);
	LOG_DEBUG("attr.cur_qp_state=" << init_attr->cur_qp_state);
	LOG_DEBUG("attr.path_mtu=" << init_attr->path_mtu);
	LOG_DEBUG("attr.path_mig_state=" << init_attr->path_mig_state);
	LOG_DEBUG("attr.qkey=" << init_attr->qkey);
	LOG_DEBUG("attr.rq_psn=" << init_attr->rq_psn);
	LOG_DEBUG("attr.sq_psn=" << init_attr->sq_psn);
	LOG_DEBUG("attr.dest_qp_num=" << init_attr->dest_qp_num);
	LOG_DEBUG("attr.qp_access_flags=" << init_attr->qp_access_flags);
	LOG_DEBUG("attr.cap.max_send_wr=" << init_attr->cap.max_send_wr);
	LOG_DEBUG("attr.cap.max_recv_wr=" << init_attr->cap.max_recv_wr);
	LOG_DEBUG("attr.cap.max_send_sge=" << init_attr->cap.max_send_sge);
	LOG_DEBUG("attr.cap.max_recv_sge=" << init_attr->cap.max_recv_sge);
	LOG_DEBUG("attr.cap.max_inline_data=" << init_attr->cap.max_inline_data);
	LOG_DEBUG("attr.ah_attr.global.subnet_prefix=" << init_attr->ah_attr.grh.dgid.global.subnet_prefix);
	LOG_DEBUG("attr.ah_attr.global.interface_id=" << init_attr->ah_attr.grh.dgid.global.interface_id);
	LOG_DEBUG("attr.ah_attr.flow_label=" << init_attr->ah_attr.grh.flow_label);
	LOG_DEBUG("attr.ah_attr.sgid_index=" << (int)init_attr->ah_attr.grh.sgid_index);
	LOG_DEBUG("attr.ah_attr.hop_limit=" << (int)init_attr->ah_attr.grh.hop_limit);
	LOG_DEBUG("attr.ah_attr.traffic_class=" << (int)init_attr->ah_attr.grh.traffic_class);

	//rsp = malloc(sizeof(struct IBV_MODIFY_QP_RSP));
	((struct IBV_MODIFY_QP_RSP *)rsp)->ret = ret;
	((struct IBV_MODIFY_QP_RSP *)rsp)->handle = request->handle;
	return sizeof(struct IBV_MODIFY_QP_RSP);
}

int ib_uverbs_post_send(Router *ffr, int client_sock, void *req_body, void *rsp, uint32_t body_size)
{
	LOG_TRACE("===POST_SEND===");
	//req_body = malloc(header.body_size);
	if (read(client_sock, req_body, body_size) < body_size) {
		LOG_ERROR("POST_SEND: Error in reading in post send.");
		return -1;
	}

	// Now recover the qp and wr
	struct ibv_qp *qp = NULL;
	struct ib_uverbs_post_send *post_send = (struct ib_uverbs_post_send*)req_body;
	if (post_send->qp_handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] QP handle (" << post_send->qp_handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
		return -1;
	} else {
		qp = ffr->qp_map[post_send->qp_handle];
	}

	struct ibv_send_wr *wr = (struct ibv_send_wr*)((char*)req_body + sizeof(struct ib_uverbs_post_send));
	struct ibv_sge *sge = (struct ibv_sge*)((char*)req_body + sizeof(struct ib_uverbs_post_send) + post_send->wr_count * sizeof(struct ibv_send_wr));

	uint32_t *ah = NULL; 
	if (qp->qp_type == IBV_QPT_UD) {
		LOG_INFO("POST_SEND_UD!!!");
		ah = (uint32_t*)(sge + post_send->sge_count);
	}

	uint32_t wr_success = 0;
	for (int i = 0; i < post_send->wr_count; i++) {
		LOG_INFO("wr[i].wr_id=" << wr[i].wr_id << " opcode=" << wr[i].opcode <<  " imm_data==" << wr[i].imm_data);

		if (wr[i].opcode == IBV_WR_RDMA_WRITE || wr[i].opcode == IBV_WR_RDMA_WRITE_WITH_IMM || wr[i].opcode == IBV_WR_RDMA_READ) {
			if (ffr->rkey_mr_shm.find(wr[i].wr.rdma.rkey) == ffr->rkey_mr_shm.end()) {
				LOG_ERROR("One sided opertaion: can't find remote MR. rkey --> " << wr[i].wr.rdma.rkey << "  addr --> " << wr[i].wr.rdma.remote_addr);
			} else {
				LOG_DEBUG("shm:" << (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].shm_ptr) << " app:" << (uint64_t)(wr[i].wr.rdma.remote_addr) << " mr:" << (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].mr_ptr));
				wr[i].wr.rdma.remote_addr = (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].shm_ptr) + (uint64_t)wr[i].wr.rdma.remote_addr - (uint64_t)ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].mr_ptr;
			}
		}

		// fix the link list pointer
		if (i >= post_send->wr_count - 1) {
			wr[i].next = NULL;
		} else {
			wr[i].next = &(wr[i+1]);
		}

		if (wr[i].num_sge > 0) {
			// fix the sg list pointer
			wr[i].sg_list = sge;
			pthread_mutex_lock(&ffr->lkey_ptr_mtx);
			for (int j = 0; j < wr[i].num_sge; j++) {
				LOG_DEBUG("wr[i].wr_id=" << wr[i].wr_id << " qp_num=" << qp->qp_num << " sge.addr=" << sge[j].addr << " sge.length=" << sge[j].length << " opcode=" << wr[i].opcode);
				sge[j].addr = (uint64_t)((char*)(ffr->lkey_ptr[sge[j].lkey]) + sge[j].addr);
				// restore 后，mr 的 lkey 会改变，所以需要重新设置
				sge[j].lkey = ffr->lkey_lkey[sge[j].lkey];
				LOG_DEBUG("data=" << ((char*)(sge[j].addr))[0] << ((char*)(sge[j].addr))[1] << ((char*)(sge[j].addr))[2]);
				LOG_DEBUG("imm_data==" << wr[i].imm_data);
			}
			pthread_mutex_unlock(&ffr->lkey_ptr_mtx);

			sge += wr[i].num_sge;
		} else {
			wr[i].sg_list = NULL;
		}

		// fix ah
		if (qp->qp_type == IBV_QPT_UD) {
			wr[i].wr.ud.ah = ffr->ah_map[*ah];
			ah = ah + 1;
		}

		wr_success++;
	}

	struct ibv_send_wr *bad_wr = NULL;
	IBV_POST_SEND_RSP *response = (IBV_POST_SEND_RSP*)rsp;
	//rsp = malloc(sizeof(struct IBV_POST_SEND_RSP));

	response->ret_errno = ibv_post_send(qp, wr, &bad_wr);
	if (response->ret_errno != 0) {
		LOG_ERROR("[Error] Post send (" << qp->handle << ") fails.");
	}

	LOG_TRACE("post_send success.");

	if (bad_wr == NULL) {
		// this IF is not needed right now, but left here for future use
		if (post_send->wr_count == wr_success) {
			response->bad_wr = 0;
		} else {
			response->bad_wr = post_send->wr_count - wr_success;
			response->ret_errno = ENOMEM;
		}
	} else{
		LOG_ERROR("bad_wr is not NULL.");
		response->bad_wr = bad_wr - wr;
	}

	return sizeof(*response);
}

int ib_uverbs_post_recv(Router *ffr, int client_sock, void *req_body, void *rsp, uint32_t body_size)
{
	LOG_TRACE("===IBV_POST_RECV===");
	//req_body = malloc(header.body_size);
	if (read(client_sock, req_body, body_size) < body_size) {
		LOG_ERROR("POST_RECV: Error in reading in post recv.");
		return -1;
	}

	// Now recover the qp and wr
	struct ibv_qp *qp = NULL;
	struct ib_uverbs_post_recv *post_recv = (struct ib_uverbs_post_recv*)req_body;
	if (post_recv->qp_handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] QP handle (" << post_recv->qp_handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	} else {
		qp = ffr->qp_map[post_recv->qp_handle];
	}

	struct ibv_recv_wr *wr = (struct ibv_recv_wr*)((char*)req_body + sizeof(*post_recv));
	struct ibv_sge *sge = (struct ibv_sge*)(wr + post_recv->wr_count);

	for (int i = 0; i < post_recv->wr_count; i++) {
		// fix the link list pointer
		if (i >= post_recv->wr_count - 1) {
			wr[i].next = NULL;
		} else {
			wr[i].next = &(wr[i+1]);
		}

		if (wr[i].num_sge > 0) {
			// fix the sg list pointer
			wr[i].sg_list = sge;
			pthread_mutex_lock(&ffr->lkey_ptr_mtx);
			for (int j = 0; j < wr[i].num_sge; j++) {
				sge[j].addr = (uint64_t)(ffr->lkey_ptr[sge[j].lkey]) + (uint64_t)(sge[j].addr);
				sge[j].lkey = ffr->lkey_lkey[sge[j].lkey];
			}
			pthread_mutex_unlock(&ffr->lkey_ptr_mtx);
			sge += wr[i].num_sge;
		} else {
			wr[i].sg_list = NULL;	
		}
	}

	struct ibv_recv_wr *bad_wr = NULL;
	struct IBV_POST_RECV_RSP *response = (IBV_POST_RECV_RSP*)rsp;
	//rsp = malloc(sizeof(struct IBV_POST_RECV_RSP));
	response->ret_errno = ibv_post_recv(qp, wr, &bad_wr);
	if (response->ret_errno != 0) {
		LOG_ERROR("[Error] Post recv (" << qp->handle << ") fails.");
	}
	if (bad_wr == NULL) {
		response->bad_wr = 0;
	} else {
		response->bad_wr = bad_wr - wr;
	}

	return sizeof(*response);
}