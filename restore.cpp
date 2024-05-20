#include "include/router.h"
#include "include/log.h"
#include "include/verbs.h"

#include <sstream>

static int ib_uverbs_restore_pd(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_pd *pd = ibv_alloc_pd(ffr->rdma_data.ib_context);
	if (pd->handle >= MAP_SIZE) {
		LOG_ERROR("PD handle is no less than MAX_QUEUE_MAP_SIZE. pd_handle=" << pd->handle);
		return -1;
	} else {
		ffr->pd_map[obj->handle] = pd;
	}
	LOG_DEBUG("ib_uverbs_restore_pd: pd_handle=" << obj->handle);
	return sizeof(struct ibv_dump_pd);
}

static int ib_uverbs_restore_cq(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_dump_cq *dump_cq = container_of(obj, struct ibv_dump_cq, obj);

	// TODO: 增加 channel 和 comp_vector 的支持
	struct ibv_cq *cq = ibv_create_cq(ffr->rdma_data.ib_context, dump_cq->cqe, NULL, NULL, 0);
	if (cq->handle >= MAP_SIZE) {
		LOG_ERROR("CQ handle (" << cq->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
		return -1;
	} else {
		ffr->cq_map[obj->handle] = cq;
	}
	LOG_DEBUG("ib_uverbs_restore_cq: cq_handle=" << obj->handle);
	LOG_DEBUG("ib_uverbs_restore_cq: cqe=" << dump_cq->cqe);
	return sizeof(*dump_cq);
}

struct ibv_qp* ib_uverbs_recreate_qp(Router *ffr, ibv_dump_qp *dump_qp)
{
	struct ibv_qp_init_attr qp_init_attr;
	bzero(&qp_init_attr, sizeof(qp_init_attr));

	qp_init_attr.qp_type = dump_qp->qp_type;
	qp_init_attr.sq_sig_all = dump_qp->sq_sig_all;
	qp_init_attr.send_cq = ffr->cq_map[dump_qp->send_cq_handle];
	qp_init_attr.recv_cq = ffr->cq_map[dump_qp->recv_cq_handle];
	// qp_init_attr.srq = ffr->srq_map[srq_handle];
	memcpy(&(qp_init_attr.cap), &(dump_qp->attr.cap), sizeof(struct ibv_qp_cap));

	struct ibv_qp *qp = ibv_create_qp(ffr->pd_map[dump_qp->pd_handle], &qp_init_attr);
	if(!qp) {
		LOG_ERROR("failed to create QP\n");
		return NULL;
	}
	if (qp->handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] QP handle (" << qp->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
		return NULL;
	} else {
		ffr->qp_map[dump_qp->obj.handle] = qp;
	}

	return qp;
}

static int ib_uverbs_reconnect_qp(Router *ffr, struct ibv_qp_attr *dump_attr, struct ibv_qp *qp)
{
	LOG_TRACE("向对端 Router 发送重建 QP 连接的请求");
	// 向对端 Router 发送重建 QP 连接的请求
	int s;
	int remote_qpn = -1;
	struct sockaddr_in si_self, si_other;
	socklen_t slen = sizeof(si_other);
	struct IBV_RECONNECT_QP_REQ buf;

	/* 填充要发送的消息 */
	buf.send_qpn = qp->qp_num;
	buf.recv_qpn = dump_attr->dest_qp_num;
	buf.send_lid = ffr->rdma_data.ib_port_attr.lid;
	buf.send_gid = ffr->rdma_data.ib_gid;
	buf.recv_gid = dump_attr->ah_attr.grh.dgid;

	// TODO: 可以在初始化的时候，和其他所有 Router 通信，建立 GID 和 IP 的映射
	/* 和每个 Router 通信，对端 Router 会判断是否是发给自己的，如果是会返回相关信息*/
	for (int i = 0; i < HOST_NUM; i++) {
		if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
			LOG_ERROR("Error in creating socket for UDP server");
			return -1;
		}

		memset(&si_other, 0, sizeof(si_other));
		si_other.sin_family = AF_INET;
		si_other.sin_port = htons(RESTORE_PORT);
		if (inet_aton(HOST_LIST[i], &si_other.sin_addr) == 0) {
			LOG_ERROR("Error in creating socket for UDP client other.");
			return -1;
		}

		LOG_TRACE("Before send");

		uint8_t gid[16];
		memcpy(gid, &(ffr->rdma_data.ib_gid), 16);
		uint8_t *p = gid;
		fprintf(stdout, "Local GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
						p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

		if (sendto(s, &buf, sizeof(buf), 0, (const sockaddr*)&si_other, slen) < 0) {
			LOG_ERROR("Error in sending RECONNECT_QP_REQ to " << HOST_LIST[i]);
			return -1;
		} else {
			LOG_DEBUG("RECONNECT_QP_REQ has been sent to " << HOST_LIST[i]);
		}
		LOG_TRACE("向对端 Router 发送重建 QP 连接的请求 over");

		if (recvfrom(s, &remote_qpn, sizeof(remote_qpn), 0, (sockaddr*)&si_other, &slen)==-1) {
			LOG_ERROR("Error in receiving RECONNECT_QP_RSP");
			return -1;
		} else if (remote_qpn > 0) {
			LOG_DEBUG("Received RECONNECT_QP_RSP: remote_qpn=" << remote_qpn);
			break;
		}
	}

	return remote_qpn;
}

static int remodify_qp_to_init(struct ibv_qp_attr *dump_attr, struct ibv_qp *qp)
{
	int flags = 0, rc = 0;
	struct ibv_qp_attr attr;
	memset(&attr, 0, sizeof(attr));

	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = IB_PORT_NUM;
	attr.pkey_index = dump_attr->pkey_index;
	attr.qp_access_flags = dump_attr->qp_access_flags;
	flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
		LOG_ERROR("failed to modify QP state to INIT, ret = " << rc);
	}
	return rc;
}

