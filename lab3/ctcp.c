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

int data_after_fin_cnt = 0;

/* to keep track of the time sent and # of transmits */
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

  linked_list_t* unackd_segments;  /* read in, sent, but not acked */
  linked_list_t* unoutputted_segs; /* received, but not outputted */
  
  ctcp_config_t config;   /* configuration settings for the connection */

  uint32_t seq_num;       
  uint32_t seq_num_next;   
  uint32_t ack_num;       
  uint32_t send_num;      

  bool fin_sent;          /* if you have sent a packet with FIN flag */
  bool fin_recd;          /* if you have received a packet with FIN flag */
  bool recv_ack;          /* if your last sent packet was acked */
  bbr_t* bbr;              /* the BBR variables for the state */
  FILE* bdp_dot_txt;       /* points to bdp.txt */
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */

ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
#ifdef DEBUG
    fprintf(stderr, "Initializing\n"); 
#endif
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  
  /* Set fields. */
  state->config.send_window = cfg->send_window;
  state->config.recv_window = cfg->recv_window;
  state->config.rt_timeout = cfg->rt_timeout;
  state->config.timer = cfg->timer;

  state->unoutputted_segs = ll_create();
  state->unackd_segments = ll_create();
  state->seq_num = 1;
  state->seq_num_next = 1;
  state->ack_num = 1;
  state->send_num = 1;
  state->fin_sent = false;
  state->fin_recd = false;
  state->recv_ack = true;
  
  
  state->bbr = bbr_init(state->config.send_window); 

  state->bdp_dot_txt = fopen("bdp.txt", "w"); 

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
  /* loop through the linked list */
  ll_node_t* node = ll_front(state->unackd_segments);
  ll_node_t* next_node;
  while(node != NULL) {
    wrapped_seg_t* w_seg = (wrapped_seg_t* ) (node->object);
    /* breaking down the russian doll of pointers */
    free(w_seg->seg);
    free(w_seg);
    next_node = node->next;
    free(node);
    node = next_node;
  }
  free(state->unackd_segments);

  node = ll_front(state->unoutputted_segs);
  while(node != NULL) {
    free(node->object);
    next_node = node->next;
    free(node);
    node = next_node; 
  }
  free(state->unoutputted_segs);

  free(state);
  end_client();
}

uint32_t max_size(ctcp_state_t* state) {
  uint32_t max = state->config.send_window + state->send_num - state->seq_num_next; 
  if(max > MAX_SEG_DATA_SIZE) {
    max = MAX_SEG_DATA_SIZE; 
  }
  return max;
}

void ctcp_read(ctcp_state_t *state) {
#ifdef DEBUG
  fprintf(stderr, "input for me to read!\n"); 
#endif
  if(state->fin_sent == true) {
#ifdef DEBUG
  fprintf(stderr, "ERROR, RX DATA AFTER SENT FIN\n"); 
#endif
    data_after_fin_cnt++;
    if(data_after_fin_cnt > 5) {
      //ctcp_destroy(state); 
      state->fin_sent = false; 
      state->fin_recd = false;
      data_after_fin_cnt = 0;
      fprintf(stderr, "Too much data after a fin, ignoring fin\n"); 
    }
    return;
  }
  if(!state->recv_ack && (state->config.send_window == MAX_SEG_DATA_SIZE)) {
#ifdef DEBUG
  fprintf(stderr, "Error, Still waiting on previous ACK\n"); 
#endif
    return;
  }
  /* deal with segments larger than MAX_SEG_DATA_SIZE */
  uint32_t max = max_size(state);
  uint32_t flags = TH_ACK;
  char* buffer = (char* ) malloc(max);
  int num_bytes = conn_input(state->conn, buffer, max);
  
  /* data or fin / ACK */
  if(num_bytes <= 0) {
    num_bytes = 0;
    flags |= TH_FIN;
#ifdef DEBUG
  fprintf(stderr, "EOF read in\n");
#endif
  }

  uint16_t seg_len = num_bytes + sizeof(ctcp_segment_t);
  ctcp_segment_t* seg = (ctcp_segment_t* ) calloc(seg_len, 1); 
  if(num_bytes > 0) {
    memcpy(seg->data, buffer, num_bytes);
  }

  /* set segment fields */
  seg->len = htons(seg_len); 
  seg->seqno = htonl(state->seq_num_next);
  seg->ackno = htonl(state->ack_num); 
  seg->flags = flags;   
  seg->window = htons(state->config.recv_window);
  seg->cksum = cksum(seg, seg_len);
  free(buffer);

  //state->config.send_window = state->bbr->cwnd; 
  while(current_time() < state->bbr->next_packet_send_time) {
    // wait here until we can send..
     // can make another buffer to hold packets in a send queue
     //    to optimize design and not block thread here. 
    
  }

  int bytes_sent = conn_send(state->conn, seg, seg_len); 
  if(bytes_sent < 0) {
#ifdef DEBUG
  fprintf(stderr, "conn_send error!\n");
#endif
  }
  int send_time = current_time(); 
  state->recv_ack = false; 

  /* deal with some variables to update */
  state->seq_num = state->seq_num_next; 
  if(num_bytes > 0) {
    state->seq_num_next += num_bytes; 
  }
  else if(num_bytes == 0) {
    if(flags&TH_FIN) {
      state->seq_num_next += 1;
      state->fin_sent = true;
    }
  }

  wrapped_seg_t* w_seg = (wrapped_seg_t* ) calloc(sizeof(wrapped_seg_t), 1);
  w_seg->seg = seg; 
  w_seg->time_last_sent = send_time; 
  w_seg->times_transmitted = 1; 

  ll_add(state->unackd_segments, w_seg);
  
}

