/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

#define DEBUG 1
#define MAX_TRANSMITS 5

/* Add useful and needed info to a segment */
typedef struct {
  ctcp_segment_t* seg; 
  long            time_last_sent;
  uint8_t         times_transmitted;
} wrapped_seg_t; 

/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */

  linked_list_t* unackd_segments; /* segment has been stdin'd, but not sent */

  linked_list_t* unoutputted_segments; /* received, not yet sent to stdout */
  /* FIXME: Add other needed fields. */

  ctcp_config_t* config;     /* Settings for this configuration */

  /* variables to track our sending to the connection */
  uint32_t byte_sent;        /* latest sent byte (in bytes) */
  uint32_t byte_input;        /* latest byte from stdin */
  uint32_t ack_recd;         /* latest byte ACK'd by connection (you recv this) */
  bool eof_stdind;             /* true when you've read in an EOF from stdin */
  bool fin_sent;            /* true when you've sent a fin (after getting an EOF) */
  long fin_wait;            /* holds the start of the TIME WAIT -- see notes for details */

  /* Receiving variables */
  bool fin_rxd;             /* true when you receive a FIN flag */
  uint32_t ack_sent;    /* byte that you last ACK'd (you sent this) */

};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */


/* 
  ctcp_init(): Initialize state associated with a connection. 
  This is called by the library when a new connection is made.
*/
ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }
#ifdef DEBUG
  fprintf(stderr, "initializing\n"); 
#endif
  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  state->unackd_segments = ll_create();
  state->unoutputted_segments = ll_create();
  memcpy(state->config, cfg, sizeof(ctcp_config_t));
  free(cfg); 
  state->byte_sent = 0; 
  state->byte_input = 0;
  state->ack_recd = 0;
  state->eof_stdind = false;
  state->fin_sent = false;
  state->fin_wait = 0;
  state->fin_rxd = false;
  state->ack_sent = 0;
  /* FIXME: Do any other initialization here. */
  
  /* Office hours: do I free cfg or is this handled elsewhere */
  return state;
}

/*
  ctcp_destroy(): Destroys connection state for a connection. 
  You should call either when 5 retransmission attempts have 
  been made on the same segment OR when all of the following hold:
    You have received a FIN from the other side.
    You have read an EOF or error from your input (conn_input returned -1) 
      and have sent a FIN.
    All segments you have sent have been acknowledged.
*/
void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  /* TODO free linked list */
  int i = 0;
  for(i; i < ll_length(state->unackd_segments); i++) {
    ll_node_t* front = ll_front(state->unackd_segments);
    if(front == NULL) {
#ifdef DEBUG
      fprintf(stderr, "unackd front is NULL, i: %i, len: %d\n", i, 
        ll_length(state->unackd_segments)); 
#endif
      break; 
    }
    free(front->object); 
    ll_remove(state->unackd_segments, front); 
  }

  for(i = 0; i < ll_length(state->unoutputted_segments); i++) {
    ll_node_t* front = ll_front(state->unoutputted_segments); 
    if(front == NULL) {
#ifdef DEBUG
      fprintf(stderr, "unoutputted front is NULL, i: %i, len: %d\n", i, 
        ll_length(state->unoutputted_segments)); 
#endif
      break; 
    }
    free(front->object); 
    ll_remove(state->unackd_segments, front); 
  }

  ll_destroy(state->unackd_segments); 
  ll_destroy(state->unoutputted_segments); 

  free(state);
  end_client();
}

/*  
    called after sending a segment
    verifies that the segment was sent successfully
*/
bool send_checks_passed(ctcp_state_t* state, wrapped_seg_t* w_seg, int num_sent) {
  bool retval = false;
  int expected = ntohs(w_seg->seg->len);
  switch(num_sent) {
    case -1:
#ifdef DEBUG
      fprintf(stderr, "Error on Send! Taking down connection\n"); 
#endif    
      ctcp_destroy(state); 
      retval = false;
      break; 
    case expected:
      retval = true; 
      break; 
    default: 
#ifdef DEBUG
      fprintf(stderr, "Incorrect number of bytes sent\n"); 
      fprintf(stderr, "Num_bytes_sent: %i, Expected: %i\n", 
        num_sent, expected);
#endif    
      retval = false; 
      break;
  }

  return retval; 
}

/*  For sending a normal segment of data
    does nothing to the flags for generality
    setting flags must be done before passing the seg
    into this function
*/
void send_seg(ctcp_state_t* state, wrapped_seg_t* w_seg) {
  
  if(state == NULL || w_seg == NULL) {
#ifdef DEBUG
    fprintf(stderr, "ERROR, State or seg passed in was NULL\n"); 
#endif
    return; 
  }

  /* Set everything in the ctcp_segment */
  w_seg->seg->window = htons(state->config->send_window); 
  w_seg->seg->cksum = 0;
  w_seg->seg->ackno = htonl(state->ack_recd + 1); 
  w_seg->seg->cksum = cksum(w_seg->seg, ntohs(w_seg->seg->len)); 

  /* Send the segment */
  int num_sent = conn_send(state->conn, w_seg->seg, ntohs(w_seg->seg->len)); 
  long time_sent = current_time();

  if(send_checks_passed(state, w_seg, num_sent)) {
#ifdef DEBUG
    fprintf(stderr, "Sent this segment successfully:\n");
    print_hdr_ctcp(w_seg->seg);  
#endif
    w_seg->time_last_sent = time_sent; 
    state->byte_sent += num_sent; 
  }
}

