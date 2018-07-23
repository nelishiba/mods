#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "myheader.h"

int main(int argc, char *argv[])
{
	int sock;
	int err;
	struct sockaddr_in l_addr;
	struct sockaddr_in r_addr;

	if (argc != 2) {
		fprintf(stderr, "usage: %s remote_addr\n", argv[0]);
		exit(EXIT_FAILURE);
	}


	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	r_addr.sin_family = AF_INET;
	r_addr.sin_port = htons(PORT);
	inet_aton(argv[1], &r_addr.sin_addr);

	err = connect(sock, (struct sockaddr *)&r_addr, sizeof(r_addr));
	if (err < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "a connection has been established\n");

	sleep(3);

	close(sock);

	return EXIT_SUCCESS;
}
