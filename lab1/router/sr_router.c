/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sr_arpcache.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_router.h"
#include "sr_rt.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr) {
  /* REQUIRES */
  assert(sr);

  /* Initialize cache and cache cleanup thread */
  sr_arpcache_init(&(sr->cache));

  pthread_attr_init(&(sr->attr));
  pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_t thread;

  pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

  /* Add initialization code here! */

} /* -- sr_init -- */

void handle_arp_request(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                        sr_arp_hdr_t* arp_hdr) {
  /* iterate through all the interfaces  */
  struct sr_if* iface;
  for (iface = sr->if_list; iface != NULL; iface = iface->next) {
    if (iface->ip == arp_hdr->ar_tip) {
      /* Construct a reply for this request */
      size_t arp_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
      uint8_t* buf = malloc(arp_len);

      /* Construct Ethernet header */
      sr_ethernet_hdr_t* reply_eth_hdr = (sr_ethernet_hdr_t*)buf;
      memcpy(reply_eth_hdr->ether_dhost, eth_hdr->ether_shost, ETHER_ADDR_LEN);
      memcpy(reply_eth_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);

      reply_eth_hdr->ether_type = htons(ethertype_arp);

      /* Construct ARP header */
      sr_arp_hdr_t* reply_arp = (sr_arp_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t));

      reply_arp->ar_hrd = htons(arp_hrd_ethernet);
      reply_arp->ar_pro = htons(0x0800);
      reply_arp->ar_hln = 6;
      reply_arp->ar_pln = 4;
      reply_arp->ar_op = htons(arp_op_reply);

      memcpy(reply_arp->ar_sha, iface->addr, ETHER_ADDR_LEN);
      reply_arp->ar_sip = iface->ip;
      memcpy(reply_arp->ar_tha, arp_hdr->ar_sha, ETHER_ADDR_LEN);
      reply_arp->ar_tip = arp_hdr->ar_sip;

      /* Send through the chosen iface */
      sr_send_packet(sr, buf, arp_len, iface->name);

      /* Free the buffer of arp_request */
      free(buf);
      break;
    }
  }
}

void handle_arp_reply(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                      sr_arp_hdr_t* arp_hdr, char* interface) {
  
  /* get the ip and mac address from the arp reply message */
  uint32_t ip = arp_hdr->ar_sip;
  unsigned char* mac = arp_hdr->ar_sha;

  /* interface is the hardware interface that receive the arp reply */
  /* insert the ip -> mac mapping to the cache */
  struct sr_arpreq* req = sr_arpcache_insert(&(sr->cache), mac, ip);
  if (req == NULL) {
    return;
  }
  assert(req->ip == ip);

  /* Send all packets tied to the req */
  struct sr_packet* curr_pkt;
  for (curr_pkt = req->packets; curr_pkt != NULL; curr_pkt = curr_pkt->next) {
    sr_ethernet_hdr_t* pkt_eth_hdr = (sr_ethernet_hdr_t*)(curr_pkt->buf);
    memcpy(pkt_eth_hdr->ether_dhost, mac, 6);
    sr_send_packet(sr, curr_pkt->buf, curr_pkt->len, curr_pkt->iface);
  }

  sr_arpreq_destroy(&(sr->cache), req);
}

int bit_count(uint32_t prefix) {
  int count = 0;

  /* I saw one previously, if I see zero again, prefix is formattedly wrong */
  unsigned prev_one = 0;

  while (prefix > 0) {
    if (prefix & 1) {
      count++;
      prev_one = 1;
    } else {
      if (prev_one) {
        return -1;
      }
    }
    prefix = prefix >> 1;
  }
  return count;
}

