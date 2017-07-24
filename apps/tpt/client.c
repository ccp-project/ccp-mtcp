#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <assert.h>

#include <mtcp_api.h>
#include <mtcp_epoll.h>
#include "cpu.h"
#include "rss.h"
#include "http_parsing.h"
#include "debug.h"

#define MAX_CPUS 16

#define MAX_URL_LEN 128
#define MAX_FILE_LEN 128
#define HTTP_HEADER_LEN 1024

#define IP_RANGE 1
#define MAX_IP_STR_LEN 16

#define BUF_SIZE (32*1024)

#define CALC_MD5SUM FALSE

#define TIMEVAL_TO_MSEC(t)		((t.tv_sec * 1000) + (t.tv_usec / 1000))
#define TIMEVAL_TO_USEC(t)		((t.tv_sec * 1000000) + (t.tv_usec))
#define TS_GT(a,b)				((int64_t)((a)-(b)) > 0)

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef ERROR
#define ERROR (-1)
#endif

#define CONCURRENCY 1
#define BUF_LEN 8192

struct thread_context
{
	int core;
	mctx_t mctx;
};

void SignalHandler(int signum) 
{
	printf("Received SIGINT.\n");
	exit(-1);
}

int main(int argc, char **argv) 
{
	int ret;
	// mtcp 
	mctx_t mctx;
	struct mtcp_conf mcfg;
	struct thread_context *ctx;
	struct mtcp_epoll_event *events;
	struct mtcp_epoll_event ev;
	int core = 0,
			ep_id;
	// sockets
	struct sockaddr_in daddr;
	int sockfd;
	// counters
	int bytes_to_send;
	int wrote      = 0,
			read       = 0,
			bytes_sent = 0,
			events_ready = 0;
	double elapsed_time = 0.0;
	struct timeval t1, t2;
	// send buffer
	char buf[BUF_LEN];
	char rcvbuf[BUF_LEN];

	if (argc < 4) {
		printf("usage: ./client [ip] [port] [bytes]\n");
		return -1;
	}

	// Parse command-line args 
	daddr.sin_family = AF_INET;
	daddr.sin_addr.s_addr = inet_addr(argv[1]);;
	daddr.sin_port = htons(atoi(argv[2]));
	bytes_to_send = atoi(argv[3]);

	// This must be done before mtcp_init
	mtcp_getconf(&mcfg);
	mcfg.num_cores = 1;
	mtcp_setconf(&mcfg);
	// Seed RNG
	srand(time(NULL));

	// Init mtcp
	printf("Initializing mtcp...\n");
	if (mtcp_init("client.conf")) {
		printf("Failed to initialize mtcp.\n");
		return -1;
	}

	// Default simple config, this must be done after mtcp_init
	mtcp_getconf(&mcfg);
	mcfg.max_concurrency = 3 * CONCURRENCY;
	mcfg.max_num_buffers = 3 * CONCURRENCY;
	mtcp_setconf(&mcfg);

	// Catch ctrl+c to clean up
	mtcp_register_signal(SIGINT, SignalHandler);

	printf("Creating thread context...\n");
	mtcp_core_affinitize(core);
	ctx = (struct thread_context *) calloc(1, sizeof(struct thread_context));
	if (!ctx) {
		printf("Failed to create context.\n");
		perror("calloc");
		return -1;
	}
	ctx->core = core;
	ctx->mctx = mtcp_create_context(core);
	if (!ctx->mctx) {
		printf("Failed to create mtcp context.\n");
		return -1;
	}
	mctx = ctx->mctx;

	// Create pool of TCP source ports for outgoing conns
	printf("Creating pool of TCP source ports...\n");
	mtcp_init_rss(mctx, INADDR_ANY, IP_RANGE, daddr.sin_addr.s_addr, daddr.sin_port);

	printf("Creating epoller...\n");
	ep_id = mtcp_epoll_create(ctx->mctx, mcfg.max_num_buffers);
	events = (struct mtcp_epoll_event *) calloc(mcfg.max_num_buffers, sizeof(struct mtcp_epoll_event));
	if (!events) {
		printf("Failed to allocate events.\n");
		return -1;
	}


  printf("Creating socket...\n");
	sockfd = mtcp_socket(mctx, AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("Failed to create socket.\n");
		return -1;
	}

	printf("Connecting socket...\n");
	ret = mtcp_connect(mctx, sockfd, (struct sockaddr *)&daddr, sizeof(struct sockaddr_in));
	if (ret < 0) {
		printf("mtcp_connect failed.\n");
		if (errno != EINPROGRESS) {
			perror("mtcp_connect");
			mtcp_close(mctx, sockfd);
			return -1;
		}
	}

	ret = mtcp_setsock_nonblock(mctx, sockfd);
	if (ret < 0) {
		printf("Failed to set socket in nonblocking mode.\n");
		return -1;
	}

	ev.events = MTCP_EPOLLIN;
	ev.data.sockid = sockfd;
	mtcp_epoll_ctl(mctx, ep_id, MTCP_EPOLL_CTL_ADD, sockfd, &ev);

	// Fill buffer with some data
	memset(buf, 0x90, sizeof(char) * BUF_LEN);
	buf[BUF_LEN-1] = '\0';

  int connected = 0;
	printf("Connection created. Sending...\n");
	while (bytes_to_send > 0) {
		wrote = mtcp_write(ctx->mctx, sockfd, buf, (bytes_to_send < BUF_LEN ? bytes_to_send : BUF_LEN));
		if (wrote > 0 && connected == 0) {
			gettimeofday(&t1, NULL); 
			connected = 1;
		}
		bytes_sent += (wrote >= 0 ? wrote : 0);
		bytes_to_send -= (wrote >= 0 ? wrote : 0);
	}

	// Send done (anything other than 0x90)
	memset(buf, 0x96, sizeof(char) * BUF_LEN);
	mtcp_write(ctx->mctx, sockfd, buf, 1);
	goto stop_timer;
	// Poll for "OK" from receiver
	printf("Done writing... polling...\n");
	events_ready = mtcp_epoll_wait(ctx->mctx, ep_id, events, mcfg.max_num_buffers, -1);
	printf("epoll returned %d.\n", events_ready);
	while (1) {
	for (int i=0; i<events_ready; i++) {
		if (events[i].events & MTCP_EPOLLIN) {
			read = mtcp_read(ctx->mctx, sockfd, rcvbuf, BUF_LEN);
			if (read <= 0) {
				goto stop_timer;
			} else {
				printf("Read %d bytes: %s\n", read, rcvbuf);
			}
		} else {
			printf("Event was %d\n",events[i].events);
			goto stop_timer;
		}
	}
	}
stop_timer:
	gettimeofday(&t2, NULL);

	printf("Done reading. Closing socket...\n");
	mtcp_close(mctx, sockfd);
	printf("Socket closed.\n");
	
	// Calculate and report stats
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1.0;
	elapsed_time += (t2.tv_usec - t1.tv_usec) / 1000000.0;
	printf("Time elapsed: %f\n", elapsed_time);
	printf("Total bytes sent: %d\n", bytes_sent);
	printf("Throughput: %.3fMbit/sec\n", ((bytes_sent * 8.0 / 1000000.0) / elapsed_time));

	mtcp_destroy_context(ctx->mctx);
	free(ctx);
	mtcp_destroy();

	return 0;
}
