#include "include/verbs.h"
#include "include/log.h"

static uint32_t obj_num = 0;
static int size = sizeof(obj_num);
static struct ibv_pd *pd = NULL;
static struct ibv_cq *cq = NULL;
static struct ibv_qp *qp = NULL;
static struct ibv_mr *mr = NULL;
char *shm_name = NULL;

static void ib_uverbs_dump_pd(struct ibv_dump_object *dump_obj)
{
	dump_obj->type = IBV_OBJECT_PD;
	dump_obj->handle = pd->handle;
	dump_obj->size = sizeof(struct ibv_dump_pd);
	LOG_DEBUG("Dump pd: pd_handle=" << pd->handle);
}

static void ib_uverbs_dump_cq(struct ibv_dump_object *dump_obj)
{
	dump_obj->type = IBV_OBJECT_CQ;
	dump_obj->handle = cq->handle;
	dump_obj->size = sizeof(struct ibv_dump_cq);

	struct ibv_dump_cq *dump_cq = container_of(dump_obj, struct ibv_dump_cq, obj);
	dump_cq->cqe = cq->cqe;
	LOG_DEBUG("Dump cq: cqe=" << cq->cqe);
}

// 不使用全局 qp 是为了将 ib_uverbs_dump_qp 设置为通用接口
void ib_uverbs_dump_qp(struct ibv_dump_object *dump_obj, struct ibv_qp *qp)
{
	dump_obj->type = IBV_OBJECT_QP;
	dump_obj->handle = qp->handle;
	dump_obj->size = sizeof(struct ibv_dump_qp);

	struct ibv_dump_qp *dump_qp = container_of(dump_obj, struct ibv_dump_qp, obj);
	dump_qp->pd_handle = qp->pd->handle;
	dump_qp->qp_num = qp->qp_num;
	dump_qp->state = qp->state;
	dump_qp->qp_type = qp->qp_type;
	dump_qp->send_cq_handle = qp->send_cq->handle;
	dump_qp->recv_cq_handle = qp->recv_cq->handle;
	dump_qp->srq_handle = qp->srq ? qp->srq->handle : 0;

	struct ibv_qp_attr attr;
	struct ibv_qp_init_attr init_attr;
	memset(&attr, 0, sizeof(attr));
	memset(&init_attr, 0, sizeof(attr));

	// TODO: 这里不能查询 IBV_QP_RATE_LIMIT，否则会失效
	int mask = IBV_QP_PATH_MTU | IBV_QP_PATH_MIG_STATE | IBV_QP_QKEY | IBV_QP_RQ_PSN | IBV_QP_SQ_PSN |
				IBV_QP_DEST_QPN | IBV_QP_ACCESS_FLAGS | IBV_QP_CAP | IBV_QP_AV | IBV_QP_ALT_PATH |
				IBV_QP_PKEY_INDEX | IBV_QP_EN_SQD_ASYNC_NOTIFY | IBV_QP_MAX_QP_RD_ATOMIC |
				IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_PORT | IBV_QP_TIMEOUT |
				IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY;

	if (ibv_query_qp(qp, &attr, mask, &init_attr) < 0) {
		LOG_ERROR("query qp error");
		return;
	}
	LOG_DEBUG("Dump qp: qp_num=" << qp->qp_num << " pd_handle=" << qp->pd->handle << " state=" << qp->state << " qp_type=" << qp->qp_type << " send_cq_handle=" << qp->send_cq->handle << " recv_cq_handle=" << qp->recv_cq->handle);
	LOG_DEBUG("Dump qp: path_mtu=" << attr.path_mtu << " path_mig_state=" << attr.path_mig_state << " qkey=" << attr.qkey << " rq_psn=" << attr.rq_psn << " sq_psn=" << attr.sq_psn << " dest_qp_num=" << attr.dest_qp_num << " qp_access_flags=" << attr.qp_access_flags << " port_num=" << attr.port_num);
	LOG_DEBUG("Dump qp: cap.max_send_wr=" << attr.cap.max_send_wr << " cap.max_recv_wr=" << attr.cap.max_recv_wr << " cap.max_send_sge=" << attr.cap.max_send_sge << " cap.max_recv_sge=" << attr.cap.max_recv_sge << " cap.max_inline_data=" << attr.cap.max_inline_data);
	
	dump_qp->sq_sig_all = init_attr.sq_sig_all;
	memcpy(&(dump_qp->attr), &attr, sizeof(struct ibv_qp_attr));
}