uint32_t routing_table_lookup(struct sr_instance* sr, uint32_t dest_ip,
                              char* iface_name, uint8_t* found) {
  struct sr_rt* rt_itr;

  uint32_t result = 0;
  int longest_matched_length = -1;

  for (rt_itr = sr->routing_table; rt_itr != NULL;
       rt_itr = rt_itr->next) {
    uint32_t rt_dest = ntohl((uint32_t)rt_itr->dest.s_addr);
    uint32_t rt_mask = ntohl((uint32_t)rt_itr->mask.s_addr);
    uint32_t prefix = rt_dest & rt_mask;
    int prefix_length = bit_count(rt_mask);

    if (prefix_length < 0) {
      printf("Negative prefix len = %d\n", prefix_length);
    }

    uint32_t masked_dest_ip = dest_ip & rt_mask;

    uint32_t matched = masked_dest_ip ^ prefix;

    if ((matched == 0) && prefix_length > longest_matched_length) {
      *found = 1;
      strncpy(iface_name, rt_itr->interface, sr_IFACE_NAMELEN);
      longest_matched_length = prefix_length;
      result = rt_itr->gw.s_addr;
    }
  }

  return result;
}

void generate_arp_request(struct sr_instance* sr, struct sr_arpreq* req,
                          struct sr_if* iface) {
  uint32_t ip = req->ip;

  size_t arp_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
  uint8_t* buf = malloc(arp_len);

  /* Construct Ethernet header */
  sr_ethernet_hdr_t* request_eth_hdr = (sr_ethernet_hdr_t*)buf;
  memcpy(request_eth_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);
  memset(request_eth_hdr->ether_dhost, 0xff, ETHER_ADDR_LEN);

  request_eth_hdr->ether_type = htons(ethertype_arp);

  /* Construct ARP header */
  sr_arp_hdr_t* request_arp = (sr_arp_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t));

  request_arp->ar_hrd = htons(arp_hrd_ethernet);
  request_arp->ar_pro = htons(0x0800);
  request_arp->ar_hln = 6;
  request_arp->ar_pln = 4;
  request_arp->ar_op = htons(arp_op_request);

  memcpy(request_arp->ar_sha, iface->addr, ETHER_ADDR_LEN);
  request_arp->ar_sip = iface->ip;

  request_arp->ar_tip = ip;

  req->sent = time(NULL);
  
  sr_send_packet(sr, buf, arp_len, iface->name);
  req->times_sent += 1;

  /* Free the buffer of arp_request */
  free(buf);
}

void send_or_queue_packet(struct sr_instance* sr, uint8_t* packet,
                          unsigned int packet_len, uint32_t dest_ip) {
  char next_hop_iface[sr_IFACE_NAMELEN];
  uint8_t found = 0;
  uint32_t next_hop_ip = routing_table_lookup(sr, dest_ip, next_hop_iface, &found);
  /* if there is no route, sent destination net unreachable to sender*/
  if (!found) {
    handle_icmp_t3(sr, (sr_ethernet_hdr_t*)(packet),
                   (sr_ip_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t)),
                   packet_len - sizeof(sr_ethernet_hdr_t), 3, 0);
    return;
  }

  struct sr_if* iface = sr_get_interface(sr, next_hop_iface);
  sr_ethernet_hdr_t* pkt_eth_hdr = (sr_ethernet_hdr_t*)(packet);
  memcpy(pkt_eth_hdr->ether_shost, iface->addr, 6);

  struct sr_arpentry* entry = sr_arpcache_lookup(&(sr->cache), next_hop_ip);

  if (entry) {
    memcpy(pkt_eth_hdr->ether_dhost, entry->mac, 6);
    sr_send_packet(sr, packet, packet_len, iface->name);
  }
  else {
    /* Queue the request if not found */
    struct sr_arpreq* req = sr_arpcache_queuereq(&(sr->cache), next_hop_ip,
                                                 packet, packet_len, iface->name);
    /* Generate and send arp request */
    generate_arp_request(sr, req, iface);
  }
}

