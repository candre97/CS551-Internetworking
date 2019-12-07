/* empty ctcp_bbr.c */

#include "ctcp_bbr.h"

/* Source: https://stackoverflow.com/questions/3437404/min-and-max-in-c */
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

bbr_t* bbr_init(uint16_t in_cwnd) {

	bbr_t* bbr = malloc(sizeof(bbr_t)); 
	/* set the variables to initial state */
	bbr->cwnd = 4; 
	bbr->btl_bw = 0; 
	bbr->full_bw = 0;
	bbr->rt_prop = 200; 
	bbr->rt_prop_stamp = current_time();
	bbr->rt_prop_expired = false;
	bbr->filled_pipe = false;
	bbr->delivered_in_round = 0;
	bbr->cycle_index = (rand() % (7 - 2 + 1)) + 2; 
	bbr->rtt_cnt = 0;
	bbr->rtts_in_mode = 0;
	bbr->bbr_log = fopen("bbr_logger.txt", "w"); 
	
	bbr_enter_startup(bbr);
	return bbr; 
}

void bbr_enter_startup(bbr_t* bbr) {
	bbr->mode = BBR_STARTUP; 
	bbr->pacing_gain = bbr_high_gain;
	bbr->cwnd_gain = bbr_high_gain;

	bbr->delivered_in_round = 0;
	bbr->rtts_in_mode = 0;
}

void bbr_enter_drain(bbr_t* bbr) {
	bbr->mode = BBR_DRAIN; 
	bbr->pacing_gain = bbr_drain_gain; 
	bbr->cwnd_gain = bbr_high_gain; 

	bbr->full_bw_cnt = 0;
	bbr->delivered_in_round = 0;
	bbr->rtts_in_mode = 0;
}

void bbr_enter_probe_bw(bbr_t* bbr) {
	bbr->mode = BBR_PROBE_BW; 
	bbr->cycle_index = (rand() % (7 - 2 + 1)) + 2; 
	bbr->pacing_gain = bbr_pacing_gain[bbr->cycle_index]; 
	bbr->cwnd_gain = bbr_cwnd_gain;
	bbr->rtts_in_mode = 0;
	bbr->delivered_in_round = 0;
}

void bbr_cycle_gain_probe_bw(bbr_t* bbr) {
	bbr->cycle_index++; 
	if(bbr->cycle_index > 7) {
		bbr->cycle_index = 0;
	}

	bbr->pacing_gain = bbr_pacing_gain[bbr->cycle_index]; 
}

void bbr_check_probe_rtt(bbr_t* bbr) {
	bbr->rt_prop_expired = current_time() - bbr->rt_prop_stamp > 10000; 
	if(bbr->mode != BBR_PROBE_RTT && bbr->rt_prop_expired) {
		bbr->rt_prop = -1;
		bbr_enter_probe_rtt(bbr); 
	}
}

void bbr_enter_probe_rtt(bbr_t* bbr) {
	bbr->mode = BBR_PROBE_RTT; 
	bbr->pacing_gain = BBR_UNIT; 
	bbr->cwnd_gain = BBR_UNIT; 
	bbr->filled_pipe = false;
	bbr->rtts_in_mode = 0;
}

void bbr_set_cwnd(bbr_t* bbr) {
	bbr->cwnd = bbr->rt_prop * bbr->btl_bw * bbr->pacing_gain * SMSS; 
}

void bbr_check_full_pipe(bbr_t* bbr) {
	if(bbr->filled_pipe) {
		return;
	}
	/* BW still growing? */
	if(bbr->btl_bw >= bbr->full_bw * 1.25) {
		bbr->full_bw = bbr->btl_bw;
		bbr->full_bw_cnt = 0;
	}
	else {
		bbr->full_bw_cnt++;
		if(bbr->full_bw_cnt > 2) {
			bbr->filled_pipe = true;
		}
	}
}

void bbr_update_rtt(bbr_t* bbr, long rtt) {
	
	fprintf(stderr, "RTT:%ld\n", rtt);

	if(rtt < bbr->rt_prop) {
		bbr->rt_prop = rtt; 
		bbr->rt_prop_stamp = current_time(); 

		/* done probing, pipe obviously not full */
		if(bbr->mode == BBR_PROBE_RTT) {
			bbr_enter_startup(bbr); 
		}
	}
}


void bbr_update_btl_bw(bbr_t* bbr, long rtt, int seg_len) {
	
	if(rtt <= 0) {
		fprintf(stderr, "RTT is negative or == 0\n"); 

		return;
	}
	bbr->btl_bw = seg_len / rtt; 
	bbr->full_bw = max(bbr->btl_bw, bbr->full_bw);
	fprintf(stderr, "btl_bw:%ld\n", (long ) bbr->btl_bw);
}

void bbr_next_send_time(bbr_t* bbr, int seg_len) {
	long time_to_wait = (long)(seg_len/(bbr->pacing_gain * bbr->btl_bw)); 
	bbr->next_packet_send_time = current_time() + time_to_wait; 
}


void bbr_on_ack(bbr_t* bbr, long rtt, int seg_len) {

	bbr->rtt_cnt++;

	bbr_update_btl_bw(bbr, rtt, seg_len); 
	bbr_check_probe_rtt(bbr); 
	bbr_check_full_pipe(bbr); 
	bbr_set_cwnd(bbr);
	bbr_next_send_time(bbr, seg_len); 
	
	switch(bbr->mode) {
		case BBR_STARTUP:

			if(bbr->filled_pipe) {
				bbr_enter_drain(bbr); 
			}
			break;
		case BBR_DRAIN:
			bbr->rtts_in_mode++;
			/* Drain the queues for 5 RTT's */
			if(bbr->rtts_in_mode > 1) {
				bbr_enter_probe_bw(bbr); 
			}
			break; 
		case BBR_PROBE_BW:

			bbr_cycle_gain_probe_bw(bbr); 
			break; 
		case BBR_PROBE_RTT:
			bbr->rtts_in_mode++;
			if(bbr->filled_pipe) {
				bbr_enter_probe_bw(bbr); 
			}
			if(bbr->rtts_in_mode > 5) {
				bbr_enter_startup(bbr); 
			}
			break;
		default: 
			fprintf(stderr, "State Transition Error, going to drain state. \n");
			bbr_enter_drain(bbr); 
			break;
	}

	bbr_update_rtt(bbr, rtt); 
	fprintf(bbr->bbr_log, "%i,%ld,%i,%u\n", bbr->mode, rtt, seg_len / rtt, bbr->cwnd);
	fflush(bbr->bbr_log); 

	if(bbr->cwnd < 4) {
		bbr->cwnd = 4; 
	}
}

