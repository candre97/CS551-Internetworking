/******************************************************************************
 * ctcp_bbr.h
 * ------
 * Contains definitions for constants functions, and structs you will need for
 * the BBR implementation. Implementations of the functions should be done in
 * ctcp_bbr.c.
 *
 *****************************************************************************/

#ifndef CTCP_BBR_H
#define CTCP_BBR_H

#define CYCLE_LEN	8	/* number of phases in a pacing gain cycle */
/* Scale factor for rate in pkt/uSec unit to avoid truncation in bandwidth
 * estimation. The rate unit ~= (1500 bytes / 1 usec / 2^24) ~= 715 bps.
 * This handles bandwidths from 0.06pps (715bps) to 256Mpps (3Tbps) in a uint32_t.
 * Since the minimum window is >=4 packets, the lower bound isn't
 * an issue. The upper bound isn't an issue with existing technologies.
 */
#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)
#define BBR_SCALE 8	/* scaling factor for fractions in BBR (e.g. gains) */
#define BBR_UNIT (1 << BBR_SCALE)

static int bbr_bw_rtts	= CYCLE_LEN + 2; /* win len of bw filter (in rounds) */
static uint32_t bbr_min_rtt_win_sec = 10;	 /* min RTT filter window (in sec) */
static uint32_t bbr_probe_rtt_mode_ms = 200;	 /* min ms at cwnd=4 in BBR_PROBE_RTT */
static int bbr_min_tso_rate	= 1200000;  /* skip TSO below here (bits/sec) */

/* We use a high_gain value chosen to allow a smoothly increasing pacing rate
 * that will double each RTT and send the same number of packets per RTT that
 * an un-paced, slow-starting Reno or CUBIC flow would.
 */
static int bbr_high_gain  = BBR_UNIT * 2885 / 1000 + 1;	/* 2/ln(2) */
static int bbr_drain_gain = BBR_UNIT * 1000 / 2885;	/* 1/high_gain */
static int bbr_cwnd_gain  = BBR_UNIT * 2;	/* gain for steady-state cwnd */
/* The pacing_gain values for the PROBE_BW gain cycle: */
static int bbr_pacing_gain[] = { BBR_UNIT * 5 / 4, BBR_UNIT * 3 / 4,
				 BBR_UNIT, BBR_UNIT, BBR_UNIT,
				 BBR_UNIT, BBR_UNIT, BBR_UNIT };
static uint32_t bbr_cycle_rand = 7;  /* randomize gain cycling phase over N phases */

/* Try to keep at least this many packets in flight, if things go smoothly. For
 * smooth functioning, a sliding window protocol ACKing every other packet
 * needs at least 4 packets in flight.
 */
static uint32_t bbr_cwnd_min_target	= 4;

/* INET_DIAG_BBRINFO */
typedef struct {
	/* uint64_t bw: max-filtered BW (app throughput) estimate in Byte per sec: */
	uint32_t	bbr_bw_lo;		/* lower 32 bits of bw */
	uint32_t	bbr_bw_hi;		/* upper 32 bits of bw */
	uint32_t	bbr_min_rtt;		/* min-filtered RTT in uSec */
	uint32_t	bbr_pacing_gain;	/* pacing gain shifted left 8 bits */
	uint32_t	bbr_cwnd_gain;		/* cwnd gain shifted left 8 bits */
} tcp_bbr_info_t;

/* BBR has the following modes for deciding how fast to send: */
enum bbr_mode {
	BBR_STARTUP,	/* ramp up sending rate rapidly to fill pipe */
	BBR_DRAIN,	/* drain any queue created during startup */
	BBR_PROBE_BW,	/* discover, share bw: pace around estimated bw */
	BBR_PROBE_RTT,	/* cut cwnd to min to probe min_rtt */
};


/* BBR congestion control block */
typedef struct {
	uint32_t	min_rtt_us;	        /* min RTT in min_rtt_win_sec window */
	uint32_t	min_rtt_stamp;	        /* timestamp of min_rtt_us */
	uint32_t	probe_rtt_done_stamp;   /* end time for BBR_PROBE_RTT mode */
	/*struct minmax bw;*/	/* Max recent delivery rate in pkts/uS << 24 */
	uint32_t	rtt_cnt;	    /* count of packet-timed rounds elapsed */
	uint32_t     next_rtt_delivered; /* scb->tx.delivered at end of round */
	/*struct skb_mstamp cycle_mstamp;*/  /* time of this cycle phase start */
	uint32_t     mode:3,		     /* current bbr_mode in state machine */
		prev_ca_state:3,     /* CA state on previous ACK */
		packet_conservation:1,  /* use packet conservation? */
		restore_cwnd:1,	     /* decided to revert cwnd to old value */
		round_start:1,	     /* start of packet-timed tx->ack round? */
		tso_segs_goal:7,     /* segments we want in each skb we send */
		idle_restart:1,	     /* restarting after idle? */
		probe_rtt_round_done:1,  /* a BBR_PROBE_RTT round at 4 pkts? */
		unused:5,
		lt_is_sampling:1,    /* taking long-term ("LT") samples now? */
		lt_rtt_cnt:7,	     /* round trips in long-term interval */
		lt_use_bw:1;	     /* use lt_bw as our bw estimate? */
	uint32_t	lt_bw;		     /* LT est delivery rate in pkts/uS << 24 */
	uint32_t	lt_last_delivered;   /* LT intvl start: tp->delivered */
	uint32_t	lt_last_stamp;	     /* LT intvl start: tp->delivered_mstamp */
	uint32_t	lt_last_lost;	     /* LT intvl start: tp->lost */
	uint32_t	pacing_gain:10,	/* current gain for setting pacing rate */
		cwnd_gain:10,	/* current gain for setting cwnd */
		full_bw_cnt:3,	/* number of rounds without large bw gains */
		cycle_idx:3,	/* current index in pacing_gain cycle array */
		unused_b:6;
	uint32_t	prior_cwnd;	/* prior cwnd upon entering loss recovery */
	uint32_t	full_bw;	/* recent bw, to estimate if pipe is full */
} bbr_t;


