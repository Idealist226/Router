# RDMA Traffic Orchestrator

This is a middle layer for intercepting RDMA Verbs. You can use it to enhance the control of the RDMA network data layer.


This application should be run in a container, please make sure you have Docker installed. I provide an image, you can get it with the following command:
```shell
docker push idealist226/router:latest
```

Clone this project and put it in the root directory.

```shell
git clone git@github.com:Idealist226/Router.git /
```

Start the container using the following command:

```shell
docker run -itd --name router1 --net host -e "ROUTER_NAME=router1" \
    -e "LD_LIBRARY_PATH=/usr/lib/:/usr/local/lib/:/usr/lib64/" \
	-v /sys/class/:/sys/class/ -v /router:/router -v /dev/:/dev/ \
    --privileged idealist226/router:latest /bin/bash
```

Then log into the router container with:
```shell
docker exec -it router1 bash
```

Now we can compile this project using the `make` command in the container and run it.
```shell
cd /router && make
./router router1
```