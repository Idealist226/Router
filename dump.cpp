
int ib_uverbs_dump_objects() {
	// LOG_TRACE("IBV_DUMP_OBJECT");


	// struct IBV_DUMP_OBJECT_REQ *request = (struct IBV_DUMP_OBJECT_REQ *)req_body;
	// if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
	// 	LOG_ERROR("RESTORE_QP: Failed to read request body.");
	// 	goto kill;
	// }

	// uint32_t obj_num = 0;
	// size = sizeof(uint32_t);
	// struct ibv_dump_object *dump_obj = (struct ibv_dump_object*)(rsp + sizeof(uint32_t));

	// /* dump pd*/
	// for (int i = 0; i < MAP_SIZE; i++) {
	// 	struct ibv_pd *pd = ffr->pd_map[i];
	// 	if (pd != NULL) {
	// 		dump_obj->type = IBV_OBJECT_PD;
	// 		dump_obj->handle = pd->handle;
	// 		dump_obj->size = (sizeof(struct ibv_dump_pd));
	// 		size += dump_obj->size;
	// 		dump_obj += dump_obj->size;
	// 		obj_num++;
	// 	}
	// }

	// /* dump cq */
	// for (int i = 0; i < MAP_SIZE; i++) {
	// 	struct ibv_cq *cq = ffr->cq_map[i];
	// 	if (cq != NULL) {
	// 		dump_obj->type = IBV_OBJECT_CQ;
	// 		dump_obj->handle = cq->handle;

	// 		struct ibv_dump_cq *dump_cq = container_of(dump_obj, struct ibv_dump_cq, obj);
	// 		dump_cq->cqe = cq->cqe;

	// 		dump_obj->size = (sizeof(struct ibv_dump_cq));
	// 		size += dump_obj->size;
	// 		dump_obj += dump_obj->size;
	// 		obj_num++;
	// 	}
	// }

	// /* dump qp */
	// for (int i = 0; i < MAP_SIZE; i++) {
	// 	struct ibv_qp *qp = ffr->qp_map[i];
	// 	if (qp != NULL) {

	// 		dump_obj->type = IBV_OBJECT_QP;
	// 		dump_obj->handle = qp->handle;

	// 		struct ibv_dump_qp *dump_qp = container_of(dump_obj, struct ibv_dump_qp, obj);
	// 		dump_qp->pd_handle = qp->pd->handle;
	// 		dump_qp->qp_num = qp->qp_num;
	// 		dump_qp->state = qp->state;
	// 		dump_qp->qp_type = qp->qp_type;
	// 		dump_qp->send_cq_handle = qp->send_cq->handle;
	// 		dump_qp->recv_cq_handle = qp->recv_cq->handle;

	// 		struct ibv_qp_attr attr;
	// 		struct ibv_qp_init_attr init_attr;
	// 		int mask = IBV_QP_PATH_MTU | IBV_QP_PATH_MIG_STATE | IBV_QP_QKEY | IBV_QP_RQ_PSN | IBV_QP_SQ_PSN |
	// 					IBV_QP_DEST_QPN | IBV_QP_ACCESS_FLAGS | IBV_QP_CAP | IBV_QP_AV | IBV_QP_ALT_PATH |
	// 					IBV_QP_PKEY_INDEX | IBV_QP_EN_SQD_ASYNC_NOTIFY | IBV_QP_MAX_QP_RD_ATOMIC |
	// 					IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_PORT | IBV_QP_TIMEOUT |
	// 					IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_RATE_LIMIT;

	// 		ibv_query_qp(qp, &attr, mask, &init_attr);
	// 		LOG_DEBUG("Dump qp: qp_num=" << qp->qp_num << " pd_handle=" << qp->pd->handle << " state=" << qp->state << " qp_type=" << qp->qp_type << " send_cq_handle=" << qp->send_cq->handle << " recv_cq_handle=" << qp->recv_cq->handle);
	// 		LOG_DEBUG("Dump qp: path_mtu=" << attr.path_mtu << " path_mig_state=" << attr.path_mig_state << " qkey=" << attr.qkey << " rq_psn=" << attr.rq_psn << " sq_psn=" << attr.sq_psn << " dest_qp_num=" << attr.dest_qp_num << " qp_access_flags=" << attr.qp_access_flags << " port_num=" << attr.port_num);
	// 		LOG_DEBUG("Dump qp: cap.max_send_wr=" << init_attr.cap.max_send_wr << " cap.max_recv_wr=" << init_attr.cap.max_recv_wr << " cap.max_send_sge=" << init_attr.cap.max_send_sge << " cap.max_recv_sge=" << init_attr.cap.max_recv_sge << " cap.max_inline_data=" << init_attr.cap.max_inline_data);
			
	// 		dump_qp->sq_sig_all = init_attr.sq_sig_all;
	// 		memcpy(&(dump_qp->attr), &attr, sizeof(struct ibv_qp_attr));

	// 		dump_obj->size = (sizeof(struct ibv_dump_qp));
	// 		size += dump_obj->size;
	// 		dump_obj += dump_obj->size;
	// 		obj_num++;

	// 	}
	// }

	// /* dump mr */
	// for (int i = 0; i < MAP_SIZE; i++) {
	// 	struct ibv_mr *mr = ffr->mr_map[i];
	// 	if (mr != NULL) {
	// 		dump_obj->type = IBV_OBJECT_MR;
	// 		dump_obj->handle = mr->handle;

	// 		struct ibv_dump_mr *dump_mr = container_of(dump_obj, struct ibv_dump_mr, obj);
	// 		dump_mr->;

	// 		dump_obj->size = (sizeof(struct ibv_dump_mr));
	// 		size += dump_obj->size;
	// 		dump_obj += dump_obj->size;
	// 		obj_num++;
	// 	}
	// }

	// *rsp = obj_num;
}