/*****************************************************************************
 * ctcp_bbr.h
 * ------
 * Contains definitions for constants functions, and structs you will need for
 * the BBR implementation. Implementations of the functions should be done in
 * ctcp_bbr.c.
 *
 *****************************************************************************/
#include <stdint.h>
#include "ctcp_utils.h"
#include "ctcp.h"

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


/* Constants go here! */
/*A constant specifying the length of the BBR.BtlBw 
max filter window for BBR.BtlBwFilter, 
BtlBwFilterLen is 10 packet-timed round trips.*/
uint16_t BtlBwFilterLen = 10;
/* length of the RTProp min filter window */
long RTpropFilterLen = 10*1000; 	/* 10 second in ms*/
const int BBRGainCycleLen = 8;		/* the number of phases in the BBR ProbeBW gain cycle: 8.*/
const int BBRMinPipeCwnd = 4;		/* min cwnd that BBR will use */
long ProbeRTTInterval = 10*1000;	/* minimum time interval b/t ProbeRTT states */
long ProbeRTTDuration = 200;		/* A constant specifying the minimum duration for which 
									ProbeRTT state holds inflight to BBRMinPipeCwnd or 
									fewer packets: 200 ms.*/
uint32_t SMSS = 1500;				/* The Sender Maximum Segment Size. */

/* Gain constants */
static float BBRHighGain = 2.89; /* (2/ln(2) ~= 2.89) */
static float drain_pacing_gain = 1/2.89;
float pacing_gain_cycle[] = {5/4, 3/4, 1, 1, 1, 1, 1, 1};


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
	uint32_t	btl_bw_filter_val;	/* max filter used to estimate btl_bw */
	long 		btl_bw_filter_time;	
	uint32_t 	full_bw; 
	uint16_t 	full_bw_count; 
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
	uint16_t 	initial_cwnd; 	/* initial value of cwnd given at initialization */
	uint16_t 	target_cwnd;	/* cwnd that you want to go towards */
	bool 		filled_pipe;	/* BBR's estimate, whether it has ever filled the pipe */
	uint16_t	round_count;	/* count of packet times round trips */
	bool 		round_start; 	/*A boolean that BBR sets to true once per packet-timed 
								round trip, on ACKs that advance BBR.round_count.*/
	uint32_t	next_round_delivered;/* packet.delivered value denoting the end of a 
								packet-timed round trip.*/
	uint32_t	delivered;		/* number of packets delivered by BBR */
	bool 		is_app_limited; /* bool to track if you are app limited */
	bool 		pacing_rate_init;/* to track whether the pacing rate has been initialized */
	bool 		packet_conservation; /* whether youre in packet conservation mode */
	uint16_t 	probe_rtt_done_stamp; /* stamp of when we are probing for RTT */
	bool 		probe_rtt_round_done; /* whether we are done with Probe RTT */
	bool 		idle_restart; 
	uint16_t 	cycle_index; 
} bbr_t;

void BBRUpdateModelAndState(bbr_t* bbr, uint32_t delivery_rate);
void BBRUpdateBtlBw(bbr_t* bbr, uint32_t delivery_rate);
void BBRInitPacingRate(bbr_t* bbr, uint16_t InitialCwnd);
void BBRSetPacingRate(bbr_t* bbr, ctcp_segment_t* segment);
void BBRModulateCwndForProbeRTT(bbr_t* bbr);
uint16_t BBRSaveCwnd(bbr_t* bbr);

void BBRUpdateOnRTO(bbr_t* bbr);
/* 
TODO: implement these functions: 
void uponEnteringFastRecovery()
  BBR.prior_cwnd = BBRSaveCwnd()
  cwnd = packets_in_flight + max(packets_delivered, 1)
  BBR.packet_conservation = true

void onACKinFastRecovery()
BBRModulateCwndForRecovery():
    if (packets_lost > 0)
      cwnd = max(cwnd - packets_lost, 1)
    if (BBR.packet_conservation)
      cwnd = max(cwnd, packets_in_flight + packets_delivered)

After one trip in fast recovery: 
  BBR.packet_conservation = false

After exiting fast recovery:
	BBR.packet_conservation = false
	BBRRestoreCwnd()
*/

