#include <unistd.h>
#include <sys/un.h>
#include <time.h>

#include "mtcp.h"
#include "tcp_in.h"
#include "tcp_stream.h"
#include "debug.h"
#include "ccp.h"
#include "libccp/ccp.h"

#if USE_CCP

static inline void get_stream_from_ccp(
		tcp_stream **stream,
		struct ccp_connection *conn
) {
	*stream = (tcp_stream *) ccp_get_impl(conn);
}

static inline void get_mtcp_from_ccp(
		mtcp_manager_t *mtcp
) {
	*mtcp = (mtcp_manager_t) ccp_get_global_impl();
}

static void _dp_set_cwnd(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t cwnd) {
	tcp_stream *stream;
	get_stream_from_ccp(&stream, conn);
	stream->sndvar->cwnd = cwnd;
}

static void _dp_set_rate_abs(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t rate) {
	tcp_stream *stream;
	get_stream_from_ccp(&stream, conn);
	// TODO: stream->sndvar->pacing_rate = rate;
	TRACE_ERROR("set_rate_abs not yet implemented!\n")
}

static void _dp_set_rate_rel(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t factor) {
	tcp_stream *stream;
	get_stream_from_ccp(&stream, conn);
	// TODO: __dp_set_rate_abs(dp, conn, stream->sndvar->pacing_rate * (factor / 100));
	TRACE_ERROR("set_rate_rel not yet implemented!\n")
}

int _dp_send_msg(struct ccp_datapath *dp, struct ccp_connection *conn, char *msg, int msg_size) {
	mtcp_manager_t mtcp;
	get_mtcp_from_ccp(&mtcp);

	int ret = send(mtcp->to_ccp, msg, msg_size, 0);
	if (ret < 0) {
		TRACE_ERROR("failed to send msg to ccp\n");
	}
	return ret;
}

uint32_t _dp_now() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((uint32_t) NSEC_TO_USEC((BILLION * now.tv_sec) + now.tv_nsec));
}

uint32_t _dp_after_usecs(uint32_t usecs) {
	return _dp_now() + usecs;
}


