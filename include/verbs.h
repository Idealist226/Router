#include "router.h"

/* pd */
int ib_uverbs_alloc_pd(Router *ffr, void *rsp);
int ib_uverbs_dealloc_pd(Router *ffr, int client_sock, void *req_body, void *rsp);

/* cq */
int ib_uverbs_create_cq(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_destroy_cq(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_poll_cq(Router *ffr, int client_sock, void *req_body, void *rsp);

/* qp */
int ib_uverbs_create_qp(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_destroy_qp(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_modify_qp(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_post_send(Router *ffr, int client_sock, void *req_body, void *rsp, uint32_t body_size);
int ib_uverbs_post_recv(Router *ffr, int client_sock, void *req_body, void *rsp, uint32_t body_size);

/* mr */
int ib_uverbs_reg_mr(Router *ffr, int client_sock, void *req_body, void *rsp, int client_id);
int ib_uverbs_reg_mr_mapping(Router *ffr, int client_sock, void *req_body, void *rsp);
int ib_uverbs_dereg_mr(Router *ffr, int client_sock, void *req_body, void *rsp);

/* dump */

/* restore */
int ib_uverbs_restore_qp(Router *ffr, int client_sock, void *req_body, void *rsp);