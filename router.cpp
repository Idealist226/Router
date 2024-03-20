#include "router.h"
#include "log.h"
#include "rdma_api.h"
#include "types.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sstream>

Router::Router(const char* name) {
	LOG_INFO("Router Init");
	
	this->name = name;
	this->pathname = "/router/";
	this->pathname.append(this->name);
	this->host_ip = 0;

	if (getenv("HOST_IP_PREFIX")) {
		const char* prefix = getenv("HOST_IP_PREFIX");
		uint32_t prefix_ip = 0, prefix_mask = 0;
		uint8_t a, b, c, d, bits;
		if (sscanf(prefix, "%hhu.%hhu.%hhu.%hhu/%hhu", &a, &b, &c, &d, &bits) == 5) {
			if (bits <= 32) {
				prefix_ip = htonl(
					(a << 24UL) |
					(b << 16UL) |
					(c << 8UL) |
					(d));
				prefix_mask = htonl((0xFFFFFFFFUL << (32 - bits)) & 0xFFFFFFFFUL);
			}
		}
		if (prefix_ip != 0 || prefix_mask != 0) {
			struct ifaddrs *ifaddr, *ifa;
			getifaddrs(&ifaddr);
			ifa = ifaddr;   
			while (ifa) {
				if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
					struct sockaddr_in *pAddr = (struct sockaddr_in *)ifa->ifa_addr;
					if ((pAddr->sin_addr.s_addr & prefix_mask) == (prefix_ip & prefix_mask)) {
						this->host_ip = pAddr->sin_addr.s_addr;
						break;
					}
				}
				ifa = ifa->ifa_next;
			}
			freeifaddrs(ifaddr);
		}
	}

	if (getenv("HOST_IP")) {
		this->host_ip = inet_addr(getenv("HOST_IP"));
	}

	if (!this->host_ip) {
		LOG_ERROR("Missing HOST_IP or HOST_IP_PREFIX. Socket may not work.");
	} else {
		struct in_addr addr_tmp;
		addr_tmp.s_addr = this->host_ip;
		LOG_INFO("Socket binding address:" << inet_ntoa(addr_tmp));
	}

	if (!getenv("RDMA_POLLING_INTERVAL_US")) {
		this->rdma_polling_interval = 0;
	} else {
		this->rdma_polling_interval = atoi(getenv("RDMA_POLLING_INTERVAL_US"));
	}

	if (!getenv("DISABLE_RDMA")) {
		this->disable_rdma = 0;
	} else {
		this->disable_rdma = atoi(getenv("DISABLE_RDMA"));
	}

	LOG_DEBUG("Pathname for Unix domain socket: " << this->pathname);

	for (int i = 0; i < MAP_SIZE; i++) {
		this->pd_map[i] = NULL;
		this->cq_map[i] = NULL;
		this->qp_map[i] = NULL;
		this->mr_map[i] = NULL;
		this->ah_map[i] = NULL;
		this->srq_map[i] = NULL;
		this->channel_map[i] = NULL;
		// this->event_channel_map[i] = NULL;
		// this->cm_id_map[i] = NULL;
		// this->shmr_map[i] = NULL;
		// this->qp_shm_map[i] = NULL;
		// this->cq_shm_map[i] = NULL;
		// this->srq_shm_map[i] = NULL;
	}

	// pthread_mutex_init(&this->qp_shm_vec_mtx, NULL);
	// pthread_mutex_init(&this->cq_shm_vec_mtx, NULL);
	// pthread_mutex_init(&this->srq_shm_vec_mtx, NULL);
	// pthread_mutex_init(&this->rkey_mr_shm_mtx, NULL);
	pthread_mutex_init(&this->lkey_ptr_mtx, NULL);
	// pthread_mutex_init(&this->shm_mutex, NULL);

	if (!this->disable_rdma) {
		setup_ib(&this->rdma_data);
		LOG_DEBUG("RDMA Dev: dev.name=" << this->rdma_data.ib_device->name << ", " <<  "dev.dev_name=" << this->rdma_data.ib_device->dev_name);    
	}

	this->vip_map["10.47.0.4"] = "192.168.2.13";
	this->vip_map["10.47.0.6"] = "192.168.2.13";
	this->vip_map["10.47.0.7"] = "192.168.2.13";
	this->vip_map["10.47.0.8"] = "192.168.2.13";
	this->vip_map["10.44.0.3"] = "192.168.2.15";
	this->vip_map["10.44.0.4"] = "192.168.2.15";
	this->vip_map["10.44.0.6"] = "192.168.2.15";
}

Router::~Router()
{
	for(std::map<int, std::vector<ShmPiece*> >::iterator it = this->shm_pool.begin(); it != this->shm_pool.end(); it++) {
		for (int i = 0; i < it->second.size(); i++) {
			delete it->second[i];
		}
	}

	for(std::map<std::string, ShmPiece* >::iterator it = this->shm_map.begin(); it != this->shm_map.end(); it++) {
		delete it->second;
	}
}

ShmPiece* Router::initCtrlShm(const char* tag)
{
	std::stringstream ss;
	ss << "ctrlshm-" << tag;

	ShmPiece *sp = new ShmPiece(ss.str().c_str(), sizeof(struct CtrlShmPiece));
	if (!sp->open()) {
		sp = NULL;
		LOG_ERROR("Failed to create control shm for tag  " << tag);
	}

	memset(sp->ptr, 0, sizeof(struct CtrlShmPiece));
	struct CtrlShmPiece *csp = (struct CtrlShmPiece *)(sp->ptr);
	csp->state = IDLE;
	return sp;
}

