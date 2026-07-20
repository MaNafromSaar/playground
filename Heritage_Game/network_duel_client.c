#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static void fatal(const char *msg)
{
	write(2, msg, strlen(msg));
	write(2, "\n", 1);
	exit(1);
}

int main(int argc, char **argv)
{
	if (argc != 3)
		fatal("Usage: ./network_duel_client <host> <port>");

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		fatal("socket failed");

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)atoi(argv[2]));

	if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1)
		fatal("invalid host (use IPv4, e.g. 127.0.0.1)");
	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		fatal("connect failed");

	char inbuf[1024];
	char sockbuf[2048];

	while (1)
	{
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		FD_SET(sockfd, &readfds);
		int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

		if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
			fatal("select failed");

		if (FD_ISSET(sockfd, &readfds))
		{
			int n = recv(sockfd, sockbuf, sizeof(sockbuf) - 1, 0);
			if (n <= 0)
				break;
			sockbuf[n] = '\0';
			write(STDOUT_FILENO, sockbuf, n);
		}

		if (FD_ISSET(STDIN_FILENO, &readfds))
		{
			if (!fgets(inbuf, sizeof(inbuf), stdin))
				break;
			if (send(sockfd, inbuf, strlen(inbuf), 0) < 0)
				fatal("send failed");
		}
	}

	close(sockfd);
	return 0;
}
