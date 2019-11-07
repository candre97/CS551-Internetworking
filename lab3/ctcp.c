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
  linked_list_t *unackd_segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */
  linked_list_t* unoutputted_segments; /* read in, not yet outputted */

  /* FIXME: Add other needed fields. */
  ctcp_config_t* config;     /* Settings for this configuration */

  /* variables related to sending */
  uint32_t seq_num_stdind;
  uint32_t seq_num_sent; 
  uint32_t ack_recd_num;    /* you receive this */ 
  bool eof_stdind;          /* read in, then must send fin */

  /* variables related to receiving */
  uint32_t seq_num_recd; 
  uint32_t seq_num_outputted; 
  bool fin_recd; 

};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. Initial ACK_num = 1 */
  state->conn = conn;
  memcpy(state->config, cfg, sizeof(ctcp_config_t));
  free(cfg); 
  state->unackd_segments = ll_create();
  state->unoutputted_segments = ll_create();
  state->seq_num_recd = 1;
  state->seq_num_outputted = 1;
  state->seq_num_sent = 1;
  state->seq_num_stdind = 1;
  state->ack_recd_num = 1;
  state->eof_stdind = false; 
  state->fin_recd = false; 

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  /* iterate through the linked lists and free them
  reference: https://stackoverflow.com/questions/1886320/free-of-doubly-linked-list
   */
  ll_node_t* node = ll_front(state->unackd_segments); 
  ll_node_t* next_node; 
  while(node != NULL) {
    next_node = node->next;
    free(node);
    node = next_node; 
  }
  free(state->unackd_segments); 

  node = ll_front(state->unoutputted_segments); 
  while(node != NULL) {
    next_node = node->next; 
    free(node); 
    node = next_node;
  }
  free(state->unoutputted_segments);

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_timer() {
  /* FIXME */
}