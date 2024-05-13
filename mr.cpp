#include "include/verbs.h"
#include "include/log.h"

#include <sstream>

int ib_uverbs_reg_mr(Router *ffr, int client_sock, void *req_body, void *rsp, int client_id)
{
	LOG_TRACE("===REG_MR===");
	//req_body = malloc(sizeof(struct IBV_REG_MR_REQ));
	ShmPiece *sp = NULL;
	struct IBV_REG_MR_REQ *request = (struct IBV_REG_MR_REQ*)req_body;
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("REG_MR: Failed to read request body.");
		return -1;
	}

	// create a shm buffer
	LOG_TRACE("Create a shared memory piece for client " << client_id << " with size " << request->mem_size);
	if (request->shm_name[0] == '\0') {
		LOG_TRACE("create shm from client id and count.");
		sp = ffr->addShmPiece(client_id, request->mem_size);
	} else {
		LOG_TRACE("create shm from name: " << request->shm_name);
		sp = ffr->addShmPiece(request->shm_name, request->mem_size);
	}
	
	if (sp == NULL) {
		LOG_ERROR("Failed to the shared memory piece.");
		return -2;
	}

	LOG_TRACE("Looking for PD with pd_handle " << request->pd_handle);
	struct ibv_pd *pd = ffr->pd_map[request->pd_handle];
	if (pd == NULL) {
		LOG_ERROR("Failed to get pd with pd_handle " << request->pd_handle);
		return -2;
	}

	LOG_DEBUG("Registering a MR ptr="<< sp->ptr << ", size="  << sp->size);            
	struct ibv_mr *mr = ibv_reg_mr(pd, sp->ptr, sp->size, request->access_flags);
	if (mr == NULL) {
		LOG_ERROR("Failed to regiester the MR. Current shared memory size: " << sp->size);
		return -2;
	}

	if (mr->handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] MR handle (" << mr->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	} else {
		ffr->shmr_map[mr->handle] = sp;
		ffr->mr_map[mr->handle] = mr;
		ffr->mr_handle_map[mr->handle] = mr->handle;
	}

	//rsp = malloc(sizeof(struct IBV_REG_MR_RSP));
	struct IBV_REG_MR_RSP *response = (struct IBV_REG_MR_RSP *)rsp;
	response->handle = mr->handle;
	response->lkey = mr->lkey;
	response->rkey = mr->rkey;
	strcpy(response->shm_name, sp->name.c_str());

	LOG_DEBUG("Reg MR: mr->handle=" << mr->handle);
	LOG_DEBUG("Reg MR: mr->lkey=" << mr->lkey);
	LOG_DEBUG("Reg MR: mr->rkey=" << mr->rkey);
	LOG_DEBUG("Reg MR: shm_name=" << sp->name.c_str());

	// store lkey to ptr mapping
	pthread_mutex_lock(&ffr->lkey_ptr_mtx);
	ffr->lkey_ptr[mr->lkey] = sp->ptr;
	pthread_mutex_unlock(&ffr->lkey_ptr_mtx);
	
	return sizeof(*response);
}

int ib_uverbs_reg_mr_mapping(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===REG_MR_MAPPING===");
	//req_body = malloc(sizeof(struct IBV_REG_MR_MAPPING_REQ));
	struct IBV_REG_MR_MAPPING_REQ *request = (struct IBV_REG_MR_MAPPING_REQ*)req_body;
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("REG_MR_MAPPING: Failed to read request body.");
		return -1;
	}
	
	pthread_mutex_lock(&ffr->lkey_ptr_mtx);
	request->shm_ptr = (char*)(ffr->lkey_ptr[request->key]);
	pthread_mutex_unlock(&ffr->lkey_ptr_mtx);

	struct sockaddr_in si_other;
	struct sockaddr src_addr;
	char recv_buff[100];
	ssize_t recv_buff_size;
	socklen_t slen = sizeof(si_other);
	int s;

	for (int i = 0; i < HOST_NUM; i++) {
		if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
			LOG_ERROR("Error in creating socket for UDP client");
			return -2;
		}

		memset(&si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons(MR_MAP_PORT);
		if (inet_aton(HOST_LIST[i], &si_other.sin_addr)==0) {
			LOG_ERROR("Error in creating socket for UDP client other.");
			continue;
		}

		if (sendto(s, req_body, sizeof(*request), 0, (const sockaddr*)&si_other, slen) < 0) {
			LOG_ERROR("Error in sending MR mapping to " << HOST_LIST[i]);
		} else {
			LOG_DEBUG("Sent MR mapping to " << HOST_LIST[i]);
		}

		if ((recv_buff_size = recvfrom(s, recv_buff, 100, 0, (sockaddr*)&si_other, &slen)) < 0) {
			LOG_ERROR("Error in receiving MR mapping ack" << HOST_LIST[i]);
		} else {
			char src_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &si_other.sin_addr, src_str, sizeof src_str);
			int src_port = ntohs(si_other.sin_port);
			LOG_DEBUG("## ACK from " << HOST_LIST[i] << "/" << src_str << ":" << src_port << " ack-rkey=" << recv_buff <<  " rkey= " << request->key);
		}

		close(s);
	}

	((struct IBV_REG_MR_MAPPING_RSP *)rsp)->ret = 0;
	return sizeof(struct IBV_REG_MR_MAPPING_RSP);
}

int ib_uverbs_dereg_mr(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===DEREG_MR===");

	struct IBV_DEREG_MR_REQ *request = (struct IBV_DEREG_MR_REQ*)req_body;
	//req_body = malloc(sizeof(struct IBV_DEREG_MR_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("DEREG_MR: Failed to read request body.");
		return -1;
	}

	ShmPiece* sp = ffr->shmr_map[request->handle];
	ibv_mr *mr = ffr->mr_map[request->handle];

	int ret = ibv_dereg_mr(mr);
	if (sp)
		delete sp;
	ffr->shmr_map[request->handle] = NULL;
	ffr->mr_map[request->handle] = NULL;

	//rsp = malloc(sizeof(struct IBV_DEREG_MR_RSP));
	((struct IBV_DEREG_MR_RSP *)rsp)->ret = ret;
	return sizeof(struct IBV_DEREG_MR_RSP);
}