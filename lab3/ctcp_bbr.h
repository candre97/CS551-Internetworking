/******************************************************************************
 * ctcp_bbr.h
 * ------
 * Contains definitions for constants functions, and structs you will need for
 * the BBR implementation. Implementations of the functions should be done in
 * ctcp_bbr.c.
 *
 *****************************************************************************/
#include <stdint.h>
#include "ctcp_utils.h"

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
typedef enum {
	BBR_STARTUP,	/* ramp up sending rate rapidly to fill pipe */
	BBR_DRAIN,	/* drain any queue created during startup */
	BBR_PROBE_BW,	/* discover, share bw: pace around estimated bw */
	BBR_PROBE_RTT,	/* cut cwnd to min to probe min_rtt */
} bbr_mode;


/* BBR congestion control block */
typedef struct {
	uint32_t	min_rtt_ms;	        		/* min RTT in min_rtt_win_sec window */
	uint32_t	min_rtt_stamp;	        	/* timestamp of min_rtt_ms */
	uint32_t	probe_rtt_done_stamp;   	/* end time for BBR_PROBE_RTT mode */
	uint32_t	rtt_cnt;	    			/* count of packet-timed rounds elapsed */
	uint32_t    next_rtt_delivered; 		/* scb->tx.delivered at end of round */
	bbr_mode 	mode,		     /* current bbr_mode in state machine */
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

/* function called on ACK (pg 23 of notes) */
void on_ack(bbr_t* bbr);

/* initialize a bbr struct with initial variables */
void bbr_init(bbr_t* bbr); 

/* update the BW max value */
void bbr_update_bw(bbr_t* bbr, long rtt, int bytes); 

/* update the RTT min value */
void bbr_update_rtt(bbr_t* bbr, long rtt); 

/* handles top level cycling through all of the modes */
void bbr_main(bbr_t* bbr); 


#endif