/* Do we estimate that STARTUP filled the pipe? */
static bool bbr_full_bw_reached(bbr_t* bbr);


/* Return the windowed max recent bandwidth sample, in pkts/uS << BW_SCALE. */
static uint32_t bbr_max_bw(bbr_t* bbr);


/* Return the estimated bandwidth of the path, in pkts/uS << BW_SCALE. */
static uint32_t bbr_bw(bbr_t* bbr);

/* Return rate in bytes per second, optionally with a gain.
 * The order here is chosen carefully to avoid overflow of uint64_t. This should
 * work for input rates of up to 2.9Tbit/sec and gain of 2.89x.
 */
/*static uint64_t bbr_rate_bytes_per_sec(struct sock *sk, uint64_t rate, int gain);*/

/*static uint64_t bbr_rate_kbps(struct sock *sk, uint64_t rate);*/


/* Pace using current bw estimate and a gain factor. */
static void bbr_set_pacing_rate(bbr_t* bbr, uint32_t bw, int gain); 


/* Return count of segments we want in the skbs we send, or 0 for default. */
static uint32_t bbr_tso_segs_goal(bbr_t* bbr);


/*static void bbr_set_tso_segs_goal(struct sock *sk);*/


/* Save "last known good" cwnd so we can restore it after losses or PROBE_RTT */
static void bbr_save_cwnd(bbr_t* bbr, ctcp_state_t* state,);


static void bbr_cwnd_event(bbr_t* bbr, ctcp_state_t* state, enum tcp_ca_event event);

/* Find target cwnd. Right-size the cwnd based on min RTT and the
 * estimated bottleneck bandwidth:
 *
 * cwnd = bw * min_rtt * gain = BDP * gain
 *
 * The key factor, gain, controls the amount of queue. While a small gain
 * builds a smaller queue, it becomes more vulnerable to noise in RTT
 * measurements (e.g., delayed ACKs or other ACK compression effects). This
 * noise may cause BBR to under-estimate the rate.
 *
 * To achieve full performance in high-speed paths, we budget enough cwnd to
 * fit full-sized skbs in-flight on both end hosts to fully utilize the path:
 *   - one skb in sending host Qdisc,
 *   - one skb in sending host TSO/GSO engine
 *   - one skb being received by receiver host LRO/GRO/delayed-ACK engine
 * Don't worry, at low rates (bbr_min_tso_rate) this won't bloat cwnd because
 * in such cases tso_segs_goal is 1. The minimum cwnd is 4 packets,
 * which allows 2 outstanding 2-packet sequences, to try to keep pipe
 * full even with ACK-every-other-packet delayed ACKs.
 */
static uint32_t bbr_target_cwnd(bbr_t* bbr, uint32_t bw, int gain);

/* An optimization in BBR to reduce losses: On the first round of recovery, we
 * follow the packet conservation principle: send P packets per P packets acked.
 * After that, we slow-start and send at most 2*P packets per P packets acked.
 * After recovery finishes, or upon undo, we restore the cwnd we had when
 * recovery started (capped by the target cwnd based on estimated BDP).
 *
 * TODO(ycheng/ncardwell): implement a rate-based approach.
 */
static bool bbr_set_cwnd_to_recover_or_restore(
	bbr_t* bbr, ctcp_state_t* state, const struct rate_sample *rs, uint32_t acked, uint32_t *new_cwnd);


/* Slow-start up toward target cwnd (if bw estimate is growing, or packet loss
 * has drawn us down below target), or snap down to target if we're above it.
 */
static void bbr_set_cwnd(struct sock *sk, const struct rate_sample *rs,
			 uint32_t acked, uint32_t bw, int gain);

/* End cycle phase if it's time and/or we hit the phase's in-flight target. */
static bool bbr_is_next_cycle_phase(struct sock *sk,
				    const struct rate_sample *rs);

static void bbr_advance_cycle_phase(struct sock *sk);