void setup_ccp_connection(mtcp_manager_t mtcp) {
	mtcp_thread_context_t ctx = mtcp->ctx;
	int cpu = ctx->cpu;
	char cpu_str[2] = "";
	int send_sock, recv_sock;
	int path_len;
	struct sockaddr_un local, remote;	

	if ((recv_sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		TRACE_ERROR("failed to create unix recv socket for ccp comm\n");
		exit(-1);
	}
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, FROM_CCP_PREFIX);
	sprintf(cpu_str, "%d", cpu);
	strcat(local.sun_path, cpu_str);
	unlink(local.sun_path);
	path_len = strlen(local.sun_path) + sizeof(local.sun_family);
	if (bind(recv_sock, (struct sockaddr *)&local, path_len) == -1) {
		TRACE_ERROR("failed to bind to unix://%s%d\n", FROM_CCP_PREFIX, cpu);
		exit(-1);
	}
	mtcp->from_ccp = recv_sock;

	if ((send_sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		TRACE_ERROR("failed to create unix send socket for ccp comm\n");
		exit(-1);
	}
	remote.sun_family = AF_UNIX;
	strcpy(remote.sun_path, TO_CCP_PREFIX);//TODO:CCP
	path_len = strlen(remote.sun_path) + sizeof(remote.sun_family);
	if (connect(send_sock, (struct sockaddr *)&remote, path_len) == -1) {
		TRACE_ERROR("failed to connect to unix://%s\n", TO_CCP_PREFIX);
		exit(-1);
	}
	mtcp->to_ccp = send_sock;

	struct ccp_datapath dp = {
		.set_cwnd = &_dp_set_cwnd,
		.set_rate_abs = &_dp_set_rate_abs, 
		.set_rate_rel = &_dp_set_rate_rel,
		.send_msg = &_dp_send_msg,
		.now = &_dp_now,
		.after_usecs = &_dp_after_usecs,
		.impl = mtcp
	};

	if (ccp_init(&dp) < 0) {
		TRACE_ERROR("failed to initialize ccp connection map\n");
		exit(-1);
	}

}

void destroy_ccp_connection(mtcp_manager_t mtcp) {
	ccp_free();
	close(mtcp->from_ccp);
	close(mtcp->to_ccp);
}

void ccp_create(mtcp_manager_t mtcp, tcp_stream *stream) {
	/*
	// TODO:CCP get cong alg from sockopt
	char buf[CCP_MAX_MSG_SIZE];
	uint8_t msg_len = 0;

	TRACE_CCP("ccp.create(%d) ->\n", stream->id);

	msg_len = _write_create_msg(stream->id, stream->sndvar->iss, "reno", 4, buf);
	_ccp_send(mtcp, buf, msg_len);
	*/

        struct ccp_datapath_info info = {
            .init_cwnd = stream->sndvar->cwnd, // TODO maybe multiply by mss?
            .mss = stream->sndvar->mss,
            .src_ip = stream->saddr,
            .src_port = stream->sport,
            .dst_ip = stream->daddr,
            .dst_port = stream->dport,
            .congAlg = "reno"
        };
	stream->ccp_conn = ccp_connection_start((void *) stream, &info);
	if (stream->ccp_conn == NULL) {
		TRACE_ERROR("failed to initialize ccp_connection")
	} else {
		TRACE_CCP("ccp.create(%d)\n", dp->index);
	}
}

void _ccp_send(mtcp_manager_t mtcp, char *msg, uint8_t msg_len) {
	if (send(mtcp->to_ccp, msg, msg_len, 0) < 0) {
		TRACE_ERROR("failed to send msg to ccp\n");
	}
}

void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, 
		uint32_t ack, uint32_t rtt,
		uint64_t bytes_delivered, uint64_t packets_delivered)
{
	uint64_t rin = bytes_delivered * S_TO_US, // TODO:CCP divide by snd_int_us
		 rout = bytes_delivered * S_TO_US; // TODO:CCP divide by rcv_int_us
	struct ccp_connection *conn = stream->ccp_conn;
	struct ccp_primitives *mmt = &conn->prims;

	TRACE_CCP("cong_control(%d) ack=%u mrtt=%d rtt=%d\n", stream->id, ack, rtt, stream->ccp->stat.rtt);

	mmt->bytes_acked = bytes_delivered;
	mmt->packets_acked = packets_delivered;
	mmt->bytes_misordered = stream->rcvvar->dup_acks * stream->sndvar->mss; 
	mmt->packets_misordered = stream->rcvvar->dup_acks;
	mmt->snd_cwnd = stream->sndvar->cwnd;
	mmt->rtt_sample_us = rtt; // TODO check if units are correct
	mmt->bytes_in_flight = 0; // TODO
	mmt->packets_in_flight = 0; // TODO
	mmt->lost_pkts_sample = // number of packets that the stack just decided were lost after an ACK arrived

	mmt->rate_outgoing = rin;
	mmt->rate_incoming = rout;

	if (conn != NULL) {
		ccp_invoke(conn);
		conn->prims.was_timeout = false;
	} else {
		TRACE_ERROR("ccp_connection not initialized\n")
	}
}

void ccp_notify_drop(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t drop_type) {

  TRACE_CCP("dupack!\n");
	// TODO NEED TIMEOUT DROPS!!!
	if (stream->ccp_conn != NULL) {
		stream->ccp_conn->prims.was_timeout = true;
	}
}