// TODO: 如果对端 QP 不用重新创建，而是直接使用原来的 QP，那么这里的 remote_qpn 就不需要了
static int remodify_qp_to_rtr(struct ibv_qp_attr *dump_attr, struct ibv_qp *qp, int remote_qpn)
{
	int flags = 0, rc = 0;
	struct ibv_qp_attr attr;
	memset(&attr, 0, sizeof(attr));

	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = dump_attr->path_mtu;
	attr.dest_qp_num = remote_qpn;
	attr.rq_psn = 0;
	attr.max_dest_rd_atomic = dump_attr->max_dest_rd_atomic;
	attr.min_rnr_timer = dump_attr->min_rnr_timer;
	memcpy(&(attr.ah_attr), &(dump_attr->ah_attr), sizeof(struct ibv_ah_attr));

	flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
			IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
		LOG_ERROR("failed to modify QP state to RTR, ret = " << rc);
	}
	return rc;
}

static int remodify_qp_to_rts(struct ibv_qp_attr *dump_attr, struct ibv_qp *qp)
{
	int flags = 0, rc = 0;
	struct ibv_qp_attr attr;
	memset(&attr, 0, sizeof(attr));

	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = dump_attr->timeout;
	attr.retry_cnt = dump_attr->retry_cnt;
	attr.rnr_retry = dump_attr->rnr_retry;
	attr.sq_psn = 0;
	attr.max_rd_atomic = dump_attr->max_rd_atomic;

	flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc != 0) {
		LOG_ERROR("failed to modify QP state to RTS, ret = " << rc);
	}
	return rc;
}