ShmPiece* Router::addShmPiece(int client_id, int mem_size)
{
    pthread_mutex_lock(&this->shm_mutex);
    if (this->shm_pool.find(client_id) == this->shm_pool.end()) {
        std::vector<ShmPiece*> v;
        this->shm_pool[client_id] = v;
    }

    int count = this->shm_pool[client_id].size();

    std::stringstream ss;
    ss << "client-" << client_id << "-memsize-" << mem_size << "-index-" << count;

    ShmPiece *sp = new ShmPiece(ss.str().c_str(), mem_size);
    this->shm_pool[client_id].push_back(sp);
    if (!sp->open()) {
        sp = NULL;
    }
    pthread_mutex_unlock(&this->shm_mutex);
    return sp;
}

ShmPiece* Router::addShmPiece(std::string shm_name, int mem_size)
{
    pthread_mutex_lock(&this->shm_mutex);
    if (this->shm_map.find(shm_name) != this->shm_map.end()) {
        pthread_mutex_unlock(&this->shm_mutex);
        return this->shm_map[shm_name];
    }

    ShmPiece *sp = new ShmPiece(shm_name.c_str(), mem_size);
    if (!sp->open()) {
        sp = NULL;
    }

    this->shm_map[shm_name] = sp;
    pthread_mutex_unlock(&this->shm_mutex);
    return sp;
}

int Router::getHandle(enum RDMA_VERBS_OBJECT object, int handle)
{
	switch (object) {
		case IBV_PD:
			return this->pd_handle_map[handle];
			break;
		case IBV_CQ:
			return this->cq_handle_map[handle];
			break;
		case IBV_QP:
			return this->qp_handle_map[handle];
			break;
		case IBV_MR:
			return this->mr_handle_map[handle];
			break;
		default:
			break;
	}
	return -1;
}

