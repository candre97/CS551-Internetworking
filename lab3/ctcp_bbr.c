/* empty ctcp_bbr.c */

#include "ctcp_bbr.h"

/* function called on ACK (pg 23 of notes) */
void on_ack(bbr_t* bbr) {
	/* calculate RTT */

	/* update min filter */

	/* update number of delivered packets */

	long delivered_time = current_time();
}

/* initialize a bbr struct with initial variables */
void bbr_init(bbr_t* bbr) {
	/* set the variables to initial state */
}

/* update the BW max value */
void bbr_update_bw(bbr_t* bbr, long rtt, int bytes); 

/* update the RTT min value */
void bbr_update_rtt(bbr_t* bbr, long rtt); 

/* handles top level cycling through all of the modes */
void bbr_main(bbr_t* bbr) {

}
