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
  ctcp_config_t config;     /* Settings for this configuration */

  /* variables related to sending */
  uint32_t seq_num;          /* current seq num*/
  uint32_t seq_num_next;    /* == seq_num + data_bytes */ 
  uint32_t send_num;    /* helps with other variables */ 
  bool fin_sent;          /* read in, then sent fin */
  bool ack_recd;            /* set false after sending, true when that seg is ack'd */
  bool retrans_to; 

  /* variables related to receiving */
  uint32_t ack_num;   /* for keeping track when sending ACKs. == received seq_num + data_bytes*/ 
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
#ifdef DEBUG
    fprintf(stderr, "Initializing\n"); 
#endif

  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);

  /* Set fields. Initial ACK_num = 1 */
  
  state->config.recv_window = cfg->recv_window;
  state->config.send_window = cfg->send_window;
  state->config.timer = cfg->timer;
  state->config.rt_timeout = cfg->rt_timeout;

  state->unackd_segments = ll_create();
  state->unoutputted_segments = ll_create();
  state->seq_num = 1;
  state->seq_num_next = 1;
  state->ack_num = 1;
  state->send_num = 1;
  state->fin_sent = false; 
  state->fin_recd = false; 
  state->ack_recd = false;
  state->retrans_to = false; 

  state->next = state_list;
  state->prev = &state_list;
  
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  state->conn = conn;

  free(cfg); 

#ifdef DEBUG
    fprintf(stderr, "Initialization complete\n"); 
#endif
  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
#ifdef DEBUG
    fprintf(stderr, "destroying\n"); 
#endif

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

void create_data_seg(ctcp_state_t* state, ctcp_segment_t* seg, int data_bytes, char* data, uint32_t flag) {
  seg->len = htons((uint16_t) data_bytes + sizeof(ctcp_segment_t));
  seg->window = htons(state->config.recv_window); 
  seg->seqno = htonl(state->seq_num_next); 
  seg->ackno = htonl(state->ack_num);
  seg->flags = flag;
/*  seg->cksum = 0;*/
  seg->cksum = cksum(seg, ntohs(seg->len)); 

  return; 
}

void create_fin_seg(ctcp_state_t* state, ctcp_segment_t* seg, uint32_t flags) {
  seg->len = htons((uint16_t ) sizeof(ctcp_segment_t)); 
  seg->window = htons(state->config.recv_window); 
  seg->seqno = htonl(state->seq_num_next); 
  seg->ackno = htonl(state->ack_num); 
  seg->flags = flags;
/*  seg->cksum = 0;*/
  seg->cksum = cksum(seg, ntohs(seg->len)); 
}

uint32_t max_size(ctcp_state_t* state) {
  uint32_t max = state->config.send_window + state->send_num - state->seq_num_next; 
  if(max > MAX_SEG_DATA_SIZE) {
    max = MAX_SEG_DATA_SIZE; 
  }
  return max;
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
#ifdef DEBUG
    fprintf(stderr, "input for me to read!\n"); 
#endif

  if(state->fin_sent == true) {
#ifdef DEBUG
    fprintf(stderr, "ERROR, RX DATA AFTER SENT FIN\n"); 
#endif
    return;
  }

  if(!state->ack_recd && (state->config.send_window == MAX_SEG_DATA_SIZE)) {
#ifdef DEBUG
    fprintf(stderr, "Error, Still waiting on previous ACK\n"); 
#endif
    return;
  }

  uint32_t max = max_size(state);

  uint32_t flags = TH_ACK; /* maybe just have set to ACK */

  char* buffer = (char* ) malloc(max); 
  int num_bytes = conn_input(state->conn, buffer, max); 

  int dat_len = num_bytes; 
  if(dat_len < 0) {
    dat_len = 0; 
  }

  ctcp_segment_t* seg = (ctcp_segment_t* ) calloc(sizeof(ctcp_segment_t) + dat_len, 1); 

  if(num_bytes == -1) {
    flags |= TH_FIN; 
    state->fin_sent = true;
#ifdef DEBUG
    fprintf(stderr, "EOF read in\n"); 
#endif
    create_fin_seg(state, seg, flags); /* known dat_len = 0 */
  }
  else if(num_bytes == 0) {
#ifdef DEBUG
    fprintf(stderr, "No more data available\n"); 
#endif
    flags |= TH_FIN; 
    create_fin_seg(state, seg, flags);
    return; 
  }
  else {
    /* create segment*/
    create_data_seg(state, seg, num_bytes, buffer, flags); 
  }
  free(buffer); 

  /* send seg */
  uint16_t seg_len = ntohs(seg->len);
  /* deal with some variables to update */
  
  wrapped_seg_t* w_seg = (wrapped_seg_t* ) malloc(sizeof(wrapped_seg_t)); 
  w_seg->seg = seg; 

  conn_send(state->conn, seg, seg_len); 
  w_seg->time_last_sent = current_time();
  w_seg->times_transmitted = 1;
  state->ack_recd = false;
  

  /* update the number of bytes you've read in */
  switch(num_bytes) {
    case -1: 
      state->seq_num_next++;
      break;
    default:
      state->seq_num = state->seq_num_next; 
      state->seq_num_next += num_bytes; 
      break;
  }
  ll_add(state->unackd_segments, w_seg); 
}

