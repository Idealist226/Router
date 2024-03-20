OBJ = main.o router.o rdma_api.o shared_memory.o
CXXFLAGS = -std=c++11
router: $(OBJ)
	# g++ -o router $(OBJ) -lrdmacm -libverbs -lpthread -lrt 
	g++ -o router $(OBJ) -libverbs -lpthread -lrt
main.o: main.cpp router.h
router.o: router.cpp router.h log.h rdma_api.h types.h
rdma_api.o: rdma_api.cpp rdma_api.h
shared_memory.o: shared_memory.cpp

clean:
	rm router *.o