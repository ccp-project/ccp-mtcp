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
#define SAMPLE_FREQ_US 10000

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
    unsigned long now = (unsigned long)(_dp_now());
    if (_dp_since_usecs(last_print) > SAMPLE_FREQ_US) {
        fprintf(stderr, "%lu %d %d %d ", 
                now / 1000, 
                stream->rcvvar->srtt * 125,
                stream->sndvar->cwnd / stream->sndvar->mss,
#if TCP_OPT_SACK_ENABLED
                stream->rcvvar->sacked_pkts
#else
                -1
#endif
                );
        PrintBucket(stream->bucket);
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
    stream->bucket->rate = rate;
}

static void _dp_set_rate_rel(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t factor) {
	tcp_stream *stream;
	get_stream_from_ccp(&stream, conn);
    stream->bucket->rate *= (factor / 100);
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

uint32_t last_drop_t = 0;

void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, 
		uint32_t ack, uint32_t rtt,
		uint64_t bytes_delivered, uint64_t packets_delivered)
{
	uint64_t rin  = bytes_delivered, //* S_TO_US,  // TODO:CCP divide by snd_int_us
		     rout = bytes_delivered; // * S_TO_US;  // TODO:CCP divide by rcv_int_us
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
#if TCP_OPT_SACK_ENABLED
    mmt->bytes_misordered   = stream->rcvvar->sacked_pkts * 1448;
    mmt->packets_misordered = stream->rcvvar->sacked_pkts;
#endif

    /*
    if (last_drop_t == 0 || _dp_since_usecs(last_drop_t) > 25000) {
        mmt->lost_pkts_sample = 0;
        last_drop_t = _dp_now();
    }
    */

    //fprintf(stderr, "mmt: %u %u\n", conn->prims.packets_misordered, conn->prims.lost_pkts_sample);

	if (conn != NULL) {
            //fprintf(stderr, " lost_pkts=%u\n", mmt->lost_pkts_sample);
            ccp_invoke(conn);
            conn->prims.was_timeout        = false;
            conn->prims.bytes_misordered   = 0;
            conn->prims.packets_misordered = 0;
            conn->prims.lost_pkts_sample   = 0;
#if TCP_OPT_SACK_ENABLED
            stream->rcvvar->sacked_pkts    = 0;
#endif
	} else {
		TRACE_ERROR("ccp_connection not initialized\n")

	}
}

#if TCP_OPT_SACK_ENABLED
uint32_t window_edge_at_last_loss = 0;
uint32_t last_loss = 0;
#endif
uint32_t last_tri_dupack_seq = 0;

void ccp_record(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t event_type, uint32_t val) {
    unsigned long now = (unsigned long)(_dp_now());
    int i;

    switch(event_type) {
        case RECORD_NONE:
            return;
        case RECORD_DUPACK:
#if TCP_OPT_SACK_ENABLED
#else
            // use num dupacks as a proxy for sacked
            stream->ccp_conn->prims.bytes_misordered += val;
            stream->ccp_conn->prims.packets_misordered++;
#endif
            break;
        case RECORD_TRI_DUPACK:
#if TCP_OPT_SACK_ENABLED
            if (val > window_edge_at_last_loss) {
                fprintf(stderr, "%lu tridup ack=%u\n", 
                        now / 1000,
                        val - stream->sndvar->iss
                );
                for (i=0; i < MAX_SACK_ENTRY; i++) {
                    window_edge_at_last_loss = MAX(
                        window_edge_at_last_loss,
                        stream->rcvvar->sack_table[i].right_edge
                    );
                }
                last_tri_dupack_seq = val;
                last_loss = _dp_now();
                stream->ccp_conn->prims.lost_pkts_sample++;
            }
#else
            // only count as a loss if we haven't already seen 3 dupacks for
            // this seq number
            if (last_tri_dupack_seq != val) {
                fprintf(stderr, "%lu tridup ack=%d\n", 
                        now / 1000,
                        val// - stream->sndvar->iss
                );
                stream->ccp_conn->prims.lost_pkts_sample++;
                last_tri_dupack_seq = val;
            }
#endif
            break;
        case RECORD_TIMEOUT:
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