void BBRSetSendQuantum(bbr_t* bbr);
void BBRSetCwnd(bbr_t* bbr, uint16_t ackd_packets);
uint16_t BBRInFlight(bbr_t* bbr, float gain);
void BBRUpdateTargetCwnd(bbr_t* bbr);
void BBRUpdateControlParameters(bbr_t* bbr, ctcp_segment_t* segment, uint16_t ackd_packets);
void BBRUpdateRound(bbr_t* bbr, ctcp_state_t* state, ctcp_segment_t* segment);
/* function called on received ACK */
void BBRUpdateOnACK(bbr_t* bbr, ctcp_state_t* state, 
	ctcp_segment_t* segment, uint16_t ackd_packets);
void BBRInitRoundCounting(bbr_t* bbr);
/* initialize a bbr struct with initial variables */
void bbr_init(bbr_t* bbr, uint16_t recv_window, ctcp_state_t* state);
void BBRCheckFullPipe(bbr_t* bbr);
void BBRCheckDrain(bbr_t* bbr, uint16_t packets_in_flight);
// source: https://www.geeksforgeeks.org/generating-random-number-range-c/
void random_int_in_range(int lower, int upper);
void BBREnterProbeBW(bbr_t* bbr);
void BBRCheckCyclePhase(bbr_t* bbr);
bool BBRIsNextCyclePhase(bbr_t* bbr, uint32_t prior_inflight, uint32_t packets_lost);
void BBRAdvanceCyclePhase(bbr_t* bbr);

void BBREnterDrain(bbr_t* bbr);
void BBRInitFullPipe(bbr_t* bbr);
void BBREnterStartup(bbr_t* bbr);
/* update the BW max value */
void BBUpdateBtlBw(bbr_t* bbr, uint32_t delivery_rate, 
	ctcp_state_t* state, ctcp_segment_t* segment);
/* update the RTT min value */
void BBRUpdateRTprop(bbr_t* bbr, long rtt);
void BBRSetPacingRateWithGain(float pacing_gain);
void BBRHandleRestartFromIdle(bbr_t* bbr, uint32_t packets_in_flight);
void BBRCheckProbeRTT(bbr_t* bbr, uint32_t packets_in_flight);
void BBREnterProbeRTT(bbr_t* bbr);
void BBRHandleProbeRTT(bbr_t* bbr, uint32_t packets_in_flight);
void BBRExitProbeRTT(bbr_t* bbr);
uint32_t BBROnTransmit(bbr_t* bbr);

#endif

// old BBR Block
// uint32_t	min_rtt_ms;	        		/* min RTT in min_rtt_win_sec window */
// 	long		min_rtt_stamp;	        	/* timestamp of min_rtt_ms */
// 	uint32_t	probe_rtt_done_stamp;   	/* end time for BBR_PROBE_RTT mode */
// 	uint32_t	rtt_cnt;	    			/* count of packet-timed rounds elapsed */
// 	uint32_t    next_rtt_delivered; 		/* scb->tx.delivered at end of round */
// 	bbr_mode 	mode,		     /* current bbr_mode in state machine */
// 	uint32_t	prev_ca_state:3,     /* CA state on previous ACK */
// 		packet_conservation:1,  /* use packet conservation? */
// 		restore_cwnd:1,	     /* decided to revert cwnd to old value */
// 		round_start:1,	     /* start of packet-timed tx->ack round? */
// 		tso_segs_goal:7,     /* segments we want in each skb we send */
// 		idle_restart:1,	     /* restarting after idle? */
// 		probe_rtt_round_done:1,  /* a BBR_PROBE_RTT round at 4 pkts? */
// 		unused:5,
// 		lt_is_sampling:1,     taking long-term ("LT") samples now? 
// 		lt_rtt_cnt:7,	     /* round trips in long-term interval */
// 		lt_use_bw:1;	     /* use lt_bw as our bw estimate? */
// 	uint32_t	lt_bw;		     /* LT est delivery rate in pkts/uS << 24 */
// 	uint32_t	lt_last_delivered;   /* LT intvl start: tp->delivered */
// 	uint32_t	lt_last_stamp;	     /* LT intvl start: tp->delivered_mstamp */
// 	uint32_t	lt_last_lost;	     /* LT intvl start: tp->lost */
// 	uint32_t	pacing_gain,	/* current gain for setting pacing rate */
// 		cwnd_gain,	/* current gain for setting cwnd */
// 		full_bw_cnt,	/* number of rounds without large bw gains */
// 		cycle_idx;	/* current index in pacing_gain cycle array */
// 	uint32_t	prior_cwnd;	/* prior cwnd upon entering loss recovery */
// 	uint32_t	full_bw;	/* recent bw, to estimate if pipe is full, 
// 							take 90% of this value */
// 	uint32_t 	data_sent_in_phase 			/* data sent during this mode 