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

/* got the fprintf code from stdio-common, 
  edited to make only print while debugging */
int printt (FILE *stream, const char *format, ...)
{
#ifdef DEBUG
  va_list arg;
  int done;
  va_start (arg, format);
  done = __vfprintf_internal (stream, format, arg, 0);
  va_end (arg);
  return done;
#endif

  return -1; 
}

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

  linked_list_t* unsent_segments; /* segment has been stdin'd, but not sent */
  linked_list_t* unackd_segments; /* segment has been sent but not ack'd */

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
  printt(stderr, "initializing\n"); 
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
  state->unsent_segments = ll_create();
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


  free(state);
  end_client();
}


/* 
  ctcp_read(): This is called if there is input to be read. Create a segment 
  from the input and send it to the connection associated with this input.
*/
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  if(state->eof_stdind == true) {
    printt(stderr, "ERROR, RX DATA AFTER EOF STDIN'd\n"); 
  }

  uint8_t* buffer;     /* buffer to read into from stdin */
  int read_from_stdin; /* number of bytes read from stdin */
  /* create TCP segment */
  ctcp_segment_t* seg; 

  /* send() */

  /*  */

  /*  */
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
