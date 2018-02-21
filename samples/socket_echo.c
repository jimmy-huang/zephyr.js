/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <errno.h>

#ifndef __ZEPHYR__

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#else

#include <sys/fcntl.h>
#include <net/socket.h>
#include <kernel.h>
#include <net/net_app.h>

#endif

static void nonblock(int fd)
{
	int fl = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int main(void)
{
	int res;
	static int counter;
	int serv4;
	struct sockaddr_in bind_addr4 = {
		.sin_family = AF_INET,
		.sin_port = htons(4242),
		.sin_addr = {
			.s_addr = htonl(INADDR_ANY),
		},
	};

	serv4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	res = bind(serv4, (struct sockaddr *)&bind_addr4, sizeof(bind_addr4));
	if (res == -1) {
		printf("Cannot bind IPv4, errno: %d\n", errno);
	}

	nonblock(serv4);
	listen(serv4, 5);

	printf("Listening on port 4242...\n");

	/* If server socket */
	int client = -1;
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	char addr_str[32];

	// loops until client connects
	while (1) {
		client = accept(serv4, (struct sockaddr *)&client_addr,
						&client_addr_len);
		if (client == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				printf("Error on accept\n");
				return -1;
			}
			sleep(1);
			continue;
		}
		void *addr = &((struct sockaddr_in *)&client_addr)->sin_addr;
		inet_ntop(client_addr.ss_family, addr, addr_str,
					sizeof(addr_str));
		printf("Connection #%d from %s fd=%d\n", counter++, addr_str,
				client);
		nonblock(client);
		break;
	}

	// loops until new message is received
	while (1) {
		char buf[128];
		int len = recv(client, buf, sizeof(buf), 0);
		printf("recv\n");
		if (len == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				printf("Error on recv\n");
				close(serv4);
				return -1;
			}
			printf("Should not block on recv - return error EAGAIN or EWOULDBLOCK\n");
			sleep(1);
			continue;
		}
		if (len == 0) {
			close(serv4);
			printf("Connection fd=%d closed\n", client);
			return 0;
		} else {
			send(client, buf, len, 0);
		}
	}
}