void handle_icmp_echo(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                      sr_ip_hdr_t* ip_hdr) {
  size_t icmp_echo_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t);
  uint8_t* buf = malloc(icmp_echo_len);

  /* Construct Ethernet header */
  sr_ethernet_hdr_t* reply_eth_hdr = (sr_ethernet_hdr_t*)buf;
  memcpy(reply_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN);
  reply_eth_hdr->ether_type = htons(ethertype_ip);

  /* Construct IP header */
  sr_ip_hdr_t* reply_ip_hdr = (sr_ip_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t));
  reply_ip_hdr->ip_v = 4;
  reply_ip_hdr->ip_hl = 5;
  reply_ip_hdr->ip_tos = 0;
  reply_ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t));
  reply_ip_hdr->ip_id = htons(0);

  reply_ip_hdr->ip_off = htons(0);
  reply_ip_hdr->ip_ttl = 0xff;
  reply_ip_hdr->ip_p = ip_protocol_icmp;

  /* Flip the dst/src IP address */
  reply_ip_hdr->ip_src = ip_hdr->ip_dst;
  reply_ip_hdr->ip_dst = ip_hdr->ip_src;

  reply_ip_hdr->ip_sum = 0;
  reply_ip_hdr->ip_sum = cksum(reply_ip_hdr, sizeof(sr_ip_hdr_t));

  /* Construct ICMP header */
  sr_icmp_hdr_t* reply_icmp_hdr = (sr_icmp_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
  reply_icmp_hdr->icmp_type = 0;
  reply_icmp_hdr->icmp_code = 0;

  reply_icmp_hdr->icmp_sum = 0;
  reply_icmp_hdr->icmp_sum = cksum(reply_icmp_hdr, sizeof(sr_icmp_hdr_t));

  send_or_queue_packet(sr, buf, icmp_echo_len, ntohl(reply_ip_hdr->ip_dst));

  free(buf);
}

/* Even though it is called type3 header, it also support type 1 */
/* This function will send icmp message to the ip_hdr->ip_src*/
void handle_icmp_t3(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                    sr_ip_hdr_t* ip_hdr, unsigned int ip_packet_len,
                    uint8_t type, uint8_t code) {
  size_t icmp_t3_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) +
                       sizeof(sr_icmp_t3_hdr_t);
  uint8_t* buf = malloc(icmp_t3_len);

  /* Construct Ethernet header */
  sr_ethernet_hdr_t* reply_eth_hdr = (sr_ethernet_hdr_t*)buf;
  /* Leave the destination mac address blank...use arp to find out */
  memcpy(reply_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN);
  reply_eth_hdr->ether_type = htons(ethertype_ip);

  /* Construct IP header */
  sr_ip_hdr_t* reply_ip_hdr = (sr_ip_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t));
  reply_ip_hdr->ip_v = 4;
  reply_ip_hdr->ip_hl = 5;
  reply_ip_hdr->ip_tos = 0;
  reply_ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
  reply_ip_hdr->ip_id = htons(0);

  reply_ip_hdr->ip_off = htons(0);
  reply_ip_hdr->ip_ttl = 0xff;
  reply_ip_hdr->ip_p = ip_protocol_icmp;

  /* Flip the IP address */
  reply_ip_hdr->ip_src = ip_hdr->ip_dst;
  reply_ip_hdr->ip_dst = ip_hdr->ip_src;

  reply_ip_hdr->ip_sum = 0;
  reply_ip_hdr->ip_sum = cksum(reply_ip_hdr, sizeof(sr_ip_hdr_t));

  /* Construct ICMP header */
  sr_icmp_t3_hdr_t* reply_icmp_hdr =
      (sr_icmp_t3_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t) +
                          sizeof(sr_ip_hdr_t));
  reply_icmp_hdr->icmp_type = type;
  reply_icmp_hdr->icmp_code = code;

  /* Copy the original ip header and datagram */
  memset(reply_icmp_hdr->data, 0, ICMP_DATA_SIZE);
  if (ip_packet_len < ICMP_DATA_SIZE) {
    memcpy(reply_icmp_hdr->data, ip_hdr, ip_packet_len);
  } else {
    /* Assume there is no ip options in the ip header */
    memcpy(reply_icmp_hdr->data, ip_hdr, ICMP_DATA_SIZE);
  }

  reply_icmp_hdr->icmp_sum = 0;
  reply_icmp_hdr->icmp_sum = cksum(reply_icmp_hdr, sizeof(sr_icmp_t3_hdr_t));

  send_or_queue_packet(sr, buf, icmp_t3_len, ntohl(reply_ip_hdr->ip_dst));

  free(buf);
}

