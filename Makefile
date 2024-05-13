OBJ = main.o router.o rdma_api.o shared_memory.o pd.o cq.o qp.o mr.o dump.o restore.o
CXXFLAGS = -std=c++11
router: $(OBJ)
	# g++ -o router $(OBJ) -lrdmacm -libverbs -lpthread -lrt 
	g++ -o router $(OBJ) -libverbs -lpthread -lrt
	rm *.o
main.o: main.cpp include/router.h
router.o: router.cpp include/router.h include/log.h include/rdma_api.h include/types.h
rdma_api.o: rdma_api.cpp include/rdma_api.h
shared_memory.o: shared_memory.cpp
pd.o: pd.cpp include/verbs.h include/log.h
cq.o: cq.cpp include/verbs.h include/log.h
qp.o: qp.cpp include/verbs.h include/log.h
mr.o: mr.cpp include/verbs.h include/log.h
dump.o: dump.cpp
restore.o: restore.cpp include/verbs.h include/log.h

clean:
	rm router *.o