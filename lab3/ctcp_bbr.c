/* empty ctcp_bbr.c */

#include "ctcp_bbr.h"

void BBRUpdateModelAndState(bbr_t* bbr, uint32_t delivery_rate) {
	/* calculate delivery rate */
	BBRUpdateBtlBw(bbr_t* bbr, delivery_rate);
    BBRCheckCyclePhase(bbr_t* bbr);
    BBRCheckFullPipe(bbr_t* bbr);
    BBRCheckDrain(bbr_t* bbr);
    BBRUpdateRTprop(bbr_t* bbr);
    BBRCheckProbeRTT(bbr_t* bbr);
}

void BBRUpdateBtlBw(bbr_t* bbr, uint32_t delivery_rate) {
	BBRUpdateRound(bbr);
	if (bbr->delivery_rate >= bbr->btl_bw || !bbr->is_app_limited) {
	    bbr->BtlBw = max(delivery_rate, bbr->btl_bw);
	}
}

void BBRInitPacingRate(bbr_t* bbr, uint16_t InitialCwnd) {
	int nominal_bandwidth = InitialCwnd / (SRTT ? SRTT : 1);
    bbr->pacing_rate =  bbr->pacing_gain * nominal_bandwidth;
    bbr->pacing_rate_init = true;
}

void BBRSetPacingRate(bbr_t* bbr, ctcp_segment_t* segment) {
	
	if(!bbr->pacing_rate_init) {
		BBRInitPacingRate(bbr, ntohs(segment->window));
		return;
	}

	/* 	
		To adapt to the bottleneck, in general BBR sets the 
		pacing rate to be proportional to BBR.BtlBw, with a dynamic gain, 
		or scaling factor of proportionality, called pacing_gain.
	*/
	float rate;
	rate = bbr->pacing_gain * bbr->btl_bw;
	if(bbr->filled_pipe || (rate > bbr->pacing_rate)) {
		bbr->pacing_rate = rate;
	}

}

void BBRModulateCwndForProbeRTT(bbr_t* bbr) {
	if (bbr->mode == BBR_Probe_RTT) {
    	bbr->cwnd = min(bbr->cwnd, 4);
	}
}


uint16_t BBRSaveCwnd(bbr_t* bbr) {
	if (/*not InLossRecovery()*/ bbr->bbr_mode != BBR_PROBE_RTT) {
    	return bbr->cwnd;
	}
    else {
    	return max(bbr->prior_cwnd, cwnd);
    }
}

void BBRUpdateOnRTO(bbr_t* bbr) {
	bbr->prior_cwnd = BBRSaveCwnd(bbr);
	bbr->cwnd = 1;
	bbr->packet_conservation = true; 
}

/* BBR.
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

void BBRSetSendQuantum(bbr_t* bbr) {
	fprintf(stderr, "Pacing Rate %d\n", bbr->pacing_rate);
	int MBps = 10^3/8; /* scaled to mbp ms */
	if (bbr->pacing_rate < 1.2 * MBps) {
		bbr->send_quantum = 1 * SMSS;
	}
    else if (bbr->pacing_rate < 24 * MBps) {
      	bbr->send_quantum  = 2 * SMSS;
    }
    else { /* Bytes  */
      	bbr->send_quantum  = min(bbr->pacing_rate * 1, 64000)
    }
}

void BBRSetCwnd(bbr_t* bbr, uint16_t ackd_packets) {
	BBRUpdateTargetCwnd(bbr);
	/* modulateCwndForRecovery() */
	if(!bbr->packet_conservation) {
		if(bbr->filled_pipe) {
			bbr->cwnd = min(bbr->cwnd + ackd_packets, bbr->target_cwnd); 
		}
		else if(bbr->cwnd < bbr->target_cwnd || bbr->delivered < bbr->initial_cwnd) {
			bbr->cwnd += ackd_packets; 
		}
		/* impose a floor on cwnd */
		bbr->cwnd = max(bbr->cwnd, BBRMinPipeCwnd);
	}
	bbr->packet_conservation = false;
	BBRModulateCwndForProbeRTT(bbr);
}

uint16_t BBRInFlight(bbr_t* bbr, float gain) {
	if(bbr->rt_prop < 0) {
		bbr->target_cwnd = bbr->initial_cwnd;
		return;
	}
	uint32_t quanta = 3 * bbr->send_quantum;
	uint32_t est_bdp = bbr->btl_bw * bbr->rt_prop;
	return ((est_bdp * bbr->cwnd_gain) + quanta) / SMSS;  
	/* to go from bytes to a multiplier of SMSS*/
}

void BBRUpdateTargetCwnd(bbr_t* bbr) {
	bbr->target_cwnd == BBRInFlight(bbr->cwnd_gain); 
	return;
}