int ib_uverbs_remodify_qp(struct ibv_dump_qp *dump_qp, struct ibv_qp *qp, int remote_qpn)
{
	if (dump_qp->state == IBV_QPS_RESET) {
		return 0;
	}

	if (remodify_qp_to_init(&(dump_qp->attr), qp)) {
		return -1;
	}

	if (dump_qp->state == IBV_QPS_INIT) {
		return 0;
	}

	if (remodify_qp_to_rtr(&(dump_qp->attr), qp, remote_qpn)) {
		return -1;
	}

	if (dump_qp->state == IBV_QPS_RTR) {
		return 0;
	}

	if (remodify_qp_to_rts(&(dump_qp->attr), qp)) {
		return -1;
	}

	if (dump_qp->state == IBV_QPS_RTS) {
		return 0;
	}

	LOG_ERROR("Unknown QP state: " << dump_qp->state);
	return -1;
}

static int ib_uverbs_restore_qp(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_dump_qp *dump_qp = container_of(obj, struct ibv_dump_qp, obj);
	LOG_DEBUG("ib_uverbs_restore_qp: qp_handle=" << obj->handle);
	LOG_DEBUG("ib_uverbs_restore_qp: pd_handle=" << dump_qp->pd_handle);
	LOG_DEBUG("ib_uverbs_restore_qp: qp_num=" << dump_qp->qp_num);
	LOG_DEBUG("ib_uverbs_restore_qp: state=" << dump_qp->state);
	LOG_DEBUG("ib_uverbs_restore_qp: qp_type=" << dump_qp->qp_type);
	LOG_DEBUG("ib_uverbs_restore_qp: send_cq_handle=" << dump_qp->send_cq_handle);
	LOG_DEBUG("ib_uverbs_restore_qp: recv_cq_handle=" << dump_qp->recv_cq_handle);

	LOG_DEBUG("ib_uverbs_restore_qp: cap.max_send_wr=" << dump_qp->attr.cap.max_send_wr);
	LOG_DEBUG("ib_uverbs_restore_qp: cap.max_recv_wr=" << dump_qp->attr.cap.max_recv_wr);
	LOG_DEBUG("ib_uverbs_restore_qp: cap.max_send_sge=" << dump_qp->attr.cap.max_send_sge);
	LOG_DEBUG("ib_uverbs_restore_qp: cap.max_recv_sge=" << dump_qp->attr.cap.max_recv_sge);
	LOG_DEBUG("ib_uverbs_restore_qp: cap.max_inline_data=" << dump_qp->attr.cap.max_inline_data);

	LOG_DEBUG("ib_uverbs_restore_qp: dest_qp_num=" << dump_qp->attr.dest_qp_num);

	/* 创建新的 QP */
	struct ibv_qp *qp = ib_uverbs_recreate_qp(ffr, dump_qp);
	if (qp == NULL) {
		return -1;
	}

	/* 和对端 QP 交换信息 */
	int remote_qpn = ib_uverbs_reconnect_qp(ffr, &(dump_qp->attr), qp);
	if (remote_qpn < 0) {
		LOG_ERROR("Failed to reconnect QP with remote QP.");
		return -1;
	}

	/* 重新修改 QP 的状态 */
	if (ib_uverbs_remodify_qp(dump_qp, qp, remote_qpn) < 0) {
		LOG_ERROR("Failed to remodify QP.");
		return -1;
	}

	return sizeof(*dump_qp);
}

