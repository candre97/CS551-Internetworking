/* empty ctcp_bbr.c */

#include "ctcp_bbr.h"

void BBRUpdateModelAndState(bbr_t* bbr) {
	/* calculate delivery rate */
	BBRUpdateBtlBw(bbr_t* bbr);
    BBRCheckCyclePhase(bbr_t* bbr);
    BBRCheckFullPipe(bbr_t* bbr);
    BBRCheckDrain(bbr_t* bbr);
    BBRUpdateRTprop(bbr_t* bbr);
    BBRCheckProbeRTT(bbr_t* bbr);
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

	/* 	TODO:
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

void BBRSetSendQuantum(bbr_t* bbr) {
	fprintf(stderr, "Pacing Rate %d\n", bbr->pacing_rate);
	if (bbr->pacing_rate < 1.2 Mbps) {
		BBR.send_quantum = 1 * MSS
	}
    else if (BBR.pacing_rate < 24 Mbps) {
      BBR.send_quantum  = 2 * MSS
    }
    else {
      BBR.send_quantum  = min(BBR.pacing_rate * 1ms, 64KBytes)
    }
}

void BBRUpdateControlParameters(bbr_t* bbr, ctcp_segment_t* segment) {
	bbr->next_send_time = current_time() + (ntohs(segment->len) / bbr->pacing_rate);
	BBRSetPacingRate(bbr_t* bbr);
    BBRSetSendQuantum(bbr_t* bbr);
    BBRSetCwnd(bbr_t* bbr);
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
void BBRUpdateOnACK(bbr_t* bbr, ctcp_state_t* state, ctcp_segment_t* segment) {
	BBRUpdateModelAndState(bbr);
	BBRUpdateControlParameters(bbr, ctcp_segment_t* segment);
	BBRUpdateRound(bbr, state, segment); 
}

void BBRInitRoundCounting(bbr_t* bbr) {
	bbr->next_round_delivered = 0;
	bbr->round_start = false;
	bbr->round_count = 0;
}

/* initialize a bbr struct with initial variables */
void bbr_init(bbr_t* bbr, uint16_t recv_window) {
	/* set the variables to initial state */
	BBRInitRoundCounting(bbr);
	BBRInitPacingRate(bbr, recv_window)
}

/* update the BW max value */
void BBUpdateBtlBw(bbr_t* bbr, uint32_t delivery_rate, 
	ctcp_state_t* state, ctcp_segment_t* segment) {
	// bbr->rtt_cnt++;
	BBRUpdateRound(bbr, ctcp_state_t* state, ctcp_segment_t* segment); 
	if(delivery_rate >= bbr->btl_bw || !bbr->is_app_limited) {
		bbr->btl_bw = update_windowed_max_filter(
                      filter=BBR.BtlBwFilter,
                      value=delivery_rate,
                      time=BBR.round_count,
                      window_length=BtlBwFilterLen);
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

void BBRHandleRestartFromIdle(bbr_t* bbr) {

}

uint32_t BBROnTransmit(bbr_t* bbr) {
	BBRHandleRestartFromIdle(bbr);
	return bbr->delivered;
}




/* 
	Questions for office hours
	1. can we overview BBR quickly just to make sure i know 
	everything ill need to implement
	2. bbr_t struct -- what is going in it, am i set up well?
	3. where is the appropriate spot to cycle bbr_mode?
*/


	// switch(bbr->mode) {
	// 	case BBR_STARTUP: /* ramp up sending rate rapidly to fill pipe */

	// 		if(current_time() - bbr->min_rtt_stamp > 10000) {
	// 			bbr->mode = BBR_PROBE_RTT;
	// 			bbr->data_sent_in_phase = 0;
	// 			return;
	// 		}
	// 		else if(/* BW does not increase */) {
	// 			bbr->mode = BBR_DRAIN;
	// 			bbr->data_sent_in_phase = 0;
	// 			return;
	// 		}

	// 		break;
	// 	case BBR_DRAIN:	/* drain any queue created during startup */

	// 		if(current_time() - bbr->min_rtt_stamp > 10000) {
	// 			bbr->mode = BBR_PROBE_RTT;
	// 			bbr->data_sent_in_phase = 0;
	// 			return;
	// 		}
	// 		else if(/* inflight <= BDP */) {
	// 			bbr->mode = BBR_PROBE_BW; 
	// 			bbr->data_sent_in_phase = 0;
	// 			return; 
	// 		}
	// 		break;
	// 	case BBR_PROBE_BW:	/* discover, share bw: pace around estimated bw */
	// 		if(current_time() - bbr->min_rtt_stamp > 10000) {
	// 			bbr->mode = BBR_PROBE_RTT;
	// 			bbr->data_sent_in_phase = 0;
	// 			return;
	// 		}
	// 		break;
	// 	case BBR_PROBE_RTT: /* cut cwnd to min to probe min_rtt */
			
	// 		if(bbr->)

	// 		break;
	// }