/* 
  Performs a number of checks on the data that was stdin'd
  sends the seg calling another helper function if it meets
  the criteria laid out in the lab handout

NOTE TO SELF: for part B, For many connections, I can use this function 
looping thru the states list
*/
void check_and_send(ctcp_state_t* state) {
  if(state == NULL) {
#ifdef DEBUG
    fprintf(stderr, "ERROR, State passed in was NULL\n"); 
#endif
    return;
  }

  if(ll_length(state->unackd_segments) == 0) {
#ifdef DEBUG
    fprintf(stderr, "No Segments to Send\n"); 
#endif
    return;
  }

  ll_node_t* node = ll_front(state->unackd_segments); 
  wrapped_seg_t* w_seg = (wrapped_seg_t* ) (node->object);
  
  if(w_seg->times_transmitted > 5) {
#ifdef DEBUG
    fprintf(stderr, "Exceeded number of retransmit times\n"); 
#endif
    ctcp_destroy(state);
    return; 
  }

  /* We are ready to send the segment if:
    the segment has already been sent (no ACK recv for it)
      at least rt_timeout has passed since last send
    or the receiver has let us know to send the next seg
  */
  if(state->ack_recd < state->byte_sent) {
    if((current_time() - w_seg->time_last_sent) > state->config->rt_timeout) {
      send_seg(state, w_seg); 
    }
  }
  else { /* ack # > seqno sent # --> receiver ready for more!! */
    send_seg(state, w_seg); 
#ifdef DEBUG
    if(w_seg->times_transmitted != 0) {
      fprintf(stderr, "Not first time sent, but should be!!\n"); 
    }
#endif
  }
}

/* 
  ctcp_read(): This is called if there is input to be read. Create a segment 
  from the input and send it to the connection associated with this input.
*/
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  if(state->eof_stdind == true) {
#ifdef DEBUG
    fprintf(stderr, "ERROR, RX DATA AFTER EOF STDIN'd\n"); 
#endif
    return;
  }

  uint8_t buffer[MAX_SEG_DATA_SIZE+1];     /* buffer to read into from stdin */
  int read_from_stdin; /* number of bytes read from stdin */
  /* create TCP segment */
  wrapped_seg_t* wrapped_segm = malloc(sizeof(wrapped_seg_t)); 
  memset(wrapped_segm, 0, sizeof(wrapped_seg_t)); 

  while(1) {
    read_from_stdin = conn_input(state->conn, buffer, MAX_SEG_DATA_SIZE); 
    if(read_from_stdin > 0) {
        buffer[read_from_stdin] = 0;
    }
    else {
#ifdef DEBUG
    fprintf(stderr, "EOF read, or read input error\n"); 
#endif
      break;  
    }
    buffer[read_from_stdin] = 0; /* null terminate whatever you read in */

#ifdef DEBUG
    fprintf(stderr, "\nRead in these bytes: %s\n", buffer); 
#endif

    memset(wrapped_segm, 0, sizeof(wrapped_seg_t) + read_from_stdin); 

    if(wrapped_segm == NULL){
#ifdef DEBUG
      fprintf(stderr, "ERROR setting the wrapped seg pointer\n"); 
      break; 
#endif
    }

    /* Do some segment setup and add to the linked list of packets we have to send */
    wrapped_segm->seg->seqno = htonl(state->byte_input + 1); 
    wrapped_segm->seg->len = htonl((uint16_t ) (sizeof(ctcp_segment_t) + read_from_stdin)); 
    memcpy(wrapped_segm->seg->data, buffer, read_from_stdin);
    ll_add(state->unackd_segments, wrapped_segm); 

    /* update the seqno to include everything we've just read in */
    state->byte_input += read_from_stdin; 
  }

  if (read_from_stdin != -1) {
#ifdef DEBUG
    fprintf(stderr, "EOF never reached... \n");
#endif
    return; 
  }

  /* you recd EOF, create a FIN segment */
  memset(wrapped_segm, 0, sizeof(wrapped_seg_t));

  if(wrapped_segm == NULL) {
#ifdef DEBUG
    fprintf(stderr, "NULL wrapped segm\n");
#endif
    return; 
  }

  /* Setup a FIN packet */
  wrapped_segm->seg->flags |= htonl(FIN);
  wrapped_segm->seg->len = htons(sizeof(ctcp_segment_t));
  wrapped_segm->seg->seqno = htonl(state->byte_input + 1);
  ll_add(state->unackd_segments, wrapped_segm); 

  state->eof_stdind = true; 
  
  /* send() the new data */
  check_and_send(state);

}

/* 
  ctcp_receive(): This is called when a segment is received. 
  You should send ACKs accordingly and output the segment's data to STDOUT.
*/
void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */

  /* buffspace() > len */

  /* ctcp_output() */
}

/* 
  ctcp_output(): This should output cTCP segments and is called by ctcp_receive() 
  if a segment is ready to be outputted. You should flow control the 
  sender by not acknowledging segments if there is no buffer space for outputting.
*/
void ctcp_output(ctcp_state_t *state) {
  /* FIXME */

  /* conn_output() */
}


/* 
  ctcp_timer(): Called periodically at specified rate. You can use this timer to 
  inspect segments and retransmit ones that have not been acknowledged. You can also 
  use this to determine if the other end of the connection has died
  (if they are unresponsive to your retransmissions).
*/
void ctcp_timer() {
  /* FIXME */
}
