#ifndef __CCP_H_
#define __CCP_H_

#include <sys/un.h>

#include "tcp_stream.h"
#include "tcp_in.h"
#include "debug.h"

#define FROM_CCP_PREFIX "/tmp/ccp/0/out"
#define TO_CCP_PREFIX "/tmp/ccp/0/in"
#define CCP_MAX_MSG_SIZE 256

#define INIT_CWND 2

#define MTU 						1500
#define S_TO_US 				1000000
#define USEC_TO_NSEC(us)		(us*1000)
#define NSEC_TO_USEC(ns)    (ns/1000)
#define MSEC_TO_NSEC(ms)		(ms*1000000)
#define BILLION 1000000000L

#define MIN(a, b) ((a)<(b)?(a):(b))
#define MAX(a, b) ((a)>(b)?(a):(b))

#define RECORD_NONE       0
#define RECORD_DUPACK     1
#define RECORD_TRI_DUPACK 2
#define RECORD_TIMEOUT    3
#define RECORD_ECN        4

static inline uint32_t current_ts() {
	struct timeval cur_ts = {0};
	gettimeofday(&cur_ts, NULL);
	return TIMEVAL_TO_TS(&cur_ts);
}

void setup_ccp_connection(mtcp_manager_t mtcp);
void destroy_ccp_connection(mtcp_manager_t mtcp);
void ccp_create(mtcp_manager_t mtcp, tcp_stream *stream);
void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, uint32_t ack, uint32_t rtt, uint64_t bytes_delivered, uint64_t packets_delivered);
void ccp_record(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t event_type, uint32_t val);

void log_cwnd_rtt(tcp_stream *stream);

// Time functions, needed for token bucket
uint32_t _dp_now();
uint32_t _dp_since_usecs(uint32_t then);
uint32_t _dp_after_usecs(uint32_t usecs);
#endif /* __CCP_H_ */