void BBRUpdateControlParameters(bbr_t* bbr, ctcp_segment_t* segment, uint16_t ackd_packets) {
	bbr->next_send_time = current_time() + (ntohs(segment->len) / bbr->pacing_rate);
	BBRSetPacingRate(bbr);
    BBRSetSendQuantum(bbr);
    BBRSetCwnd(bbr, ackd_packets);
}

void BBRUpdateRound(bbr_t* bbr, ctcp_state_t* state, ctcp_segment_t* segment) {
	bbr->delivered += ntohs(segment->len);
	if(segment->seqno >= bbr->next_round_delivered) {
		bbr->next_round_delivered = bbr->delivered;
		bbr->round_count++;
		bbr->round_start = true;
	}
	else {
		bbr->round_start = false;
	}
} 

/* function called on received ACK */
void BBRUpdateOnACK(bbr_t* bbr, ctcp_state_t* state, 
	ctcp_segment_t* segment, uint16_t ackd_packets) {
	BBRUpdateModelAndState(bbr);
	BBRUpdateControlParameters(bbr, segment, ackd_packets);
	BBRUpdateRound(bbr, state, segment); 
}

void BBRInitRoundCounting(bbr_t* bbr) {
	bbr->next_round_delivered = 0;
	bbr->round_start = false;
	bbr->round_count = 0;
}

/* initialize a bbr struct with initial variables */
void bbr_init(bbr_t* bbr, uint16_t recv_window, ctcp_state_t* state) {
	/* set the variables to initial state */
	bbr->btl_bw_filter_val = 0;
	bbr->btl_bw_filter_time = 0;
	bbr->rt_prop = -1;
	bbr->rtprop_stamp = current_time();
	bbr->probe_rtt_done_stamp = 0;
	bbr->probe_rtt_round_done = false;
	bbr->packet_conservation = false;
	bbr->prior_cwnd = 0;
	bbr->idle_restart = false;
	bbr->initial_cwnd = recv_window;
	if(bbr->initial_cwnd < 4) {
		bbr->initial_cwnd = 4;
	}
	bbr->cwnd = bbr->initial_cwnd;

	BBREnterStartup(bbr); 
	BBRInitRoundCounting(bbr);
	BBRInitFullPipe(bbr);
	BBRInitPacingRate(bbr, recv_window);
	
}

void BBRCheckFullPipe(bbr_t* bbr) {
	if(bbr->filled_pipe || !bbr->round_start || bbr->is_app_limited) {
		/* no need to check */
		return;
	}
	else if(bbr->btl_bw >= bbr->full_bw * 1.25) { /* BW still growing? */
		bbr->full_bw = bbr->btl_bw;
		bbr->full_bw_count = 0;
		return;	
	}
	bbr->full_bw_count++;
	if(bbr->full_bw_count >= 3) {
		bbr->filled_pipe = true; 
	}
}

void BBRCheckDrain(bbr_t* bbr, uint16_t packets_in_flight) {
	if(bbr->mode == BBR_STARTUP && bbr->filled_pipe) {
		BBREnterDrain(bbr);
		return;
	}
	if(bbr->mode == BBR_DRAIN && packets_in_flight <= BBRInFlight(1.0)) {
		BBREnterProbeBW(bbr); 
	}
}

// source: https://www.geeksforgeeks.org/generating-random-number-range-c/
void random_int_in_range(int lower, int upper) 
{ 
	return (rand() % (upper - lower + 1)) + lower; 
} 

void BBREnterProbeBW(bbr_t* bbr) {
    bbr->state = BBR_PROBE_BW; 
    bbr->pacing_gain = 1;
    bbr->cwnd_gain = 2;
    bbr->cycle_index = BBRGainCycleLen - 1 - random_int_in_range(0,6)
    BBRAdvanceCyclePhase(bbr);
}

void BBRCheckCyclePhase(bbr_t* bbr) {
	if(bbr->mode == BBR_PROBE_BW && BBRIsNextCyclePhase(bbr, prior_inflight, packets_lost)) {
		BBRAdvanceCyclePhase(bbr); 
	}
}

bool BBRIsNextCyclePhase(bbr_t* bbr, uint32_t prior_inflight, uint32_t packets_lost) {
    bool is_full_length = (current_time() - bbr->cycle_stamp) > bbr->rt_prop;
    if(bbr->pacing_gain == 1)
    	return is_full_length; 
    if(bbr->pacing_gain > 1) {
    	return (is_full_length && 
    	((packets_lost > 0) || prior_inflight >= BBRInFlight(bbr->pacing_gain)));
    }
    else  //  (BBR.pacing_gain < 1)
      	return (is_full_length || (prior_inflight <= BBRInflight(1)));
}

void BBRAdvanceCyclePhase(bbr_t* bbr) {
    bbr->cycle_stamp = current_time();
    bbr->cycle_index = (bbr->cycle_index + 1) % BBRGainCycleLen
    bbr->pacing_gain = pacing_gain_cycle[bbr->cycle_index];
}