/* Gain cycling: cycle pacing gain to converge to fair share of available bw. */
static void bbr_update_cycle_phase(struct sock *sk,
				   const struct rate_sample *rs);


static void bbr_reset_startup_mode(struct sock *sk); 

static void bbr_reset_probe_bw_mode(struct sock *sk);


static void bbr_reset_mode(struct sock *sk);


/* Start a new long-term sampling interval. */
static void bbr_reset_lt_bw_sampling_interval(struct sock *sk);

/* Completely reset long-term bandwidth sampling. */
static void bbr_reset_lt_bw_sampling(struct sock *sk); 

/* Long-term bw sampling interval is done. Estimate whether we're policed. */
static void bbr_lt_bw_interval_done(struct sock *sk, uint32_t bw);


/* Token-bucket traffic policers are common (see "An Internet-Wide Analysis of
 * Traffic Policing", SIGCOMM 2016). BBR detects token-bucket policers and
 * explicitly models their policed rate, to reduce unnecessary losses. We
 * estimate that we're policed if we see 2 consecutive sampling intervals with
 * consistent throughput and high packet loss. If we think we're being policed,
 * set lt_bw to the "long-term" average delivery rate from those 2 intervals.
 */
static void bbr_lt_bw_sampling(struct sock *sk, const struct rate_sample *rs);


/* Estimate the bandwidth based on how fast packets are delivered */
static void bbr_update_bw(struct sock *sk, const struct rate_sample *rs);


/* Estimate when the pipe is full, using the change in delivery rate: BBR
 * estimates that STARTUP filled the pipe if the estimated bw hasn't changed by
 * at least bbr_full_bw_thresh (25%) after bbr_full_bw_cnt (3) non-app-limited
 * rounds. Why 3 rounds: 1: rwin autotuning grows the rwin, 2: we fill the
 * higher rwin, 3: we get higher delivery rate samples. Or transient
 * cross-traffic or radio noise can go away. CUBIC Hystart shares a similar
 * design goal, but uses delay and inter-ACK spacing instead of bandwidth.
 */
static void bbr_check_full_bw_reached(struct sock *sk,
				      const struct rate_sample *rs); 


/* If pipe is probably full, drain the queue and then enter steady-state. */
static void bbr_check_drain(struct sock *sk, const struct rate_sample *rs);

/* The goal of PROBE_RTT mode is to have BBR flows cooperatively and
 * periodically drain the bottleneck queue, to converge to measure the true
 * min_rtt (unloaded propagation delay). This allows the flows to keep queues
 * small (reducing queuing delay and packet loss) and achieve fairness among
 * BBR flows.
 *
 * The min_rtt filter window is 10 seconds. When the min_rtt estimate expires,
 * we enter PROBE_RTT mode and cap the cwnd at bbr_cwnd_min_target=4 packets.
 * After at least bbr_probe_rtt_mode_ms=200ms and at least one packet-timed
 * round trip elapsed with that flight size <= 4, we leave PROBE_RTT mode and
 * re-enter the previous mode. BBR uses 200ms to approximately bound the
 * performance penalty of PROBE_RTT's cwnd capping to roughly 2% (200ms/10s).
 *
 * Note that flows need only pay 2% if they are busy sending over the last 10
 * seconds. Interactive applications (e.g., Web, RPCs, video chunks) often have
 * natural silences or low-rate periods within 10 seconds where the rate is low
 * enough for long enough to drain its queue in the bottleneck. We pick up
 * these min RTT measurements opportunistically with our min_rtt filter. :-)
 */
static void bbr_update_min_rtt(struct sock *sk, const struct rate_sample *rs);


static void bbr_update_model(struct sock *sk, const struct rate_sample *rs);


static void bbr_main(struct sock *sk, const struct rate_sample *rs); 


static void bbr_init(struct sock *sk); 

static uint32_t bbr_sndbuf_expand(struct sock *sk);

/* In theory BBR does not need to undo the cwnd since it does not
+ * always reduce cwnd on losses (see bbr_main()). Keep it for now.
+ */
static uint32_t bbr_undo_cwnd(struct sock *sk);


/* Entering loss recovery, so save cwnd for when we exit or undo recovery. */
static uint32_t bbr_ssthresh(struct sock *sk); 


static size_t bbr_get_info(struct sock *sk, uint32_t ext, int *attr, union tcp_cc_info *info);


static void bbr_set_state(struct sock *sk, uint8_t new_state);


/*static struct tcp_congestion_ops tcp_bbr_cong_ops __read_mostly = {
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "bbr",
	.owner		= THIS_MODULE,
	.init		= bbr_init,
	.cong_control	= bbr_main,
	.sndbuf_expand	= bbr_sndbuf_expand,
	.undo_cwnd	= bbr_undo_cwnd,
	.cwnd_event	= bbr_cwnd_event,
	.ssthresh	= bbr_ssthresh,
	.tso_segs_goal	= bbr_tso_segs_goal,
	.get_info	= bbr_get_info,
	.set_state	= bbr_set_state,
};*/

static int __init bbr_register(void);


static void __exit bbr_unregister(void);





#endif