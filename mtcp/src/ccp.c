#include <unistd.h>
#include <sys/un.h>
#include <time.h>

#include "mtcp.h"
#include "tcp_in.h"
#include "tcp_stream.h"
#include "debug.h"
#include "ccp.h"
#include "libccp/ccp.h"

/*****************************************************************************
 *  Internal timekeeping
 *  Outside of if USE_CCP because useful for adding time since start to log
 ****************************************************************************/
uint64_t init_time_ns = 0;
uint32_t last_print = 0;

uint32_t _dp_now() {
    struct timespec now;
    uint32_t now_us;
    uint64_t now_ns;

    clock_gettime(CLOCK_MONOTONIC, &now);

    now_ns = (BILLION * now.tv_sec) + now.tv_nsec;
    if (init_time_ns == 0) {
        init_time_ns = now_ns;
    }

    now_us = ((now_ns - init_time_ns) / 1000) & 0xffffffff;
    return now_us;
}

uint32_t _dp_since_usecs(uint32_t then) {
    return _dp_now() - then;
}

uint32_t _dp_after_usecs(uint32_t usecs) {
    return _dp_now() + usecs;
}

void log_cwnd_rtt(tcp_stream *stream) {
    unsigned long now = (unsigned long)(_dp_now() / 1000);
    if (_dp_since_usecs(last_print) > 100000) {
        printf("%lu %d %d\n", 
                now, 
                stream->rcvvar->srtt * 125,
                stream->sndvar->cwnd / stream->sndvar->mss);
        last_print = now;
    }
}
/*****************************************************************************/

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
	stream->sndvar->cwnd = MAX(cwnd, INIT_CWND * stream->sndvar->mss);
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
		TRACE_ERROR("failed to send msg to ccp: %s\n", strerror(errno));
	}
	return ret;
}

void setup_ccp_connection(mtcp_manager_t mtcp) {
	mtcp_thread_context_t ctx = mtcp->ctx;
	int cpu = ctx->cpu;
	//char cpu_str[2] = "";
	int send_sock, recv_sock;
	int path_len;
	struct sockaddr_un local, remote;	

	if ((recv_sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		TRACE_ERROR("failed to create unix recv socket for ccp comm\n");
		exit(-1);
	}
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, FROM_CCP_PREFIX);
	//sprintf(cpu_str, "%d", cpu);
	//strcat(local.sun_path, cpu_str);
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
	    .since_usecs = &_dp_since_usecs,
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

    struct ccp_datapath_info info = {
        .init_cwnd = INIT_CWND, // TODO maybe multiply by mss?
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

void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, 
		uint32_t ack, uint32_t rtt,
		uint64_t bytes_delivered, uint64_t packets_delivered)
{
	uint64_t rin  = bytes_delivered * S_TO_US,  // TODO:CCP divide by snd_int_us
		     rout = bytes_delivered * S_TO_US;  // TODO:CCP divide by rcv_int_us
	struct ccp_connection *conn = stream->ccp_conn;
	struct ccp_primitives *mmt = &conn->prims;

	mmt->bytes_acked       = bytes_delivered;
	mmt->packets_acked     = packets_delivered;
	mmt->snd_cwnd          = stream->sndvar->cwnd; 
	mmt->rtt_sample_us     = rtt * 125; 
	mmt->bytes_in_flight   = 0; // TODO
	mmt->packets_in_flight = 0; // TODO
	mmt->rate_outgoing     = rin;
	mmt->rate_incoming     = rout;

	if (conn != NULL) {
		ccp_invoke(conn);
		conn->prims.was_timeout        = false;
        conn->prims.bytes_misordered   = 0;
        conn->prims.packets_misordered = 0;
        conn->prims.lost_pkts_sample   = 0;
	} else {
		TRACE_ERROR("ccp_connection not initialized\n")
	}
}

void ccp_record(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t event_type, uint32_t val) {

    switch(event_type) {
        case RECORD_NONE:
            return;
        case RECORD_DUPACK:
            stream->ccp_conn->prims.bytes_misordered += stream->sndvar->mss;
            stream->ccp_conn->prims.packets_misordered++;
            break;
        case RECORD_TRI_DUPACK:
            stream->ccp_conn->prims.lost_pkts_sample++;
            break;
        case RECORD_TIMEOUT:
            //printf("timeout!\n");
            //stream->ccp_conn->prims.was_timeout = true;
            break;
        case RECORD_ECN:
            printf("ecn is not currently supported!\n");
            break;
        default:
            printf("unknown record event type %d!\n", event_type);
            break;
    }

}

#endif