void BBREnterDrain(bbr_t* bbr) {
	bbr->mode = BBR_DRAIN; 
	bbr->pacing_gain = drain_pacing_gain; 	// pace slowly
	bbr->cwnd_gain = BBRHighGain; 			// maintain cwnd
}

void BBRInitFullPipe(bbr_t* bbr) {
	bbr->filled_pipe = false; 
	bbr->full_bw = 0;
	bbr->full_bw_count = 0;
}

void BBREnterStartup(bbr_t* bbr) {
	bbr->mode = BBR_STARTUP; 
	bbr->pacing_gain = BBRHighGain; 
	bbr->cwnd_gain = BBRHighGain; 
} 

/* update the BW max value */
void BBUpdateBtlBw(bbr_t* bbr, uint32_t delivery_rate, 
	ctcp_state_t* state, ctcp_segment_t* segment) {
	// bbr->rtt_cnt++;
	BBRUpdateRound(bbr, ctcp_state_t* state, ctcp_segment_t* segment); 
	if(delivery_rate >= bbr->btl_bw || !bbr->is_app_limited) {
		bbr->btl_bw = max(delivery_rate, bbr->btl_bw); 
	}
}

/* update the RTT min value */
void BBRUpdateRTprop(bbr_t* bbr, long rtt) {
	if(current_time() > (bbr->rtprop_stamp + RTpropFilterLen)) {
		bbr->rtprop_expired = true;
	}
	if(rtt >= 0 && (rtt <= bbr->rt_prop || bbr->rtprop_expired)) {
		bbr->rt_prop = rtt;
		bbr->rtprop_stamp = current_time();
		bbr->rtprop_expired = false;
	}	
}

void BBRSetPacingRateWithGain(float pacing_gain) {
	if(!bbr->pacing_rate_init) {
		BBRInitPacingRate(bbr, ntohs(segment->window));
		return;
	}

	float rate;
	rate = bbr->pacing_gain * bbr->btl_bw;
	if(bbr->filled_pipe || (rate > bbr->pacing_rate)) {
		bbr->pacing_rate = rate;
	}
}

void BBRHandleRestartFromIdle(bbr_t* bbr, uint32_t packets_in_flight) {
    if(packets_in_flight == 0 && bbr->is_app_limited) {
    	bbr->idle_restart = true;
    	if(bbr->mode == BBR_PROBE_BW) {
    		BBRSetPacingRateWithGain(1);
    	}
    }
}

/* TODO 

1. translate following sudo code to real code
2. fix header file to account for all these new functions
3. implement BBR changes in ctcp.c
*/

void BBRCheckProbeRTT(bbr_t* bbr, uint32_t packets_in_flight) {
    if (bbr->mode != BBR_PROBE_BW && bbr->rtprop_expired && !bbr->idle_restart) {
		BBREnterProbeRTT(bbr);
		BBRSaveCwnd(bbr); 
		bbr->probe_rtt_done_stamp = 0;
  	}
    if (bbr->mode == ProbeRTT) {
      BBRHandleProbeRTT(bbr, packets_in_flight); 
    }
    bbr->idle_restart = false;
}

void BBREnterProbeRTT(bbr_t* bbr) {
    bbr->mode = BBR_PROBE_BW;
    bbr->pacing_gain = 1;
    bbr->cwnd_gain = 1;
}

void BBRHandleProbeRTT(bbr_t* bbr, uint32_t packets_in_flight) {
    /* Ignore low rate samples during ProbeRTT: */
    bbr->is_app_limited = (bbr->delivered + packets_in_flight) ? : 1; 
    if (bbr->probe_rtt_done_stamp == 0 && packets_in_flight <= BBRMinPipeCwnd) {
		bbr->probe_rtt_done_stamp = current_time() + ProbeRTTDuration; 
		bbr->probe_rtt_round_done = false; 
		bbr->next_round_delivered = bbr->delivered;
  	}
    else if (bbr->probe_rtt_done_stamp != 0) {
		if (bbr->round_start) {
			bbr->probe_rtt_round_done = true;
		}
		if (bbr->probe_rtt_round_done && current_time() > bbr->probe_rtt_done_stamp) {
			bbr->rtprop_stamp = current_time();
			BBRRestoreCwnd(bbr);
			BBRExitProbeRTT(bbr);
		}
    }
}

void BBRExitProbeRTT(bbr_t* bbr) {
    if (bbr->filled_pipe) {
      BBREnterProbeBW(bbr);
    }
    else {
      BBREnterStartup(bbr); 
    }
}

uint32_t BBROnTransmit(bbr_t* bbr) {
	BBRHandleRestartFromIdle(bbr);
	return bbr->delivered;
}