/*
void ccp_notify_drop(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t drop_type) {
	if (drop_type == stream->ccp->last_drop_t) {
		TRACE_CCP("not reporting duplicate drop type %d\n", drop_type);
		return;
	} 
	stream->ccp->last_drop_t = drop_type;

	char buf[CCP_MAX_MSG_SIZE];
	uint8_t msg_len = 0;

	switch(drop_type) {
		case NO_DROP:
			TRACE_CCP("notify_drop called with arg NO_DROP\n");
			return;
		case DROP_TIMEOUT:
			TRACE_CCP("timeout!\n");
			TRACE_CCP("(currently not reporting timeouts to CCP)\n");
			return;
			msg_len = _write_drop_msg(stream->id, "timeout", 7, buf);
			break;
		case DROP_DUPACK:
			TRACE_CCP("dupack!\n");
			msg_len = _write_drop_msg(stream->id, "dupack", 6, buf);
			break;
		case DROP_ECN:
			TRACE_ERROR("ecn is not currently supported in mtcp\n");
			return;
		default:
			TRACE_CCP("unknown drop type %d\n", drop_type);
			return;
	}
	_ccp_send(mtcp, buf, msg_len);
}

void ccp_sm_set_cwnd_abs(tcp_stream *stream, uint32_t cwnd) {
	//printf("cwnd:%d\n",cwnd);
	stream->sndvar->cwnd = cwnd;
}

void ccp_sm_set_rate_abs(tcp_stream *stream, uint32_t rate) {
	// TODO:CCP
	printf("set_rate_abs not yet implemented (stream=%d rate=%d)\n", stream->id, rate);
}

void ccp_sm_set_rate_rel(tcp_stream *stream, uint32_t factor) {
	// TODO:CCP
	printf("set_rate_rel not yet implemented (stream=%d factor=%d)\n", stream->id, factor);
}

void ccp_reset_stats(struct ccp_vars *ccp) {
	ccp->stat.rtt = 0;
	ccp->stat.rin = 0;
	ccp->stat.rout = 0;
}

void ccp_sm_send_report(mtcp_manager_t mtcp, tcp_stream *stream) {
	FlowStat *stat = &(stream->ccp->stat);
	char buf[CCP_MAX_MSG_SIZE];
	uint8_t msg_len = 0;

	msg_len = _write_measure_msg(stream->id, stat->ack, USEC_TO_NSEC(stat->rtt), stat->rin, stat->rout, buf);
	_ccp_send(mtcp, buf, msg_len);

	ccp_reset_stats(stream->ccp);
}

void ccp_sm_wait_abs(tcp_stream *stream, uint32_t wait_ms) {
	stream->ccp->next_state_time = current_ts() + wait_ms;
}

void ccp_sm_wait_rel(tcp_stream *stream, uint32_t rtt_factor) {
	ccp_sm_wait_abs(stream, stream->ccp->stat.rtt * (rtt_factor / 100.0));
}

void next_state(mtcp_manager_t mtcp, tcp_stream *stream) {
	StateMachine *m = &(stream->ccp->sm);

	if (m->num_states == 0) { // State machine is empty
		return;
	}

	uint32_t now = current_ts();
	if(now > stream->ccp->next_state_time) { // Ready for next state
		m->cur_state = (m->cur_state + 1) % m->num_states;
		TRACE_CCP("NEXT EVENT CUR_STATE=%d ",m->cur_state);
	} else {
		return;
	}
	
	State state = m->seq[m->cur_state];
	switch(state.type) {
		case PATTERN_SETRATEABS:
			TRACE_CCP("set rate %u\n", state.val);
			ccp_sm_set_rate_abs(stream, state.val);
			break;
		case PATTERN_SETCWNDABS:
			TRACE_CCP("set cwnd %d\n", state.val);
			ccp_sm_set_cwnd_abs(stream, state.val);
			break;
		case PATTERN_SETRATEREL:
			TRACE_CCP("set rate factor %u\n", state.val);
			ccp_sm_set_rate_rel(stream, state.val);
			break;
		case PATTERN_WAITABS:
			TRACE_CCP("wait %d us\n", state.val);
			ccp_sm_wait_abs(stream, state.val);
			break;
		case PATTERN_WAITREL:
			TRACE_CCP("wait %f rtts\n", (state.val/100.0));
			ccp_sm_wait_rel(stream, state.val);
			break;
		case PATTERN_REPORT:
			TRACE_CCP("report\n");
			ccp_sm_send_report(mtcp, stream);
			break;
		default:
			break;
	}
}

void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, uint32_t ack, uint32_t rtt, uint64_t bytes_delivered) {
	uint64_t rin = bytes_delivered * S_TO_US, // TODO:CCP divide by snd_int_us
		 rout = bytes_delivered * S_TO_US; // TODO:CCP divide by rcv_int_us


	FlowStat *stat = &(stream->ccp->stat);
	stat->ack = ack;
	stat->rtt = ewma(stat->rtt, rtt);
	stat->rin = ewma(stat->rin, rin);
	stat->rout = ewma(stat->rout, rout);
	TRACE_CCP("cong_control(%d) ack=%u mrtt=%d rtt=%d\n", stream->id, ack, rtt, stream->ccp->stat.rtt);

	next_state(mtcp, stream);
}

void print_machine(StateMachine *m) {
	int i;
	printf("StateMachine {\n");
	for (i=0; i<m->num_states; i++) {
		State s = m->seq[i];
		if (i == m->cur_state) {
			printf("\t[_]");
		} else {
			printf("\t[%d]: ", i+1);
		}
		switch(s.type) {
			case PATTERN_SETRATEABS:
				printf("set rate %u", s.val);
				break;
			case PATTERN_SETCWNDABS:
				printf("set cwnd %d", s.val);
				break;
			case PATTERN_SETRATEREL:
				printf("set rate factor %u", s.val);
				break;
			case PATTERN_WAITREL:
				printf("wait %d rtts", s.val);
				break;
			case PATTERN_WAITABS:
				printf("wait %d us", s.val);
				break;
			case PATTERN_REPORT:
				printf("report");
				break;
		}
		printf(" ->\n");
	}
	printf("}\n");
}

void print_buf(char *buf, int len) {
	int i;
	for (i=0; i<len; i++) {
		printf("%d ",buf[i]);
	}
	printf("\n");
}


uint8_t _write_create_msg(uint32_t sid, uint32_t iss, char *cong_alg, uint8_t cong_alg_len, char *out) {
	uint8_t length = CREATE_MIN_LEN + cong_alg_len;

	uint8_t *pu8 = (uint8_t *) out;
	*pu8 = CREATE_MSG_TYPE;		pu8++;
	*pu8 = length;						pu8++;

	uint32_t *pu32 = (uint32_t *) pu8;
	*pu32 = sid;							pu32++;
	*pu32 = iss;							pu32++;

	char *pc = (char *) pu32;
	strncpy(pc, cong_alg, cong_alg_len);

	return length;
}

uint8_t _write_drop_msg(uint32_t sid, char *event, uint8_t event_len, char *out) {
	uint8_t length = DROP_MIN_LEN + event_len;

	uint8_t *pu8 = (uint8_t *) out;
	*pu8 = DROP_MSG_TYPE;			pu8++;
	*pu8 = length;						pu8++;

	uint32_t *pu32 = (uint32_t *) pu8;
	*pu32 = sid;							pu32++;

	char *pc = (char *) pu32;
	strncpy(pc, event, event_len);

	return length;
}

uint8_t _write_measure_msg(uint32_t sid, uint32_t ack, uint32_t rtt, uint64_t rin, uint64_t rout, char *out) {
	uint8_t length = MEASURE_MIN_LEN;

	uint8_t *pu8 = (uint8_t *) out;
	*pu8 = MEASURE_MSG_TYPE;			pu8++;
	*pu8 = length;						pu8++;

	uint32_t *pu32 = (uint32_t *) pu8;
	*pu32 = sid;							pu32++;
	*pu32 = ack;							pu32++;
	*pu32 = rtt;							pu32++;

	uint64_t *pu64 = (uint64_t *) pu32;
	*pu64 = rin;							pu64++;
	*pu64 = rout;							pu64++;

	return length;
}

void ccp_install_state_machine(char *in, tcp_stream *stream) {
	int i;
	//printf("installing new state machine for stream %d\n", stream->id);
	StateMachine *m = &(stream->ccp->sm);

	m->num_states = *((uint8_t *)in);
	in += 4;

	// TODO:CCP should probably use the memory pool api for hugepages, but this will do
	// for now, hopefully..
	State *seq = malloc(m->num_states * sizeof(State));
	for (i=0; i<m->num_states; i++) {
		memcpy(&(seq[i]), in, sizeof(State));
		if (seq[i].type == PATTERN_REPORT && seq[i].size == 2) {
			seq[i].val = 0;
		} else if (seq[i].size != 6) {
			printf("found unknown event type %d with %d bytes\n", seq[i].type, seq[i].size);
			return;
		}
		in += seq[i].size;
	}
	
	m->seq = seq;
	m->cur_state = m->num_states - 1;

	stream->ccp->next_state_time = current_ts();
	stream->ccp->last_drop_t = NO_DROP;

	//print_machine(m);
	return;
}
*/
#endif