void handle_ip_packet_to_me(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                            sr_ip_hdr_t* ip_hdr, unsigned int ip_packet_len,
                            char* interface) {
  uint8_t ip_proto = ip_protocol((uint8_t*)ip_hdr);
  if (ip_proto == ip_protocol_icmp) {
    sr_icmp_hdr_t* icmp_hdr = (sr_icmp_hdr_t*)(ip_hdr + sizeof(sr_ip_hdr_t));
    uint16_t check_sum = cksum(icmp_hdr, sizeof(sr_icmp_hdr_t)) ^ 0xffff;
    if (check_sum != 0) {
      fprintf(stderr, "ICMP packet check sum error\n");
      print_hdr_icmp((uint8_t*)icmp_hdr);
      return;
    }

    if (icmp_hdr->icmp_type == 8 && icmp_hdr->icmp_code == 0) {
      handle_icmp_echo(sr, eth_hdr, ip_hdr);
    }
  } else if (ip_proto == 0x06 || ip_proto == 0x11) {
    handle_icmp_t3(sr, eth_hdr, ip_hdr, ip_packet_len, 3, 3);
  }
  else {
    fprintf(stderr, "Received an IP packet that was not ICMP\n");
  }
}

void handle_icmp_time_exceed(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                             sr_ip_hdr_t* ip_hdr, unsigned int ip_packet_len,
                             char* interface) {
  size_t icmp_t3_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) +
                       sizeof(sr_icmp_t3_hdr_t);
  uint8_t* buf = malloc(icmp_t3_len);

  /* Construct Ethernet header */
  sr_ethernet_hdr_t* reply_eth_hdr = (sr_ethernet_hdr_t*)buf;
  /* Leave the destination mac address blank...use arp to find out */
  memcpy(reply_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN);
  reply_eth_hdr->ether_type = htons(ethertype_ip);

  /* Construct IP header */
  sr_ip_hdr_t* reply_ip_hdr = (sr_ip_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t));
  reply_ip_hdr->ip_v = 4;
  reply_ip_hdr->ip_hl = 5;
  reply_ip_hdr->ip_tos = 0;
  reply_ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t));
  reply_ip_hdr->ip_id = htons(0);
  reply_ip_hdr->ip_off = htons(0);
  reply_ip_hdr->ip_ttl = 0xff;
  reply_ip_hdr->ip_p = ip_protocol_icmp;

  struct sr_if* iface = sr_get_interface(sr, interface);

  /* When sending time exceed, use this router's ip as src */
  reply_ip_hdr->ip_src = iface->ip;
  reply_ip_hdr->ip_dst = ip_hdr->ip_src;
  reply_ip_hdr->ip_sum = 0;
  reply_ip_hdr->ip_sum = cksum(reply_ip_hdr, sizeof(sr_ip_hdr_t));

  /* Construct ICMP header */
  sr_icmp_t3_hdr_t* reply_icmp_hdr =
      (sr_icmp_t3_hdr_t*)(buf + sizeof(sr_ethernet_hdr_t) +
                          sizeof(sr_ip_hdr_t));
  reply_icmp_hdr->icmp_type = 11;
  reply_icmp_hdr->icmp_code = 0;

  /* Copy the original ip header and datagram */
  memset(reply_icmp_hdr->data, 0, ICMP_DATA_SIZE);
  if (ip_packet_len < ICMP_DATA_SIZE) {
    memcpy(reply_icmp_hdr->data, ip_hdr, ip_packet_len);
  } else {
    /* Assume there is no ip options in the ip header */
    memcpy(reply_icmp_hdr->data, ip_hdr, ICMP_DATA_SIZE);
  }

  reply_icmp_hdr->icmp_sum = 0;
  reply_icmp_hdr->icmp_sum = cksum(reply_icmp_hdr, sizeof(sr_icmp_t3_hdr_t));

  send_or_queue_packet(sr, buf, icmp_t3_len, ntohl(reply_ip_hdr->ip_dst));

  free(buf);
}