static void ib_uverbs_dump_mr(struct ibv_dump_object *dump_obj, Router *ffr)
{
	dump_obj->type = IBV_OBJECT_MR;
	dump_obj->handle = mr->handle;
	dump_obj->size = sizeof(struct ibv_dump_mr);

	struct ibv_dump_mr *dump_mr = container_of(dump_obj, struct ibv_dump_mr, obj);
	dump_mr->pd_handle = mr->pd->handle;
	dump_mr->addr = mr->addr;
	dump_mr->length = mr->length;
	dump_mr->lkey = mr->lkey;
	dump_mr->rkey = mr->rkey;
	strcpy(dump_mr->shm_name, ffr->shmr_map[mr->handle]->name.c_str());
}

static void ib_uverbs_dump_object(enum ibv_object_type type, struct ibv_dump_object **dump_obj, Router *ffr)
{
	switch (type) {
		case IBV_OBJECT_PD:
			ib_uverbs_dump_pd(*dump_obj);
			break;
		case IBV_OBJECT_CQ:
			ib_uverbs_dump_cq(*dump_obj);
			break;
		case IBV_OBJECT_QP:
			ib_uverbs_dump_qp(*dump_obj, qp);
			break;
		case IBV_OBJECT_MR:
			ib_uverbs_dump_mr(*dump_obj, ffr);
			break;
	}
	size += (*dump_obj)->size;
	*dump_obj = (struct ibv_dump_object *)((char *)(*dump_obj) + (*dump_obj)->size);
	obj_num++;
	// TODO: dump 完是否要销毁对应的 verbs 对象？
}

int ib_uverbs_dump_objects(Router *ffr, int client_sock, void *req_body, void *rsp) {
	LOG_TRACE("===IBV_DUMP_OBJECTS===");

	struct ibv_dump_object *dump_obj = (struct ibv_dump_object*)((char *)rsp + sizeof(uint32_t));

	/* dump pd*/
	for (int i = 0; i < MAP_SIZE; i++) {
		struct ibv_pd *cur_pd = ffr->pd_map[i];
		if (cur_pd != NULL) {
			pd = cur_pd;
			ib_uverbs_dump_object(IBV_OBJECT_PD, &dump_obj, NULL);
		}
	}

	/* dump cq */
	for (int i = 0; i < MAP_SIZE; i++) {
		struct ibv_cq *cur_cq = ffr->cq_map[i];
		if (cur_cq != NULL) {
			cq = cur_cq;
			ib_uverbs_dump_object(IBV_OBJECT_CQ, &dump_obj, NULL);
		}
	}

	/* dump qp */
	for (int i = 0; i < MAP_SIZE; i++) {
		struct ibv_qp *cur_qp = ffr->qp_map[i];
		if (cur_qp != NULL) {
			qp = cur_qp;
			ib_uverbs_dump_object(IBV_OBJECT_QP, &dump_obj, NULL);
		}
	}

	/* dump mr */
	for (int i = 0; i < MAP_SIZE; i++) {
		struct ibv_mr *cur_mr = ffr->mr_map[i];
		if (cur_mr != NULL) {
			mr = cur_mr;
			ib_uverbs_dump_object(IBV_OBJECT_MR, &dump_obj, ffr);
		}
	}

	*(uint32_t*)rsp = obj_num;
	return size;
}