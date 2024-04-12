#ifndef ROUTER_H
#define ROUTER_H

#include "constant.h"
#include "rdma_api.h"
#include "shared_memory.h"
#include "types.h"

#include <string>
#include <map>
#include <vector>
#include <infiniband/verbs.h>
// #include <rdma/rdma_cma.h>

#define UDP_PORT 11232
#define HOST_NUM 2

const char HOST_LIST[HOST_NUM][16] = {
	"192.168.2.13",
	"192.168.2.15"
};

struct HandlerArgs {
    struct Router *ffr;
    int client_sock;
    int count;
};

class Router {
public:
	int sock;
	std::string name;
	std::string pathname;
	int pid_count;
	struct ib_data rdma_data;
	struct ibv_pd* pd_map[MAP_SIZE];
	int pd_handle_map[MAP_SIZE];
	struct ibv_cq* cq_map[MAP_SIZE];
	int cq_handle_map[MAP_SIZE];
	struct ibv_qp* qp_map[MAP_SIZE];
	int qp_handle_map[MAP_SIZE];
	struct ibv_mr* mr_map[MAP_SIZE];
	int mr_handle_map[MAP_SIZE];
	struct ibv_ah* ah_map[MAP_SIZE];
	struct ibv_srq* srq_map[MAP_SIZE];
	struct ibv_comp_channel* channel_map[MAP_SIZE];
	// struct rdma_event_channel* event_channel_map[MAP_SIZE];
	// struct rdma_cm_id* cm_id_map[MAP_SIZE];
	
	ShmPiece* shmr_map[MAP_SIZE];
	ShmPiece* qp_shm_map[MAP_SIZE];
	ShmPiece* cq_shm_map[MAP_SIZE];
	// ShmPiece* srq_shm_map[MAP_SIZE];
	// std::vector<uint32_t> qp_shm_vec;
	// pthread_mutex_t qp_shm_vec_mtx;
	// std::vector<uint32_t> cq_shm_vec;
	// pthread_mutex_t cq_shm_vec_mtx;
	// std::vector<uint32_t> srq_shm_vec;
	// pthread_mutex_t srq_shm_vec_mtx;

	std::map<uintptr_t, uintptr_t> uid_map;

	// clientid --> shared memmory piece vector
	std::map<int, std::vector<ShmPiece*> > shm_pool;
	std::map<std::string, ShmPiece* > shm_map;
	pthread_mutex_t shm_mutex;

	// lkey --> ptr of shm piece buffer
	std::map<uint32_t, void*> lkey_ptr;
	pthread_mutex_t lkey_ptr_mtx;

	// rkey --> MR and SHM pointers
	// std::map<uint32_t, struct MR_SHM> rkey_mr_shm;
	// pthread_mutex_t rkey_mr_shm_mtx;

	// qp_handle -> tokenbucket
	// std::map<uint32_t, TokenBucket*> tokenbucket;

	// fsocket bind address
	uint32_t host_ip;

	Router(const char* name);
	~Router();    
	void start();
	void start_udp_server();
	// void map_vip(void* addr);

	ShmPiece* addShmPiece(int cliend_id, int mem_size);
	ShmPiece* addShmPiece(std::string shm_name, int mem_size);

	ShmPiece* initCtrlShm(const char* tag);
	int getHandle(enum RDMA_VERBS_OBJECT object, int handle);

	uint32_t rdma_polling_interval;
	uint8_t disable_rdma;

	std::map<std::string, std::string> vip_map;
};

void HandleRequest(struct HandlerArgs *args);

int send_fd(int sock, int fd);
int recv_fd(int sock);

#endif /* ROUTER_H */