bool is_valid_segment(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* checksum, length, checkers */
/*  omitted-- causing me problems if(len < segment->len) {
#ifdef DEBUG
    fprintf(stderr, "truncated or damaged segment\n"); 
#endif
    free(segment);
    return false; 
  }*/

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

void send_ack_c(ctcp_state_t* state) {
#ifdef DEBUG
    fprintf(stderr, "Sending ACK\n");
#endif
  ctcp_segment_t* seg = calloc(sizeof(ctcp_segment_t), 1); 
  uint16_t seg_len = sizeof(ctcp_segment_t); 

  seg->seqno = htonl(state->seq_num_next);
  seg->ackno = htonl(state->ack_num); 
  seg->flags = TH_ACK;
  seg->window = htons(state->config.recv_window);
  seg->len = htons(seg_len); 
  seg->cksum = cksum(seg, seg_len);
  if(conn_send(state->conn, seg, seg_len) < 0) {
#ifdef DEBUG
    print_hdr_ctcp(seg); 
#endif
    ctcp_destroy(state);
  } 
  free(seg); 
  return; 
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {

#ifdef DEBUG
  fprintf(stderr, "received a segment\n"); 
#endif
  /* relegated to only doing checksum, len checker didn't work NOTE: potential source of error! */
  if(!is_valid_segment(state, segment, len)) {
    return;
  }

  uint32_t seg_ackno = ntohl(segment->ackno);
  uint32_t seg_seqno = ntohl(segment->seqno);
  uint16_t segment_len = ntohs(segment->len);
  
  /* got an old packet? send the ACK again*/
  if(seg_seqno < state->ack_num) {
    if(state->unackd_segments->length == 0) {
      send_ack_c(state);
    }
    free(segment);
    return;
  }
  /* did we get an ACK? */
  if(seg_seqno == state->ack_num) {
    state->recv_ack = true;
    state->config.send_window = ntohs(segment->window);
    if(seg_ackno > state->send_num) {
      state->send_num = seg_ackno;
    }
  }
  
  /* is there data here? */
  if(segment_len - sizeof(ctcp_segment_t) > 0) {
    linked_list_t* ll = state->unoutputted_segs;
    /* easy to add when there is nothing in ll*/
    /* reference: https://www.geeksforgeeks.org/given-a-
    linked-list-which-is-sorted-how-will-you-insert-in-sorted-way/*/
    if(state->unoutputted_segs->length == 0) {
      ll_add(ll, segment); 
    }
    /* otherwise we've gotta put them in order */
    else {
      ctcp_segment_t* head = (ctcp_segment_t* ) (ll_front(ll)->object);
      int head_seqno = ntohl(head->seqno);
      int segment_seqno = ntohl(segment->seqno);
      
      if(segment_seqno > head_seqno) {
        /* can't put it in front, figure out where it goes... */
        ll_node_t* node = ll_front(ll);      
        while(node->next != NULL) {
          int node_next_seqno = ntohl(((ctcp_segment_t*)node->next->object)->seqno);
          if(node_next_seqno > segment_seqno) {
            break;
          }
          node = node->next;
        }
        ll_add_after(ll, node, segment);
      }
      else if(segment_seqno < head_seqno) {
        /* can put it in front, life is easy */
        ll_add_front(ll, segment); 
      }
#if DEBUG /* should never happen */
      else {
        fprintf(stderr, "Error between the keyboard and the chair!\n"); 
      }
#endif
    }
    ctcp_output(state);
  }
  else { 
    /* handle ACK, FIN */
    if(segment->flags & TH_FIN) {
      state->ack_num++; 
      state->fin_recd = true;
      send_ack_c(state);
      conn_output(state->conn, NULL, 0);
    }
    /* update unacked_segments linked list */
    if(state->unackd_segments->length != 0) {
      ll_node_t* node = ll_front(state->unackd_segments);

      while(node != NULL) {
        wrapped_seg_t* w_seg = (wrapped_seg_t* ) (node->object);
        ctcp_segment_t* segment = w_seg->seg;
        int node_seq_num = ntohl(segment->seqno);
        int dat_len = ntohs(segment->len) - sizeof(ctcp_segment_t);

        if(segment->flags & TH_FIN) {
          dat_len = 1; /* phantom byte */
        }
        /* we don't need to check the rest of the ll from here */
        if(node_seq_num + dat_len > seg_ackno) {
          break;
        }
        else { /* woohoo! got some ACK's */
          int seg_rtt = (int) current_time() - (int)w_seg->time_last_sent; 
          if(seg_rtt <= 0) {
            seg_rtt = 200;
          }
          if(seg_rtt > state->config.rt_timeout) { 
            seg_rtt = state->config.rt_timeout;
          }
          int bdp = (int)state->bbr->btl_bw * 8 * seg_rtt;
          fprintf(stderr, "RTT:%i\n", seg_rtt); 
          bbr_on_ack(state->bbr, seg_rtt, dat_len); 
          if(bdp < 20000) {
            fprintf(state->bdp_dot_txt, "%ld,%i\n", current_time(), bdp);
            fflush(state->bdp_dot_txt); 
          }
          state->config.send_window = state->bbr->cwnd; 

          ll_node_t* node_next = node->next;
          ll_remove(state->unackd_segments, node);
          node = node_next;
          free(w_seg);
          free(segment); 
        }
      }
    }
    free(segment);
  }

  if((state->fin_recd) && (state->fin_sent)) {
    if((state->unoutputted_segs->length == 0) && (state->unackd_segments->length == 0)) {
      ctcp_destroy(state);
    }
  }
}

/* 
  algorithm that I implemented closely related to this tutorial:
  https://www.geeksforgeeks.org/delete-a-given-node-in-linked-list
  -under-given-constraints/
*/
void ctcp_output(ctcp_state_t *state) {

  /* check for available space conn_buffspace */
  unsigned int space_avail = conn_bufspace(state->conn);
  ll_node_t* node = ll_front(state->unoutputted_segs);
  bool ack_needed = false; 
  while(node != NULL) {
    ctcp_segment_t* segment = (ctcp_segment_t* ) node->object;

    uint32_t segment_seqnum = ntohl(segment->seqno);
    uint16_t dat_len = ntohs(segment->len)- sizeof(ctcp_segment_t);
    /* sequence number should match the current ack_num */
    if(segment_seqnum != state->ack_num) {
      break;
    }
    /* also break if you're out of buffer space */
    else if(dat_len > space_avail) {
      break;
    }
    /* 
       otherwise output the segment, update state, update available buffer space
       remove the segment from the unoutputted ll 
    */
    conn_output(state->conn, segment->data, dat_len);
    ack_needed = true;
    space_avail -= dat_len;
    state->ack_num += dat_len;
    ll_node_t* node_next = node->next;          
    segment = ll_remove(state->unoutputted_segs, node);

    free(segment);
    node = node_next;
  }

  if(ack_needed) {
    send_ack_c(state); 
  }
}

void ctcp_timer() {

  ctcp_state_t* state = state_list;
  /* Loop through the different connection states */
  while(state != NULL) {
    ll_node_t* node = ll_front(state->unackd_segments);
    /* loop through each unackd segment in that state */
    while(node != NULL) {
      int cur_time = current_time();
      wrapped_seg_t* w_seg = (wrapped_seg_t* ) (node->object);
      /* determine if you've transmitted too many times, if so destroy state */
      if (w_seg->times_transmitted > 5) {
        ctcp_destroy(state);
        break;
      }
      /* otherwise check to see if its time to send it again */
      else if(cur_time - w_seg->time_last_sent >= state->config.rt_timeout) {
        conn_send(state->conn, w_seg->seg, ntohs(w_seg->seg->len)); 
        w_seg->time_last_sent = cur_time;
        w_seg->times_transmitted += 1;
      } 
      node = node->next;
    }
    state = state->next;
  }
}