bool is_valid_segment(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* checksum, length, checkers */
  if(len < segment->len) {
#ifdef DEBUG
    fprintf(stderr, "truncated or damaged segment\n"); 
#endif
    free(segment);
    return false; 
  }

  uint16_t recv_cksum = segment->cksum;
  segment->cksum = 0;
  uint16_t calcd_sum = cksum(segment, ntohs(segment->len)); 
  if(recv_cksum != calcd_sum) {
#ifdef DEBUG
    fprintf(stderr, "checksum error\n"); 
#endif
    return false; 
  }
  segment->cksum = recv_cksum; 

  return true;

}

void send_ack_c(ctcp_state_t *state) {
  ctcp_segment_t* segment = malloc(sizeof(ctcp_segment_t));
  segment->seqno = htonl(state->seq_num); 
  segment->ackno = htonl(state->ack_num); 
  segment->len = htons(sizeof(ctcp_segment_t)); 
  segment->flags |= TH_ACK; 
  segment->window = htons(state->config.recv_window); 
  segment->cksum = 0;
  segment->cksum = cksum(segment, sizeof(ctcp_segment_t)); 
  conn_send(state->conn, segment, sizeof(ctcp_segment_t)); 
  free(segment);
}

void handle_ack(ctcp_state_t* state, uint32_t ack_num) {
  
  int dat_len = 0;
  ll_node_t* node = ll_front(state->unackd_segments);
  while(node != NULL) {
    wrapped_seg_t* w_seg = node->object; 
    if(w_seg->seg->flags & TH_FIN) {
      dat_len = 1;
    }
    else {
      dat_len = ntohs(w_seg->seg->len) - sizeof(ctcp_segment_t); 
    }
    if(ntohl(w_seg->seg->seqno) + dat_len == ack_num) {
      ll_remove(state->unackd_segments, node); 
      free(w_seg); 
    }
    node = node->next;
  }
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
#ifdef DEBUG
    fprintf(stderr, "received a segment\n"); 
#endif

  /* checking checksum and length */
  if(!is_valid_segment(state, segment, len)) {
    return; 
  }
  
  if(ntohl(segment->seqno) < state->ack_num) {
#ifdef DEBUG
    fprintf(stderr, "out of order segment\n"); 
#endif
    send_ack_c(state); 
    free(segment); 
    return; 
  } 

  if(ntohl(segment->ackno) == state->ack_num + 1) {
    state->ack_recd = true;
    state->config.send_window = ntohs(segment->window); 
  }

  uint16_t dat_len = ntohs(segment->len) - sizeof(ctcp_segment_t); 

  switch (dat_len) {
    case 0:
      if(segment->flags & TH_FIN) {
        state->ack_num++; 
        state->fin_recd = true; 
        send_ack_c(state); 
        conn_output(state->conn, NULL, 0); 
      }
      else if(segment->flags & TH_ACK) {
        handle_ack(state, htonl(segment->ackno)); 
        free(segment); 
      }
      break; 
    default:
      if(dat_len > 0) {
        ll_add(state->unoutputted_segments, segment); 
        ctcp_output(state); 
      }
      break; 
  }
  
  /* check for data, send ACK, if outside receive window, break

  keep filling the linked list till there are no more packets or they go outside receive window
      
   */

  /* check for ACK, set ACK recd true */

  /* output segment 
  
  */

}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  /* check for available space conn_buffspace */
  unsigned int space_avail = conn_bufspace(state->conn);
  ll_node_t* node = ll_front(state->unoutputted_segments); 

  while(node != NULL) {
    ctcp_segment_t* seg = node->object;
    uint16_t dat_len = seg->len - sizeof(ctcp_segment_t); 
    
    if(dat_len > space_avail) {
      break; 
    }
    uint32_t seg_seqno = ntohl(seg->seqno); 
    if(seg_seqno == state->ack_num) {
      conn_output(state->conn, seg->data, dat_len); 
      space_avail -= dat_len;
      state->ack_num += dat_len; 
      free(ll_remove(state->unoutputted_segments, node));
      node = node->next; 
      send_ack_c(state); 
    }
  }
  /* go through the unoutputted_segments ll, 
  output them one by one using conn_output, remove from unoutputted segments */

  /* stop when there are no more segments to output or you're out of space */
}

void ctcp_timer() {
  /* FIXME */
  ctcp_state_t* curr_state = state_list;
  while(curr_state != NULL) {
    ll_node_t* node =  ll_front(curr_state->unackd_segments);
    while(node != NULL) {
      wrapped_seg_t* w_seg = node->object;
      if(w_seg->times_transmitted > 5) {
        ctcp_destroy(curr_state); 
        break; 
      }
      else if(current_time() - w_seg->time_last_sent > curr_state->config.rt_timeout) {
        conn_send(curr_state->conn, w_seg->seg, ntohs(w_seg->seg->len)); 
        w_seg->times_transmitted++;
        w_seg->time_last_sent = current_time(); 
      }
      node = node->next; 
    }
  }
  /* loop through all the unackd_segs */

  /* if any have been transmitted more than 5x, destroy the connection */

  /* if they haven't been sent 5x and timeout has occurred, send them again */
}


