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