void Router::start()
{
	LOG_INFO("Router Starting... ");

	// if (!disable_rdma) {
	// 	start_udp_server();

	// 	pthread_t ctrl_th; //the fast data path thread
	// 	struct HandlerArgs ctrl_args;
	// 	ctrl_args.ffr = this;
	// 	pthread_create(&ctrl_th, NULL, (void* (*)(void*))CtrlChannelLoop, &ctrl_args); 
	// 	sleep(1.0);
	// }

	char c;
	register int i, len;
	struct sockaddr_un saun;

	if ((this->sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		LOG_ERROR("Cannot create Unix domain socket.");
		exit(1);
	}

	saun.sun_family = AF_UNIX;
	strcpy(saun.sun_path, this->pathname.c_str());

	unlink(this->pathname.c_str());
	len = sizeof(saun.sun_family) + strlen(saun.sun_path);

	if (bind(this->sock, (const sockaddr*)&saun, len) < 0) {
		LOG_ERROR("Cannot bind Unix domain socket.");
		exit(1);
	}

	if (listen(this->sock, 128) < 0) {
		LOG_ERROR("Cannot listen Unix domain socket.");
		exit(1);
	} 

	int client_sock;
	int fromlen = sizeof(struct sockaddr_un);
	int count = 0;
	struct sockaddr_un fsaun;
	memset(&fsaun, 0, sizeof fsaun);

	LOG_DEBUG("Accepting new clients... ");

	while (1) {
		if ((client_sock = accept(this->sock, (sockaddr*)&fsaun, (socklen_t*)&fromlen)) < 0) {
			LOG_ERROR("Failed to accept." << errno);
			exit(1);
		}
		LOG_TRACE("New client with sock " << client_sock << ".");

		// Start a thread to handle the request.     
		pthread_t *pth = (pthread_t *) malloc(sizeof(pthread_t));
		struct HandlerArgs *args = (struct HandlerArgs *) malloc(sizeof(struct HandlerArgs));
		args->ffr = this;
		args->client_sock = client_sock;
		int ret = pthread_create(pth, NULL, (void* (*)(void*))HandleRequest, args);
		LOG_TRACE("result of pthread_create --> " << ret);
		count ++;
	}
}

void HandleRequest(struct HandlerArgs *args)
{
	LOG_TRACE("Start to handle the request from client sock " << args->client_sock << ".");

	Router *ffr = args->ffr;
	int client_sock = args->client_sock;

	// Speed up
	char *req_body = NULL;
	char *rsp = NULL;

	if (ffr->disable_rdma) {
		req_body = (char*)malloc(0xff);
		rsp = (char*)malloc(0xff);
	}
	else {
		req_body = (char*)malloc(0xfffff);
		rsp = (char*)malloc(0xfffff);
	}

	while(1)
	{
		int n = 0, size = 0, count = 0, i = 0, ret = 0, host_fd = -1;
		//void *req_body = NULL;
		//void *rsp = NULL;
		void *context = NULL;
		struct ibv_cq *cq = NULL;
		struct ibv_qp *qp = NULL;
		struct ibv_pd *pd = NULL;
		struct ibv_mr *mr = NULL;
		struct ibv_ah *ah = NULL;
		struct ibv_srq *srq = NULL;
		struct ibv_comp_channel *channel = NULL;
		// struct rdma_event_channel *event_channel = NULL;
		// struct rdma_cm_id *cm_id = NULL;
		ShmPiece *sp = NULL;
		struct ibv_wc *wc_list = NULL;
		struct FfrRequestHeader header;

		LOG_TRACE("Start to read from sock " << client_sock);
		
		if ((n = read(client_sock, &header, sizeof(header))) < sizeof(header)) {
			if (n < 0)
				LOG_ERROR("Failed to read the request header. Read bytes: " << n << " Size of Header: " << sizeof(header));
			goto kill;
		} else {
			LOG_TRACE("Get request cmd " << header.func);
		}

		switch (header.func) {
			case IBV_GET_CONTEXT: {
				LOG_DEBUG("GET_CONTEXT");
				//rsp = malloc(sizeof(struct IBV_GET_CONTEXT_RSP));
				size = sizeof(struct IBV_GET_CONTEXT_RSP);
				((struct IBV_GET_CONTEXT_RSP *)rsp)->async_fd = ffr->rdma_data.ib_context->async_fd;
				((struct IBV_GET_CONTEXT_RSP *)rsp)->num_comp_vectors = ffr->rdma_data.ib_context->num_comp_vectors;
			}
				break;
			case IBV_ALLOC_PD: {
				LOG_DEBUG("ALLOC_PD");
				//rsp = malloc(sizeof(struct IBV_ALLOC_PD_RSP));
				size = sizeof(struct IBV_ALLOC_PD_RSP);
				pd = ibv_alloc_pd(ffr->rdma_data.ib_context); 
				if (pd->handle >= MAP_SIZE) {
					LOG_INFO("PD handle is no less than MAX_QUEUE_MAP_SIZE. pd_handle=" << pd->handle); 
				} else {
					ffr->pd_map[pd->handle] = pd;
				}
				((struct IBV_ALLOC_PD_RSP *)rsp)->pd_handle = pd->handle;
				LOG_DEBUG("Return pd_handle " << pd->handle << " for client_id " << header.client_id);
			}
				break;
			case IBV_QUERY_PORT: {
				LOG_DEBUG("QUERY_PORT client_id=" << client_sock);
				//req_body = malloc(sizeof(struct IBV_QUERY_PORT_REQ));
				if (read(client_sock, req_body, sizeof(struct IBV_QUERY_PORT_REQ)) < sizeof(struct IBV_QUERY_PORT_REQ)) {
					LOG_ERROR("Failed to read request body.");
					goto kill;
				}

				//rsp = malloc(sizeof(struct IBV_QUERY_PORT_RSP));
				size = sizeof(struct IBV_QUERY_PORT_RSP);
				if (ibv_query_port(ffr->rdma_data.ib_context, 
					((IBV_QUERY_PORT_REQ*)req_body)->port_num, &((struct IBV_QUERY_PORT_RSP *)rsp)->port_attr) < 0) {
					LOG_ERROR("Cannot query port" << ((IBV_QUERY_PORT_REQ*)req_body)->port_num);
				}
            }
				break;
			case IBV_DEALLOC_PD: {
				LOG_DEBUG("DEALLOC_PD");

				struct IBV_DEALLOC_PD_REQ *request = (struct IBV_DEALLOC_PD_REQ*)req_body;
				//req_body = malloc(sizeof(struct IBV_DEALLOC_PD_REQ));
				if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
					LOG_ERROR("Failed to read the request body."); 
					goto kill;
				}

				LOG_DEBUG("Dealloc PD: " << request->pd_handle); 

				pd = ffr->pd_map[request->pd_handle];
				if (pd == NULL) {
					LOG_ERROR("Failed to get pd with pd_handle " << request->pd_handle);
					goto end;
				}

				ret = ibv_dealloc_pd(pd);
				ffr->pd_map[request->pd_handle] = NULL;
				//rsp = malloc(sizeof(struct IBV_DEALLOC_PD_RSP));
				((struct IBV_DEALLOC_PD_RSP *)rsp)->ret = ret;
				size = sizeof(struct IBV_DEALLOC_PD_RSP);
			}
				break;
			case IBV_CREATE_CQ: {
				LOG_INFO("CREATE_CQ, body_size=" << header.body_size);
				//req_body = malloc(sizeof(struct IBV_CREATE_CQ_REQ));
				if (read(client_sock, req_body, sizeof(struct IBV_CREATE_CQ_REQ)) < sizeof(struct IBV_CREATE_CQ_REQ)) {
					LOG_ERROR("DESTROY_CQ: Failed to read the request body."); 
					goto kill;
				}

				if (((struct IBV_CREATE_CQ_REQ *)req_body)->channel_fd < 0) {
					channel = NULL;
				} else {
					channel = ffr->channel_map[((struct IBV_CREATE_CQ_REQ *)req_body)->channel_fd];
					if (channel == NULL) {
						LOG_ERROR("Failed to get channel with fd " << ((struct IBV_CREATE_CQ_REQ *)req_body)->channel_fd);
						goto end;
					}
				}

				cq = ibv_create_cq(ffr->rdma_data.ib_context, ((struct IBV_CREATE_CQ_REQ *)req_body)->cqe, NULL, channel, ((struct IBV_CREATE_CQ_REQ *)req_body)->comp_vector);
				if (cq->handle >= MAP_SIZE) {
					LOG_INFO("CQ handle (" << cq->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					ffr->cq_map[cq->handle] = cq;
				}

				//rsp = malloc(sizeof(struct IBV_CREATE_CQ_RSP));
				((struct IBV_CREATE_CQ_RSP *)rsp)->cqe = cq->cqe;
				((struct IBV_CREATE_CQ_RSP *)rsp)->handle = cq->handle;
				size = sizeof(struct IBV_CREATE_CQ_RSP);

				LOG_DEBUG("Create CQ: cqe=" << cq->cqe << " handle=" << cq->handle);

				std::stringstream ss;
				ss << "cq" << cq->handle;
				ShmPiece* sp = ffr->initCtrlShm(ss.str().c_str());
				ffr->cq_shm_map[cq->handle] = sp;
				strcpy(((struct IBV_CREATE_CQ_RSP *)rsp)->shm_name, sp->name.c_str());
				// pthread_mutex_lock(&ffr->cq_shm_vec_mtx);
				// ffr->cq_shm_vec.push_back(cq->handle);
				// pthread_mutex_unlock(&ffr->cq_shm_vec_mtx);
			}
				break;
			case IBV_DESTROY_CQ: {
				LOG_DEBUG("DESTROY_CQ, body_size=" << header.body_size);

				struct IBV_DESTROY_CQ_REQ *request = (struct IBV_DESTROY_CQ_REQ*)req_body;
				//req_body = malloc(sizeof(struct IBV_DESTROY_CQ_REQ));
				if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
					LOG_ERROR("DESTROY_CQ: Failed to read the request body."); 
					goto kill;
				}

				LOG_DEBUG("cq_handle in request: " << request->cq_handle);

				cq = ffr->cq_map[request->cq_handle];
				if (cq == NULL) {
					LOG_ERROR("Failed to get cq with cq_handle " << request->cq_handle);
					goto end;
				}
				LOG_DEBUG("found cq from cq_map");
		
				ret = ibv_destroy_cq(cq);
				ffr->cq_map[request->cq_handle] = NULL;

				// pthread_mutex_lock(&ffr->cq_shm_vec_mtx);
				// std::vector<uint32_t>::iterator position = std::find(ffr->cq_shm_vec.begin(), ffr->cq_shm_vec.end(), request->cq_handle);
				// if (position != ffr->cq_shm_vec.end()) // == myVector.end() means the element was not found
				// 	ffr->cq_shm_vec.erase(position);
				// pthread_mutex_unlock(&ffr->cq_shm_vec_mtx); 

				ShmPiece* sp = ffr->cq_shm_map[request->cq_handle];
				if (sp)
					delete sp;

				ffr->cq_shm_map[request->cq_handle] = NULL;

				//rsp = malloc(sizeof(struct IBV_DESTROY_CQ_RSP));
				((struct IBV_DESTROY_CQ_RSP *)rsp)->ret = ret;
				size = sizeof(struct IBV_DESTROY_CQ_RSP);
			}
				break;
			// case IBV_REQ_NOTIFY_CQ:
			case IBV_CREATE_QP: {
				LOG_DEBUG("CREATE_QP");

				struct IBV_CREATE_QP_REQ *request = (struct IBV_CREATE_QP_REQ*)req_body;
				//req_body = malloc(sizeof(struct IBV_CREATE_QP_REQ));
				if ((n = read(client_sock, req_body, sizeof(*request))) < sizeof(*request)) {
					LOG_ERROR("CREATE_CQ: Failed to read the request body."); 
					goto kill;
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
				LOG_DEBUG("init_attr.send_cq=" << request->send_cq_handle); 
				LOG_DEBUG("init_attr.recv_cq=" << request->recv_cq_handle); 
				LOG_TRACE("init_attr.cap.max_send_wr=" << init_attr.cap.max_send_wr); 
				LOG_TRACE("init_attr.cap.max_recv_wr=" << init_attr.cap.max_recv_wr); 
				LOG_TRACE("init_attr.cap.max_send_sge=" << init_attr.cap.max_send_sge); 
				LOG_TRACE("init_attr.cap.max_recv_sge=" << init_attr.cap.max_recv_sge); 
				LOG_TRACE("init_attr.cap.max_inline_data=" << init_attr.cap.max_inline_data); 
							
				pd = ffr->pd_map[request->pd_handle];
				LOG_TRACE("Get pd " << pd << "from pd_handle " << request->pd_handle);             
			
				qp = ibv_create_qp(pd, &init_attr);
				if (qp == NULL) {
					LOG_ERROR("Failed to create a QP.");             
					goto end;
				}

				if (qp->handle >= MAP_SIZE) {
					LOG_ERROR("[Warning] QP handle (" << qp->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					ffr->qp_map[qp->handle] = qp;
					ffr->qp_handle_map[qp->handle] = qp->handle;
				}

				struct IBV_CREATE_QP_RSP *response = (struct IBV_CREATE_QP_RSP*)rsp;
				response->qp_num = qp->qp_num;
				response->handle = qp->handle;

				LOG_TRACE("qp->qp_num=" << qp->qp_num);
				LOG_TRACE("qp->handle=" << qp->handle); 

				response->cap.max_send_wr = init_attr.cap.max_send_wr;
				response->cap.max_recv_wr = init_attr.cap.max_recv_wr;
				response->cap.max_send_sge = init_attr.cap.max_send_sge;
				response->cap.max_recv_sge = init_attr.cap.max_recv_sge;
				response->cap.max_inline_data = init_attr.cap.max_inline_data;

				size = sizeof(struct IBV_CREATE_QP_RSP);    

				std::stringstream ss;
				ss << "qp" << qp->handle;
				ShmPiece* sp = ffr->initCtrlShm(ss.str().c_str());
				ffr->qp_shm_map[qp->handle] = sp;
				strcpy(response->shm_name, sp->name.c_str());
				// pthread_mutex_lock(&ffr->qp_shm_vec_mtx);
				// ffr->qp_shm_vec.push_back(qp->handle);
				// pthread_mutex_unlock(&ffr->qp_shm_vec_mtx);
			}
				break;
			case IBV_DESTROY_QP: {
				LOG_DEBUG("DESTROY_QP");

				struct IBV_DESTROY_QP_REQ *request = (struct IBV_DESTROY_QP_REQ*)req_body;
				//req_body = malloc(sizeof(struct IBV_DESTROY_QP_REQ));
				if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
					LOG_ERROR("Failed to read the request body."); 
					goto kill;
				}

				int qp_handle = ffr->getHandle(IBV_QP, request->qp_handle);
				LOG_TRACE("Destroy QP: " << qp_handle); 

				qp = ffr->qp_map[qp_handle];
				if (qp == NULL) {
					LOG_ERROR("Failed to get qp with qp_handle " << qp_handle);
					goto end;
				}

				ret = ibv_destroy_qp(qp);
				ffr->qp_map[qp_handle] = NULL;

				// pthread_mutex_lock(&ffr->qp_shm_vec_mtx);
				// std::vector<uint32_t>::iterator position = std::find(ffr->qp_shm_vec.begin(), ffr->qp_shm_vec.end(), request->qp_handle);
				// if (position != ffr->qp_shm_vec.end()) // == myVector.end() means the element was not found
				// 	ffr->qp_shm_vec.erase(position);
				// pthread_mutex_unlock(&ffr->qp_shm_vec_mtx); 

				ShmPiece* sp = ffr->qp_shm_map[qp_handle];
				if (sp)
					delete sp;

				ffr->qp_shm_map[qp_handle] = NULL;

				//rsp = malloc(sizeof(struct IBV_DESTROY_QP_RSP));
				((struct IBV_DESTROY_QP_RSP *)rsp)->ret = ret;
				size = sizeof(struct IBV_DESTROY_QP_RSP);
			}
				break;
			case IBV_REG_MR: {
				LOG_DEBUG("REG_MR");
				//req_body = malloc(sizeof(struct IBV_REG_MR_REQ));
				if (read(client_sock, req_body, sizeof(struct IBV_REG_MR_REQ)) < sizeof(struct IBV_REG_MR_REQ)) {
					LOG_ERROR("REG_MR: Failed to read request body.");
					goto kill;
				}

				struct IBV_REG_MR_REQ *request = (struct IBV_REG_MR_REQ *)req_body;
				// create a shm buffer
				LOG_TRACE("Create a shared memory piece for client " << header.client_id << " with size " << request->mem_size);
				if (request->shm_name[0] == '\0') {
					LOG_TRACE("create shm from client id and count.");
					sp = ffr->addShmPiece(header.client_id, request->mem_size);
				} else {
					LOG_TRACE("create shm from name: " << request->shm_name);
					sp = ffr->addShmPiece(request->shm_name, request->mem_size);
				}
				
				if (sp == NULL) {
					LOG_ERROR("Failed to the shared memory piece.");
					goto end;
				}

				LOG_TRACE("Looking for PD with pd_handle " << request->pd_handle);
				pd = ffr->pd_map[request->pd_handle];
				if (pd == NULL) {
					LOG_ERROR("Failed to get pd with pd_handle " << request->pd_handle);
					goto end;
				}

				LOG_DEBUG("Registering a MR ptr="<< sp->ptr << ", size="  << sp->size);            
				mr = ibv_reg_mr(pd, sp->ptr, sp->size, request->access_flags);
				if (mr == NULL) {
					LOG_ERROR("Failed to regiester the MR. Current shared memory size: " << sp->size);
					goto end;
				}

				if (mr->handle >= MAP_SIZE) {
					LOG_ERROR("[Warning] MR handle (" << mr->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					ffr->shmr_map[mr->handle] = sp;
					ffr->mr_map[mr->handle] = mr;
				}

				//rsp = malloc(sizeof(struct IBV_REG_MR_RSP));
				struct IBV_REG_MR_RSP *response = (struct IBV_REG_MR_RSP *)rsp;
				size = sizeof(struct IBV_REG_MR_RSP);
				response->handle = mr->handle;
				response->lkey = mr->lkey;
				response->rkey = mr->rkey;
				strcpy(response->shm_name, sp->name.c_str());
			
				LOG_TRACE("mr->handle=" << mr->handle);
				LOG_TRACE("mr->lkey=" << mr->lkey);
				LOG_TRACE("mr->rkey=" << mr->rkey);
				LOG_TRACE("shm_name=" << sp->name.c_str());

				// store lkey to ptr mapping
				pthread_mutex_lock(&ffr->lkey_ptr_mtx);
				ffr->lkey_ptr[mr->lkey] = sp->ptr;
				pthread_mutex_unlock(&ffr->lkey_ptr_mtx);
			}
				break;
			case IBV_REG_MR_MAPPING: {
				// LOG_DEBUG("REG_MR_MAPPING");
				// //req_body = malloc(sizeof(struct IBV_REG_MR_MAPPING_REQ));
				// if (read(client_sock, req_body, sizeof(struct IBV_REG_MR_MAPPING_REQ)) < sizeof(struct IBV_REG_MR_MAPPING_REQ)) {
				// 	LOG_ERROR("REG_MR_MAPPING: Failed to read request body.");
				// 	goto kill;
				// }

				// struct IBV_REG_MR_MAPPING_REQ *p = (struct IBV_REG_MR_MAPPING_REQ*)req_body;
				
				// pthread_mutex_lock(&ffr->lkey_ptr_mtx);
				// p->shm_ptr = (char*)(ffr->lkey_ptr[p->key]);
				// pthread_mutex_unlock(&ffr->lkey_ptr_mtx);

				// struct sockaddr_in si_other, si_self;
				// struct sockaddr src_addr;
				// socklen_t addrlen;
				// char recv_buff[1400];
				// ssize_t recv_buff_size;
				// int s, i, slen=sizeof(si_other);

				// srand (client_sock);

				// for (int i = 0; i < HOST_NUM; i++) {
				// 	if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
				// 		LOG_ERROR("Error in creating socket for UDP client");
				// 		return;
				// 	}

				// 	memset((char *) &si_other, 0, sizeof(si_other));
				// 	si_other.sin_family = AF_INET;
				// 	si_other.sin_port = htons(UDP_PORT);


				// 	memset((char *) &si_self, 0, sizeof(si_self));
				// 	si_self.sin_family = AF_INET;
				// 	int self_p = 0;//2000 + rand() % 40000;
				// 	si_self.sin_port = htons(self_p);

				// 	if (inet_aton("0.0.0.0", &si_self.sin_addr)==0) {
				// 		LOG_ERROR("Error in creating socket for UDP client self.");
				// 		continue;
				// 	}

				// 	if (bind(s, (const struct sockaddr *)&si_self, sizeof(si_self)) < 0) {
				// 		LOG_ERROR("Failed to bind UDP. errno=" << errno);
				// 		continue;
				// 	}

				// 	if (inet_aton(HOST_LIST[i], &si_other.sin_addr)==0) {
				// 		LOG_ERROR("Error in creating socket for UDP client other.");
				// 		continue;
				// 	}

				// 	if (sendto(s, req_body, sizeof(struct IBV_REG_MR_MAPPING_REQ), 0, (const sockaddr*)&si_other, slen)==-1) {
				// 		LOG_DEBUG("Error in sending MR mapping to " << HOST_LIST[i]);
				// 	} else {
				// 		LOG_TRACE("Sent MR mapping to " << HOST_LIST[i]);
				// 	}

				// 	if ((recv_buff_size = recvfrom(s, recv_buff, 1400, 0, (sockaddr*)&si_other, (socklen_t*)&slen)) == -1) {
				// 		LOG_ERROR("Error in receiving MR mapping ack" << HOST_LIST[i]);
				// 	} else {
				// 		char src_str[INET_ADDRSTRLEN];
				// 		inet_ntop(AF_INET, &si_other.sin_addr, src_str, sizeof src_str);
				// 		int src_port = ntohs(si_other.sin_port);
				// 		LOG_INFO("## ACK from " << HOST_LIST[i] << "/" << src_str << ":" << src_port << "ack-rkey=" << recv_buff <<  " rkey= " << p->key);
				// 	}

				// 	close(s);
				// }
				// size = sizeof(struct IBV_REG_MR_MAPPING_RSP);
				// ((struct IBV_REG_MR_MAPPING_RSP *)rsp)->ret = 0;
				// break;
			}
			case IBV_DEREG_MR: {
				LOG_DEBUG("DEREG_MR");

				struct IBV_DEREG_MR_REQ *request = (struct IBV_DEREG_MR_REQ*)req_body;
				//req_body = malloc(sizeof(struct IBV_DEREG_MR_REQ));
				if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
					LOG_ERROR("DEREG_MR: Failed to read request body.");
					goto kill;
				}

				sp = ffr->shmr_map[request->handle];
				mr = ffr->mr_map[request->handle];

				ret = ibv_dereg_mr(mr);
				if (sp)
					delete sp;
				ffr->shmr_map[request->handle] = NULL;
				ffr->mr_map[request->handle] = NULL;

				//rsp = malloc(sizeof(struct IBV_DEREG_MR_RSP));
				size = sizeof(struct IBV_DEREG_MR_RSP);
				((struct IBV_DEREG_MR_RSP *)rsp)->ret = ret;
			}
				break;
			case IBV_MODIFY_QP: {
				LOG_TRACE("MODIFY_QP");

				//req_body = malloc(sizeof(struct IBV_MODIFY_QP_REQ));
				if (read(client_sock, req_body, sizeof(struct IBV_MODIFY_QP_REQ)) < sizeof(struct IBV_MODIFY_QP_REQ)) {
					LOG_ERROR("MODIFY_QP: Failed to read request body.");
					goto kill;
				}

				struct IBV_MODIFY_QP_REQ *request = (struct IBV_MODIFY_QP_REQ *)req_body;
				LOG_TRACE("QP handle to modify: " << request->handle);

				if (request->handle >= MAP_SIZE) {
					LOG_ERROR("QP handle (" << qp->handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					qp = ffr->qp_map[request->handle];
				}

				int ret = 0;
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

				if ((ret = ibv_modify_qp(qp, &request->attr, request->attr_mask)) != 0) {
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
				size = sizeof(struct IBV_MODIFY_QP_RSP);
				((struct IBV_MODIFY_QP_RSP *)rsp)->ret = ret;
				((struct IBV_MODIFY_QP_RSP *)rsp)->handle = request->handle;
			}
				break;
			// case IBV_QUERY_QP:
			case IBV_POST_SEND: {
				LOG_INFO("POST_SEND");
				//req_body = malloc(header.body_size);
				if (read(client_sock, req_body, header.body_size) < header.body_size) {
					LOG_ERROR("POST_SEND: Error in reading in post send.");
					goto end;
				}

				// Now recover the qp and wr
				struct ib_uverbs_post_send *post_send = (struct ib_uverbs_post_send*)req_body;
				if (post_send->qp_handle >= MAP_SIZE) {
					LOG_ERROR("[Warning] QP handle (" << post_send->qp_handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					qp = ffr->qp_map[post_send->qp_handle];
					// tb = ffr->tokenbucket[post_send->qp_handle];
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

					// if (wr[i].opcode == IBV_WR_RDMA_WRITE || wr[i].opcode == IBV_WR_RDMA_WRITE_WITH_IMM || wr[i].opcode == IBV_WR_RDMA_READ) {
					// 	if (ffr->rkey_mr_shm.find(wr[i].wr.rdma.rkey) == ffr->rkey_mr_shm.end()) {
					// 		LOG_ERROR("One sided opertaion: can't find remote MR. rkey --> " << wr[i].wr.rdma.rkey << "  addr --> " << wr[i].wr.rdma.remote_addr);
					// 	}
					// 	else {
					// 		LOG_DEBUG("shm:" << (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].shm_ptr) << " app:" << (uint64_t)(wr[i].wr.rdma.remote_addr) << " mr:" << (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].mr_ptr));
					// 		wr[i].wr.rdma.remote_addr = (uint64_t)(ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].shm_ptr) + (uint64_t)wr[i].wr.rdma.remote_addr - (uint64_t)ffr->rkey_mr_shm[wr[i].wr.rdma.rkey].mr_ptr;
					// 	}
					// }

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
							LOG_DEBUG("wr[i].wr_id=" << wr[i].wr_id << " qp_num=" << qp->qp_num << " sge.addr=" << sge[j].addr << " sge.length" << sge[j].length << " opcode=" << wr[i].opcode);
							sge[j].addr = (uint64_t)((char*)(ffr->lkey_ptr[sge[j].lkey]) + sge[j].addr);
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
				size = sizeof(struct IBV_POST_SEND_RSP);
				IBV_POST_SEND_RSP *response = (IBV_POST_SEND_RSP*)rsp;
				//rsp = malloc(sizeof(struct IBV_POST_SEND_RSP));

				response->ret_errno = ibv_post_send(qp, wr, &bad_wr);
				if (response->ret_errno != 0) {
					LOG_ERROR("[Error] Post send (" << qp->handle << ") fails.");
				}

				LOG_DEBUG("post_send success.");

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
			}
				break;
			case IBV_POST_RECV: {
				LOG_TRACE("IBV_POST_RECV");
				//req_body = malloc(header.body_size);
				if (read(client_sock, req_body, header.body_size) < header.body_size) {
					LOG_ERROR("POST_RECV: Error in reading in post recv.");
					goto end;
				}

				// Now recover the qp and wr
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
				size = sizeof(*response);
				response->ret_errno = ibv_post_recv(qp, wr, &bad_wr);
				if (response->ret_errno != 0) {
					LOG_ERROR("[Error] Post recv (" << qp->handle << ") fails.");
				}
				if (bad_wr == NULL) {
					response->bad_wr = 0;
				} else {
					response->bad_wr = bad_wr - wr;
				}
			}
				break;
			case IBV_POLL_CQ: {
				LOG_TRACE("IBV_POLL_CQ");

				struct IBV_POLL_CQ_REQ *request = (struct IBV_POLL_CQ_REQ *)req_body;
				//req_body = malloc(sizeof(struct IBV_POLL_CQ_REQ));
				if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
					LOG_ERROR("POLL_CQ: Failed to read request body.");
					goto kill;
				}

				LOG_TRACE("CQ handle to poll: " << request->cq_handle);
			
				if (request->cq_handle >= MAP_SIZE) {
					LOG_ERROR("CQ handle (" << request->cq_handle << ") is no less than MAX_QUEUE_MAP_SIZE.");
				} else {
					cq = ffr->cq_map[request->cq_handle];
				}

				if (cq == NULL) {
					LOG_ERROR("cq pointer is NULL.");
					goto end;
				}

				//rsp = malloc(sizeof(struct FfrResponseHeader) + request->ne * sizeof(struct ibv_wc));
				wc_list = (struct ibv_wc*)((char *)rsp + sizeof(struct FfrResponseHeader));
				count = ibv_poll_cq(cq, request->ne, wc_list);

				if (count <= 0) {
					LOG_TRACE("The return of ibv_poll_cq is " << count);
					size = sizeof(struct FfrResponseHeader);
					((struct FfrResponseHeader*)rsp)->rsp_size = 0;    
				} else {
					LOG_DEBUG("ibv_poll_cq return 1");
					size = sizeof(struct FfrResponseHeader) + count * sizeof(struct ibv_wc);
					((struct FfrResponseHeader*)rsp)->rsp_size = count * sizeof(struct ibv_wc);
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
			}
				break;
			case IBV_RESTORE_QP: {
				// LOG_TRACE("IBV_RESTORE_QP");

				// struct IBV_RESTORE_QP_REQ *request = (struct IBV_RESTORE_QP_REQ *)req_body;
				// //req_body = malloc(sizeof(struct IBV_RESTORE_QP_REQ));
				// if (read(client_sock, req_body, sizeof(*request)) < sizeof(*request)) {
				// 	LOG_ERROR("RESTORE_QP: Failed to read request body.");
				// 	goto kill;
				// }

				// struct ibv_qp_init_attr qp_init_attr;
				// memset(&qp_init_attr, 0, sizeof(qp_init_attr));
				// qp_init_attr.qp_type = IBV_QPT_RC;
				// qp_init_attr.sq_sig_all = 1;
				// int send_cq_handle = ffr->getHandle(IBV_CQ, request->send_cq_handle);
				// int recv_cq_handle = ffr->getHandle(IBV_CQ, request->recv_cq_handle);
				// qp_init_attr.send_cq = ffr->cq_map[send_cq_handle];
				// qp_init_attr.recv_cq = ffr->cq_map[recv_cq_handle];
				// qp_init_attr.cap.max_send_wr = 1;
				// qp_init_attr.cap.max_recv_wr = 1;
				// qp_init_attr.cap.max_send_sge = 1;
				// qp_init_attr.cap.max_recv_sge = 1;
				// int pd_handle = ffr->getHandle(IBV_PD, request->pd_handle);
				// struct ibv_qp *test_qp = ibv_create_qp(ffr->pd_map[pd_handle], &qp_init_attr);

				// move_qp_to_init(test_qp);
				// struct ib_conn_data dest;
				// dest.out_reads = 1;
				// dest.psn = 0;
				// dest.lid = request->lid;
				// dest.qpn = request->qpn;
				// dest.gid = request->gid;
				// move_qp_to_rtr(test_qp, &dest);
				// move_qp_to_rts(test_qp);

				// ffr->qp_map[test_qp->handle] = test_qp;
				// ffr->qp_handle_map[request->qp_handle] = test_qp->handle;

				// struct IBV_RESTORE_QP_RSP *response = (struct IBV_RESTORE_QP_RSP *)rsp;
				// std::stringstream ss;
				// ss << "qp" << test_qp->handle;
				// ShmPiece* sp = ffr->initCtrlShm(ss.str().c_str());
				// ffr->qp_shm_map[test_qp->handle] = sp;
				// strcpy(response->shm_name, sp->name.c_str());

				// size = sizeof(struct IBV_RESTORE_QP_RSP);
				// ((struct IBV_RESTORE_QP_RSP *)rsp)->handle = request->handle;
			}
				break;
			default:
				break;
		}

		LOG_TRACE("write rsp " << size << " bytes to sock " << client_sock);
		if ((n = write(client_sock, rsp, size)) < size) {
			LOG_ERROR("Error in writing bytes" << n);
			goto kill;
		}

		if (header.func == SOCKET_SOCKET || header.func == SOCKET_ACCEPT || header.func == SOCKET_ACCEPT4) {
			if (((struct SOCKET_SOCKET_RSP *)rsp)->ret >= 0) {
				if (send_fd(client_sock, ((struct SOCKET_SOCKET_RSP *)rsp)->ret) < 0)
					LOG_ERROR("failed to send_fd for socket.");
				close(((struct SOCKET_SOCKET_RSP *)rsp)->ret);
			}
		}

end:
		if (host_fd >= 0) {
			close(host_fd);
		}
		if (header.func == SOCKET_SOCKET || header.func == SOCKET_BIND || 
			header.func == SOCKET_ACCEPT || header.func == SOCKET_ACCEPT4 ||
			header.func == SOCKET_CONNECT) {
			break;
		}
	}

kill:
	close(client_sock);
	free(args);
	free(rsp);
	free(req_body);
}

int send_fd(int sock, int fd)
{
	ssize_t     size;
	struct msghdr   msg;
	struct iovec    iov;
	union {
		struct cmsghdr  cmsghdr;
		char        control[CMSG_SPACE(sizeof (int))];
	} cmsgu;
	struct cmsghdr  *cmsg;
	char buf[2];

	iov.iov_base = buf;
	iov.iov_len = 2;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (fd != -1) {
		msg.msg_control = cmsgu.control;
		msg.msg_controllen = sizeof(cmsgu.control);

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof (int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;

		//printf ("passing fd %d\n", fd);
		*((int *) CMSG_DATA(cmsg)) = fd;
	} else {
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		//printf ("not passing fd\n");
	}

	size = sendmsg(sock, &msg, 0);

	if (size < 0) {
		perror ("sendmsg");
	}
	return size;
}

int recv_fd(int sock)
{
	ssize_t size;
	struct msghdr msg;
	struct iovec iov;
	union {
		struct cmsghdr cmsghdr;
		char control[CMSG_SPACE(sizeof (int))];
	} cmsgu;
	struct cmsghdr *cmsg;
	char buf[2];
	int fd = -1;

	iov.iov_base = buf;
	iov.iov_len = 2;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgu.control;
	msg.msg_controllen = sizeof(cmsgu.control);
	size = recvmsg (sock, &msg, 0);
	if (size < 0) {
		perror ("recvmsg");
		return -1;
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
		if (cmsg->cmsg_level != SOL_SOCKET) {
			fprintf (stderr, "invalid cmsg_level %d\n",
					cmsg->cmsg_level);
			return -1;
		}
		if (cmsg->cmsg_type != SCM_RIGHTS) {
			fprintf (stderr, "invalid cmsg_type %d\n",
					cmsg->cmsg_type);
			return -1;
		}
		int *fd_p = (int *)CMSG_DATA(cmsg);
		fd = *fd_p;
		// printf ("received fd %d\n", fd);
	} else {
		fd = -1;
	}

	return(fd);  
}