static int ib_uverbs_restore_mr(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_dump_mr *dump_mr = container_of(obj, struct ibv_dump_mr, obj);
	LOG_DEBUG("ib_uverbs_restore_mr: mr_handle = " << obj->handle);
	LOG_DEBUG("ib_uverbs_restore_mr: pd_handle = " << dump_mr->pd_handle);
	LOG_DEBUG("ib_uverbs_restore_mr: addr = " << dump_mr->addr);
	LOG_DEBUG("ib_uverbs_restore_mr: length = " << dump_mr->length);
	LOG_DEBUG("ib_uverbs_restore_mr: lkey = " << dump_mr->lkey);
	LOG_DEBUG("ib_uverbs_restore_mr: rkey = " << dump_mr->rkey);
	LOG_DEBUG("ib_uverbs_restore_mr: shm_name = " << dump_mr->shm_name);

	/* 创建 Share Memory Space */
	ShmPiece *sp = ffr->addShmPiece(dump_mr->shm_name, dump_mr->length);
	if (sp == NULL) {
		LOG_ERROR("Failed to the shared memory piece.");
		return -1;
	}

	/* 创建 mr */
	struct ibv_pd *pd = ffr->pd_map[dump_mr->pd_handle];
	if (pd == NULL) {
		LOG_ERROR("Failed to get pd with pd_handle " << dump_mr->pd_handle);
		return -2;
	}
	int mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE ;
	struct ibv_mr *mr = ibv_reg_mr(pd, sp->ptr, sp->size, mr_flags);
	if (mr == NULL) {
		LOG_ERROR("Failed to regiester the MR. Current shared memory size: " << sp->size);
		return -1;
	}else if (mr->handle >= MAP_SIZE) {
		LOG_ERROR("[Warning] MR handle (" << mr->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
		return -1;
	} else {
		ffr->shmr_map[obj->handle] = sp;
		ffr->mr_map[obj->handle] = mr;
		// store lkey to ptr mapping
		pthread_mutex_lock(&ffr->lkey_ptr_mtx);
		ffr->lkey_ptr[dump_mr->lkey] = sp->ptr;
		pthread_mutex_unlock(&ffr->lkey_ptr_mtx);
		ffr->lkey_lkey[dump_mr->lkey] = mr->lkey;
		ffr->mr_handle_addr[obj->handle] = dump_mr->addr;
	}

	/* 向其他 Router 通报 mr 的 lkey 到本端 Router shm_ptr 的映射 */
	struct IBV_REG_MR_MAPPING_REQ mapping_req;
	mapping_req.key = dump_mr->lkey;
	mapping_req.mr_ptr = dump_mr->addr;
	mapping_req.shm_ptr = sp->ptr;
	if (ib_uverbs_reg_mr_mapping(&mapping_req) < 0) {
		LOG_ERROR("ib_uverbs_reg_mr_mapping failed");
		return -1;
	}
	
	return sizeof(*dump_mr);
}

int ib_uverbs_restore_objects(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===RESTORE_OBJECTS===");

	int ret = read(client_sock, req_body, MAX_SIZE);
	if (ret < 0) {
		LOG_ERROR("CREATE_CQ: Failed to read the request body."); 
		return -1;
	}
	LOG_DEBUG("ib_uverbs_restore_objects: Read " << ret << " bytes from client_sock.");

	uint32_t obj_num = *(uint32_t *)req_body;
	void *cur_obj = (char*)req_body + sizeof(obj_num);
	for (int i = 0; i < obj_num; i++) {
		struct ibv_dump_object *obj = (struct ibv_dump_object *)cur_obj;
		LOG_DEBUG("Found obj of type: " << obj->type);
		switch (obj->type) {
			case IBV_OBJECT_PD:
				ret = ib_uverbs_restore_pd(ffr, obj);
				break;
			case IBV_OBJECT_CQ:
				ret = ib_uverbs_restore_cq(ffr, obj);
				break;
			case IBV_OBJECT_QP:
				ret = ib_uverbs_restore_qp(ffr, obj);
				break;
			case IBV_OBJECT_MR:
				ret = ib_uverbs_restore_mr(ffr, obj);
				break;
			default:
				LOG_ERROR("Unknown object type: " << obj->type);
				ret = -1;
				break;
		}
		if (ret < 0) {
			return -1;
		}
		cur_obj = (char*)cur_obj + ret;
	}

	struct IBV_RESTORE_OBJECTS_RSP *response = (IBV_RESTORE_OBJECTS_RSP*)rsp;
	response->ret = ret < 0 ? -1 : 0;
	return sizeof(*response);
}