#include "include/log.h"
#include "include/rdma_api.h"
#include "include/verbs.h"

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
	pthread_mutex_init(&this->rkey_mr_shm_mtx, NULL);
	pthread_mutex_init(&this->lkey_ptr_mtx, NULL);
	pthread_mutex_init(&this->shm_mutex, NULL);

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
	LOG_DEBUG("Create control shm " << ss.str().c_str());
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

void Router::start()
{
	LOG_INFO("Router Starting... ");

	if (!disable_rdma) {
		start_udp_server();

	// 	pthread_t ctrl_th; //the fast data path thread
	// 	struct HandlerArgs ctrl_args;
	// 	ctrl_args.ffr = this;
	// 	pthread_create(&ctrl_th, NULL, (void* (*)(void*))CtrlChannelLoop, &ctrl_args); 
	// 	sleep(1.0);
	}

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
	} else {
		req_body = (char*)malloc(0xfffff);
		rsp = (char*)malloc(0xfffff);
	}

	while(1)
	{
		int n = 0, size = 0, host_fd = -1;
		void *context = NULL;
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
				LOG_DEBUG("===GET_CONTEXT===");
				//rsp = malloc(sizeof(struct IBV_GET_CONTEXT_RSP));
				size = sizeof(struct IBV_GET_CONTEXT_RSP);
				((struct IBV_GET_CONTEXT_RSP *)rsp)->async_fd = ffr->rdma_data.ib_context->async_fd;
				((struct IBV_GET_CONTEXT_RSP *)rsp)->num_comp_vectors = ffr->rdma_data.ib_context->num_comp_vectors;
			}
				break;
			case IBV_ALLOC_PD:
				size = ib_uverbs_alloc_pd(ffr, rsp);
				break;
			case IBV_QUERY_PORT: {
				LOG_DEBUG("===QUERY_PORT===");
				//req_body = malloc(sizeof(struct IBV_QUERY_PORT_REQ));
				if (read(client_sock, req_body, sizeof(struct IBV_QUERY_PORT_REQ)) < sizeof(struct IBV_QUERY_PORT_REQ)) {
					LOG_ERROR("Failed to read request body.");
					goto kill;
				}

				//rsp = malloc(sizeof(struct IBV_QUERY_PORT_RSP));
				size = sizeof(struct IBV_QUERY_PORT_RSP);
				memcpy(&((struct IBV_QUERY_PORT_RSP *)rsp)->port_attr, &(ffr->rdma_data.ib_port_attr), sizeof(struct ibv_port_attr));
				// if (ibv_query_port(ffr->rdma_data.ib_context, 
				// 	((IBV_QUERY_PORT_REQ*)req_body)->port_num, &((struct IBV_QUERY_PORT_RSP *)rsp)->port_attr) < 0) {
				// 	LOG_ERROR("Cannot query port" << ((IBV_QUERY_PORT_REQ*)req_body)->port_num);
				// }
            }
				break;
			case IBV_DEALLOC_PD:
				size = ib_uverbs_dealloc_pd(ffr, client_sock, req_body, rsp);
				break;
			case IBV_CREATE_CQ:
				size = ib_uverbs_create_cq(ffr, client_sock, req_body, rsp);
				break;
			case IBV_DESTROY_CQ:
				size = ib_uverbs_destroy_cq(ffr, client_sock, req_body, rsp);
				break;
			// case IBV_REQ_NOTIFY_CQ:
			case IBV_CREATE_QP:
				size = ib_uverbs_create_qp(ffr, client_sock, req_body, rsp);
				break;
			case IBV_DESTROY_QP:
				size = ib_uverbs_destroy_qp(ffr, client_sock, req_body, rsp);
				break;
			case IBV_REG_MR:
				size = ib_uverbs_reg_mr(ffr, client_sock, req_body, rsp, header.client_id);
				break;
			case IBV_DEREG_MR:
				size = ib_uverbs_dereg_mr(ffr, client_sock, req_body, rsp);
				break;
			case IBV_MODIFY_QP:
				size = ib_uverbs_modify_qp(ffr, client_sock, req_body, rsp);
				break;
			// case IBV_QUERY_QP:
			case IBV_POST_SEND:
				size = ib_uverbs_post_send(ffr, client_sock, req_body, rsp, header.body_size);
				break;
			case IBV_POST_RECV:
				size = ib_uverbs_post_recv(ffr, client_sock, req_body, rsp, header.body_size);
				break;
			case IBV_POLL_CQ:
				size = ib_uverbs_poll_cq(ffr, client_sock, req_body, rsp);
				break;
			case IBV_DUMP_OBJECTS:
				size = ib_uverbs_dump_objects(ffr, client_sock, req_body, rsp);
				break;
			case IBV_RESTORE_OBJECTS:
				size = ib_uverbs_restore_objects(ffr, client_sock, req_body, rsp);
				break;
			default:
				break;
		}

		if (size == -1) {
			goto kill;
		} else if (size == -2) {
			goto end;
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

void* udp_mr_map(void* param) {
	LOG_TRACE("===udp_mr_map: started===");

	int s;
	struct sockaddr_in si_me, si_other;
	struct IBV_REG_MR_MAPPING_REQ buf;
	socklen_t slen = sizeof(si_other);
	struct Router *ffr = ((struct HandlerArgs *)param)->ffr;
	
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		LOG_ERROR("udp_mr_map: Error in creating socket for UDP server");
		return NULL;
	}
	memset(&si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(MR_MAP_PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (const sockaddr*)&si_me, sizeof(si_me))==-1){
		LOG_ERROR("udp_mr_map: Error in binding UDP port");
		return NULL;
	}
	for (;;) {
		if (recvfrom(s, &buf, sizeof(buf), 0, (sockaddr*)&si_other, &slen)==-1) {
			LOG_DEBUG("udp_mr_map: Error in receiving UDP packets");
			return NULL;
		}
		
		struct MR_SHM mr_shm;
		mr_shm.mr_ptr = buf.mr_ptr;
		mr_shm.shm_ptr = buf.shm_ptr;

		pthread_mutex_lock(&(ffr->rkey_mr_shm_mtx));
		ffr->rkey_mr_shm[buf.key] = mr_shm;
		pthread_mutex_unlock(&(ffr->rkey_mr_shm_mtx));

		char src_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &si_other.sin_addr, src_str, sizeof(src_str));
		int self_p = ntohs(si_other.sin_port);
		LOG_DEBUG("udp_mr_map: Receive MR Mapping: rkey=" << (uint32_t)(buf.key) << " mr=" << (uint64_t)(buf.mr_ptr) << " shm=" << (uint64_t)(buf.shm_ptr) << " from " << src_str << ":" << self_p);

		char ack[100];
		sprintf(ack, "ack-%u", buf.key);
		if (sendto(s, ack, 100, 0, (const sockaddr*)&si_other, slen)==-1) {
			LOG_ERROR("udp_mr_map: Error in sending MR mapping to " << src_str);
		}
	}
	return NULL;
}

