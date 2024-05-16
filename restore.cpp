#include "include/router.h"
#include "include/log.h"
#include "include/verbs.h"

#include <sstream>

int ib_uverbs_restore_qp(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===IBV_RESTORE_QP===");

	struct IBV_RESTORE_QP_REQ *request = (struct IBV_RESTORE_QP_REQ *)req_body;
	//req_body = malloc(sizeof(struct IBV_RESTORE_QP_REQ));
	if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
		LOG_ERROR("RESTORE_QP: Failed to read request body.");
		return -1;
	}

	struct ibv_qp_init_attr qp_init_attr;
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.qp_type = IBV_QPT_RC;
	qp_init_attr.sq_sig_all = 1;
	int send_cq_handle = ffr->getHandle(IBV_CQ, request->send_cq_handle);
	int recv_cq_handle = ffr->getHandle(IBV_CQ, request->recv_cq_handle);
	qp_init_attr.send_cq = ffr->cq_map[send_cq_handle];
	qp_init_attr.recv_cq = ffr->cq_map[recv_cq_handle];
	qp_init_attr.cap.max_send_wr = 1;
	qp_init_attr.cap.max_recv_wr = 1;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;
	int pd_handle = ffr->getHandle(IBV_PD, request->pd_handle);

	struct ibv_qp *test_qp = ibv_create_qp(ffr->pd_map[pd_handle], &qp_init_attr);
	if(!test_qp) {
		LOG_ERROR("failed to create QP\n");
	}
	LOG_DEBUG("create qp over");




	LOG_DEBUG("向对端 Router 发送重建 QP 连接的请求");
	// 向对端 Router 发送重建 QP 连接的请求
	struct sockaddr_in si_self, si_other;
	int s;
	socklen_t slen = sizeof(si_other);
	struct IBV_RECONNECT_QP_REQ buf;
	
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		LOG_ERROR("Error in creating socket for UDP server");
	}

	memset(&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(RESTORE_PORT);
	// TODO: change the IP address
	if (inet_aton("192.168.122.68", &si_other.sin_addr) == 0) {
		LOG_ERROR("Error in creating socket for UDP client other.");
	}

	// memset(&si_self, 0, sizeof(si_self));
	// si_self.sin_family = AF_INET;
	// si_self.sin_port = htons(0);
	// if (inet_aton("0.0.0.0", &si_self.sin_addr) == 0) {
	// 	LOG_ERROR("Error in creating socket for UDP client self.");
	// }

	// if (bind(s, (const struct sockaddr *)&si_self, sizeof(si_self)) < 0) {
	// 	LOG_ERROR("Failed to bind UDP. errno=" << errno);
	// }

	buf.src_qpn = test_qp->qp_num;
	buf.dest_qpn = request->qpn;
	buf.lid = ffr->rdma_data.ib_port_attr.lid;
	buf.gid = ffr->rdma_data.ib_gid;

	LOG_DEBUG("Before send");
	uint8_t gid[16];
	memcpy(gid, &(ffr->rdma_data.ib_gid), 16);
	uint8_t *p = gid;
	fprintf(stdout, "Local GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
					p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

	if (sendto(s, &buf, sizeof(buf), 0, (const sockaddr*)&si_other, slen) < 0) {
		LOG_DEBUG("Error in sending RECONNECT_QP_REQ to " << "192.168.122.68");
	} else {
		LOG_TRACE("Sent RECONNECT_QP_REQ to " << "192.168.122.47");
	}
	LOG_DEBUG("向对端 Router 发送重建 QP 连接的请求 over");


	int remote_qpn;
	if (recvfrom(s, &remote_qpn, sizeof(remote_qpn), 0, (sockaddr*)&si_other, &slen)==-1) {
		LOG_DEBUG("Error in receiving RECONNECT_QP_RSP");
	} else {
		LOG_DEBUG("Received RECONNECT_QP_RSP: remote_qpn=" << remote_qpn);
	}

	// close(s);

	move_qp_to_init(test_qp);
	LOG_DEBUG("move_qp_to_init over");
	struct ib_conn_data dest;
	dest.out_reads = 1;
	dest.psn = 0;
	dest.lid = request->lid;
	// dest.qpn = request->qpn;
	dest.qpn = remote_qpn;
	dest.gid = request->gid;
	move_qp_to_rtr(test_qp, &dest);
	LOG_DEBUG("move_qp_to_rtr over");
	move_qp_to_rts(test_qp);
	LOG_DEBUG("move_qp_to_rts over");

	ffr->qp_map[test_qp->handle] = test_qp;
	ffr->qp_handle_map[request->qp_handle] = test_qp->handle;


	struct IBV_RESTORE_QP_RSP *response = (struct IBV_RESTORE_QP_RSP *)rsp;
	std::stringstream ss;
	ss << "qp" << test_qp->handle;
	ShmPiece* sp = ffr->initCtrlShm(ss.str().c_str());
	ffr->qp_shm_map[test_qp->handle] = sp;
	strcpy(response->shm_name, sp->name.c_str());

	return sizeof(struct IBV_RESTORE_QP_RSP);
}

int ib_uverbs_restore_pd(Router *ffr, ibv_dump_object *obj)
{
	// struct ibv_pd *pd = ibv_alloc_pd(ffr->rdma_data.ib_context);
	// if (pd->handle >= MAP_SIZE) {
	// 	LOG_ERROR("PD handle is no less than MAX_QUEUE_MAP_SIZE. pd_handle=" << pd->handle);
	// 	return -1;
	// } else {
	// 	ffr->pd_map[pd->handle] = pd;
	// 	ffr->pd_handle_map[obj->handle] = pd->handle;
	// }
	LOG_DEBUG("ib_uverbs_restore_pd: pd_handle=" << obj->handle);
	return sizeof(struct ibv_dump_pd);
}

int ib_uverbs_restore_cq(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_dump_cq *dump_cq = container_of(obj, struct ibv_dump_cq, obj);

	// // TODO: 增加 channel 和 comp_vector 的支持
	// struct ibv_cq *cq = ibv_create_cq(ffr->rdma_data.ib_context, dump_cq->cqe, NULL, NULL, 0);
	// if (cq->handle >= MAP_SIZE) {
	// 	LOG_ERROR("CQ handle (" << cq->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
	// 	return -1;
	// } else {
	// 	ffr->cq_map[cq->handle] = cq;
	// 	ffr->cq_handle_map[obj->handle] = cq->handle;
	// }
	LOG_DEBUG("ib_uverbs_restore_cq: cq_handle=" << obj->handle);
	LOG_DEBUG("ib_uverbs_restore_cq: cqe=" << dump_cq->cqe);
	return sizeof(*dump_cq);
}

int ib_uverbs_restore_qp(Router *ffr, ibv_dump_object *obj)
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
	return sizeof(*dump_qp);
}

int ib_uverbs_restore_mr(Router *ffr, ibv_dump_object *obj)
{
	struct ibv_dump_mr *dump_mr = container_of(obj, struct ibv_dump_mr, obj);
	LOG_DEBUG("ib_uverbs_restore_mr: mr_handle=" << obj->handle);
	LOG_DEBUG("ib_uverbs_restore_mr: pd_handle=" << dump_mr->pd_handle);
	LOG_DEBUG("ib_uverbs_restore_mr: addr=" << dump_mr->addr);
	LOG_DEBUG("ib_uverbs_restore_mr: length=" << dump_mr->length);
	return sizeof(*dump_mr);
}

int ib_uverbs_restore_objects(Router *ffr, int client_sock, void *req_body, void *rsp)
{
	LOG_TRACE("===RESTORE_OBJECTS===");

	int ret = read(client_sock, req_body, sizeof(4*1024));
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
			break;
		}
		cur_obj = (char*)cur_obj + ret;
	}

	struct IBV_RESTORE_OBJECTS_RSP *response = (IBV_RESTORE_OBJECTS_RSP*)rsp;
	response->ret = ret < 0 ? -1 : 0;
	return sizeof(*response);
}