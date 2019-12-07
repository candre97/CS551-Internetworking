/*****************************************************************************
 * ctcp_bbr.h
 * ------
 * Contains definitions for constants functions, and structs you will need for
 * the BBR implementation. Implementations of the functions should be done in
 * ctcp_bbr.c.
 *
 *****************************************************************************/


#ifndef CTCP_BBR_H
#define CTCP_BBR_H

#include <stdint.h>
#include "ctcp_utils.h"


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

/* Gain constants
static float BBRHighGain = 2.89; 
static float drain_pacing_gain = 1/2.89;
float pacing_gain_cycle[] = {5/4, 3/4, 1, 1, 1, 1, 1, 1};*/


/* We use a high_gain value chosen to allow a smoothly increasing pacing rate
 * that will double each RTT and send the same number of packets per RTT that
 * an un-paced, slow-starting Reno or CUBIC flow would.
 */
static const int bbr_high_gain  = BBR_UNIT * 2885 / 1000 + 1;	/* 2/ln(2) */
static const int bbr_drain_gain = BBR_UNIT * 1000 / 2885;	/* 1/high_gain */
static const int bbr_cwnd_gain  = BBR_UNIT * 2;	/* gain for steady-state cwnd */
/* The pacing_gain values for the PROBE_BW gain cycle: */
static const int bbr_pacing_gain[] = { BBR_UNIT * 5 / 4, BBR_UNIT * 3 / 4,
				 BBR_UNIT, BBR_UNIT, BBR_UNIT,
				 BBR_UNIT, BBR_UNIT, BBR_UNIT };

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

/* BBR congestion control block 
https://tools.ietf.org/id/draft-cardwell-iccrg-bbr-congestion-control-00.html
*/
typedef struct {
	bbr_mode 	mode; 
	float 		pacing_rate; 	/* The current pacing rate for a BBR flow, 
								controls inter-packet spacing.*/
	uint32_t 	send_quantum; 	/* The maximum size of a data aggregate 
								scheduled and transmitted together.*/
	uint16_t 	cwnd; 			/* the sender's congestion window */
	uint32_t 	btl_bw;			/* BBR's current estimate of btlBW */
	uint32_t 	full_bw;		/* full BW estimate */
	long 		rt_prop; 		/* BBR's estimated two-way round-trip propagation
								 delay of the path, estimated from the windowed minimum
								 recent round-trip delay sample.*/
	long 		rt_prop_stamp;	/* time of when current rt_prop was recorded */
	bool		rt_prop_expired;/* whether the BBR.RTprop has expired and is due 
								for a refresh with an application idle period or a 
								transition into ProbeRTT state*/
	float 		pacing_gain;	/* gain factor used to scale BBR.BtlBw to produce BBR.pacing_rate.*/
	float 		cwnd_gain; 		/* The dynamic gain factor used to scale the estimated 
								BDP to produce a congestion window (cwnd). */
	bool 		filled_pipe;	/* BBR's estimate, whether it has ever filled the pipe */
	uint32_t	delivered_in_round;		/* number of packets delivered by BBR in current round */
	//long	 	probe_rtt_done_stamp; /* stamp of when we are done probing for RTT */
	//bool 		probe_rtt_round_done; /* whether we are done with Probe RTT */
	//bool 		idle_restart; 
	uint8_t		full_bw_cnt; 	/* how many times there is no BW improvement */
	uint8_t 	cycle_index; 	/* pacing gain index for probe_bw*/
	uint32_t	rtt_cnt; 		/* need to update BW estimate every 10 */
	uint32_t	rtts_in_mode;  /* number of RTT's spent in the phase */
	long 		next_packet_send_time; 
} bbr_t;

bbr_t* bbr_init(uint16_t in_cwnd);

void bbr_enter_startup(bbr_t* bbr);

void bbr_enter_drain(bbr_t* bbr);

void bbr_enter_probe_bw(bbr_t* bbr);

void bbr_cycle_gain_probe_bw(bbr_t* bbr);

void bbr_check_probe_rtt(bbr_t* bbr);

void bbr_enter_probe_rtt(bbr_t* bbr);

void bbr_set_cwnd(bbr_t* bbr);

void bbr_check_full_pipe(bbr_t* bbr);

void bbr_update_rtt(bbr_t* bbr, long rtt);

void bbr_update_btl_bw(bbr_t* bbr, long rtt, int seg_len);

void bbr_next_send_time(bbr_t* bbr, int seg_len);

void bbr_on_ack(bbr_t* bbr, long rtt, int seg_len);

#endif