void* udp_restore(void* param) {
	LOG_TRACE("===udp_restore: started===");

	int s;
	struct sockaddr_in si_me, si_other;
	socklen_t slen = sizeof(si_other);
	IBV_RECONNECT_QP_REQ buf;
	
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		LOG_ERROR("udp_restore: Error in creating socket for UDP server");
		return NULL;
	}
	memset(&si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(RESTORE_PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (const sockaddr*)&si_me, sizeof(si_me)) < 0){
		LOG_ERROR("udp_restore: Error in binding UDP port");
		return NULL;
	}

	while (1) {
		memset(&buf, 0, sizeof(buf));
		if (recvfrom(s, &buf, sizeof(buf), 0, (sockaddr*)&si_other, &slen) < 0) {
			LOG_ERROR("udp_restore: Error in receiving UDP packets");
			return NULL;
		}
		LOG_DEBUG("udp_restore: Received RECONNECT_QP_REQ from " << inet_ntoa(si_other.sin_addr) << ":" << ntohs(si_other.sin_port));

		int qp_handle, pd_handle, i;
		Router *ffr = ((struct HandlerArgs *)param)->ffr;
		ibv_qp *origin_qp, *new_qp;
		struct ibv_dump_qp dump_qp;

		// 判断如果不是发给自己的，就不做处理
		if (memcmp(&(ffr->rdma_data.ib_gid), &(buf.recv_gid), 16) != 0) {
			LOG_DEBUG("udp_restore: It wasn't sent to me.");
			int flag = -1;
			if (sendto(s, &flag, sizeof(flag), 0, (const sockaddr*)&si_other, slen) < 0) {
				LOG_ERROR("udp_restore: Error in sending -1 to " << inet_ntoa(si_other.sin_addr) << ":" << ntohs(si_other.sin_port));
			} else {
				LOG_DEBUG("udp_restore: Sent -1 to " << inet_ntoa(si_other.sin_addr) << ":" << ntohs(si_other.sin_port));
			}
			continue;
		}

		// 查找 origin qp 并 dump 数据
		for (i = 0; i < MAP_SIZE; i++) {
			origin_qp = ffr->qp_map[i];
			if (origin_qp != NULL && origin_qp->qp_num == buf.recv_qpn) {
				LOG_DEBUG("udp_restore: find origin_qp");
				ib_uverbs_dump_qp(&dump_qp.obj, origin_qp);
				break;
			}
		}
		if (i == MAP_SIZE || origin_qp == NULL) {
			LOG_ERROR("udp_restore: not found origin_qp");
			continue;
		}

		// 删除 origin qp
		ibv_destroy_qp(origin_qp);

		// TODO: 尝试是否可以不用 create a new qp，而是直接转换状态？先转换为 RST
		// 创建新的 qp 和对端通信
		new_qp = ib_uverbs_recreate_qp(ffr, &dump_qp);
		if (new_qp == NULL) {
			LOG_ERROR("udp_restore: not found origin_qp");
			continue;
		}

		// 发送本端新 qp 信息给对方
		LOG_DEBUG("udp_restore: 发送本端 QP 信息给对方");
		if (sendto(s, &(new_qp->qp_num), sizeof(new_qp->qp_num), 0, (const sockaddr*)&si_other, slen) < 0) {
			LOG_ERROR("udp_restore: Error in sending RECONNECT_QP_RSP to " << inet_ntoa(si_other.sin_addr) << ":" << ntohs(si_other.sin_port));
		} else {
			LOG_DEBUG("udp_restore: Sent RECONNECT_QP_RSP to " << inet_ntoa(si_other.sin_addr) << ":" << ntohs(si_other.sin_port));
		}

		// 修改 qp 状态
		dump_qp.attr.ah_attr.dlid = buf.send_lid;
		dump_qp.attr.ah_attr.grh.dgid = buf.send_gid;
		ib_uverbs_remodify_qp(&dump_qp, new_qp, buf.send_qpn);
	}
	return NULL;
}

void Router::start_udp_server() {
	pthread_t *pth = (pthread_t *) malloc(sizeof(pthread_t));
	pthread_t *pth2 = (pthread_t *) malloc(sizeof(pthread_t));
	struct HandlerArgs *args = (struct HandlerArgs *) malloc(sizeof(struct HandlerArgs));
	args->ffr = this;
	int ret = pthread_create(pth, NULL, udp_mr_map, args);
	LOG_DEBUG("result of udp_mr_map --> " << ret);
	ret = pthread_create(pth2, NULL, udp_restore, args);
	LOG_DEBUG("result of udp_restore --> " << ret);
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