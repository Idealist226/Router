#include "router.h"

int main (int argc, char **argv)
{
	const char *router_name = NULL;
	if (argc < 2) {
		printf("WARNING: router name not specified. Using \"router\"\n");
		router_name = "router";
	}
	else {
		router_name = argv[1];
	}

	Router ffr(router_name);
	ffr.start();
}