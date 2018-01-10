#ifndef __CCP_H_
#define __CCP_H_

#include <sys/un.h>

#include "tcp_stream.h"
#include "tcp_in.h"
#include "debug.h"

#define FROM_CCP_PREFIX "/tmp/ccp-out-mtcp"
#define TO_CCP_PREFIX "/tmp/ccp-in"
#define CCP_MAX_MSG_SIZE 256

#define CREATE_MSG_TYPE		0
#define MEASURE_MSG_TYPE	1
#define DROP_MSG_TYPE			2
#define PATTERN_MSG_TYPE	3

#define CREATE_MIN_LEN		10
#define MEASURE_MIN_LEN		30
#define DROP_MIN_LEN			6
#define PATTERN_MIN_LEN		6

#define PATTERN_SETRATEABS	0
#define PATTERN_SETCWNDABS	1
#define PATTERN_SETRATEREL	2
#define PATTERN_WAITABS			3
#define PATTERN_WAITREL			4
#define PATTERN_REPORT			5

#define MTU 						1500
#define S_TO_US 				1000000
#define USEC_TO_NSEC(us)		(us*1000)
#define MSEC_TO_NSEC(ms)		(ms*1000000)

typedef uint8_t drop_t;
#define NO_DROP				0
#define DROP_TIMEOUT	1
#define DROP_DUPACK		2
#define DROP_ECN			3

static inline uint32_t current_ts() {
	struct timeval cur_ts = {0};
	gettimeofday(&cur_ts, NULL);
	return TIMEVAL_TO_TS(&cur_ts);
}

void setup_ccp_connection(mtcp_manager_t mtcp);
void ccp_create(mtcp_manager_t mtcp, tcp_stream *stream);
void ccp_notify_drop(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t drop_type);
void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, uint32_t ack, uint32_t rtt, uint64_t bytes_delivered);
void ccp_install_state_machine(char *in, tcp_stream *stream);

typedef struct CreateMsg {
	uint32_t	sid;
	uint32_t	start_seq;
	char*			cong_alg;
} CreateMsg;

uint8_t _write_create_msg(
		uint32_t sid,
		uint32_t iss,
		char *cong_alg,
		uint8_t cong_alg_len,
		char *out
);
uint8_t _write_drop_msg(
		uint32_t sid,
		char *event,
		uint8_t event_len,
		char *out
);
uint8_t _write_measure_msg(
		uint32_t sid,
		uint32_t ack,
		uint32_t rtt,
		uint64_t rin,
		uint64_t rout,
		char *out
);

typedef struct __attribute__((packed, aligned(2))) State {
	uint8_t type;
	uint8_t size;
	uint32_t val;
} State;

typedef struct StateMachine {
	uint8_t num_states;
	uint8_t cur_state;
	State *seq;
} StateMachine;

typedef struct FlowStat {
	uint32_t ack;
	uint32_t rtt;
	uint64_t rin;
	uint64_t rout;
} FlowStat;

struct ccp_vars {
	uint32_t			cur_rate;
	uint32_t			next_state_time;
	drop_t				last_drop_t;
	FlowStat			stat;
	StateMachine	sm;
};

#define CCP_FRAC_DENOM		10
#define CCP_EWMA_RECENCY	6
static inline uint64_t ewma(uint64_t curr, uint64_t sample) {
	if (curr == 0) {
		return sample;
	}
	return ((sample * CCP_EWMA_RECENCY) + 
			(curr * (CCP_FRAC_DENOM-CCP_EWMA_RECENCY))) / CCP_FRAC_DENOM;
}

#endif /* __CCP_H_ */