void handle_ip_packet_forward(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                              sr_ip_hdr_t* ip_hdr, unsigned int len,
                              char* interface) {
  if (ip_hdr->ip_ttl == 1) {
    printf("Time to live is over!!\n");
    handle_icmp_time_exceed(sr, eth_hdr, ip_hdr, len, interface);
    return; 
  }

  /* decrement ttl and re-checksum the ip packet */
  ip_hdr->ip_ttl -= 1;
  ip_hdr->ip_sum = 0;
  ip_hdr->ip_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));

  send_or_queue_packet(sr, (uint8_t*) eth_hdr, len, ntohl(ip_hdr->ip_dst));
}

void handle_ip_packet(struct sr_instance* sr, sr_ethernet_hdr_t* eth_hdr,
                      uint8_t* ip_packet_buf, unsigned int len,
                      char* interface) {
  /* Read the ip header out */
  sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t*)ip_packet_buf;

  uint16_t check_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t)) ^ 0xffff;

  if (check_sum != 0) {
    fprintf(stderr, "IP packet check sum error %d\n", check_sum);
    print_hdr_ip((uint8_t*)ip_hdr);
    return;
  }

  /* determine whether the packet is for me */
  struct sr_if* if_itr;
  for (if_itr = sr->if_list; if_itr != NULL;
       if_itr = if_itr->next) {

    if (if_itr->ip == ip_hdr->ip_dst) {
      printf("ip packet for me\n");
      handle_ip_packet_to_me(sr, eth_hdr, ip_hdr, len - sizeof(sr_ethernet_hdr_t),
                             interface);
      return;
    }
  }

  printf("ip packet for others\n");
  /* Reach here means the ip packet is not for me. Need to forward */
  handle_ip_packet_forward(sr, eth_hdr, ip_hdr, len, interface);
}

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr, uint8_t* packet /* lent */,
                     unsigned int len, char* interface /* lent */) {
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  printf("*** -> Received packet of length %d \n", len);

  unsigned int ether_len = sizeof(sr_ethernet_hdr_t);
  if (len < ether_len) {
    fprintf(stderr, "packet too short\n");
    return;
  }
  uint8_t* buf = packet;
  unsigned int offset = 0;

  sr_ethernet_hdr_t* eth_hdr = (sr_ethernet_hdr_t*)buf;
  uint16_t ether_type = ethertype(buf);

  buf += ether_len;
  offset += ether_len;
  switch (ether_type) {
    case ethertype_arp:
      if (len - offset < sizeof(sr_arp_hdr_t)) {
        fprintf(stderr, "Failed to parse ARP header, insufficient length\n");
        return;
      }

      sr_arp_hdr_t* arp_hdr = (sr_arp_hdr_t*)(buf);

      if (ntohs(arp_hdr->ar_op) == arp_op_request) {
        printf("Received an ARP request\n");
        handle_arp_request(sr, eth_hdr, arp_hdr);
      } else if (ntohs(arp_hdr->ar_op) == arp_op_reply) {
        handle_arp_reply(sr, eth_hdr, arp_hdr, interface);
      } else {
        printf("ARP op-code invalid: arp opcode %d", ntohs(arp_hdr->ar_op));
        print_hdrs(packet, len);
      }
      break;
    case ethertype_ip:
      if (len - offset < sizeof(sr_ip_hdr_t)) {
        fprintf(stderr, "Failed to parse IP header, insufficient length\n");
        return;
      }
      printf("Received IP packet\n");
      /* Pass the original length */
      handle_ip_packet(sr, eth_hdr, buf, len, interface);
      break;
    default:
      printf("not implemented: ethertype %d", ether_type);
  }
} 
