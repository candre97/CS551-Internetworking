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

#include <stdio.h>
#include <assert.h>


#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

int ip_id_num = 0;

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
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

/* Handles receiving an IP packet and moves responsibility over to other functions to do heavy lifting */
void receive_IP_packet(sr_instance_t* sr, uint8_t* packet, char* interface) {

    /* Determine if the packet is for me */
    sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
    uint32_t dest_addr = ip_hdr->ip_dst; 

    struct sr_if* inf;

    /* verify that this was received legitamately */
    for (inf = sr->if_list; inf != NULL; inf = inf->next) {
        if (inf->ip == dest_addr) {
            handle_IP_packet_for_me(sr, packet, interface); 
        }
    }

    return;
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
 * 1) plan of action for take 2: sudo code
 * 2) ping direct
 * 3) packet forwarding
 * 4) ARP stuff
 * 5) Actually make functions this time you idiot
 * Note to self: no code until pseudo code is finished
 * USE fprintf(stderr, "whatever you wanna say"); for printing
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    printf("*** -> Received packet of length %d \n",len);

    /* fill in code here */
    /* check packet length, drop if too short */
    if(len < sizeof(sr_ethernet_hdr_t)) {
        printf("Dropping Packet, too short\n"); 
        fprintf(stderr, "Packet shorter than an eth header\n"); 
        return; 
    }

    print_hdrs(packet, len); 

    fprintf(stderr, "ETHERTYPE: %i\n", ethertype(packet));
    fprintf(stderr, "ETHERTYPE_IP: %i\n", ethertype_ip);
    struct sr_arpentry* dest_entry = NULL; 

    switch(ethertype(packet)) {
        case ethertype_ip: 
            /* Check if I need to forward the packet or not */
            fprintf(stderr, "Got an IP Packet\n"); 

            sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));

            uint16_t recd_chk_sum = ip_hdr->ip_sum; 
            ip_hdr->ip_sum = 0; 
            uint16_t calcd_chk_sum = cksum((void* ) ip_hdr, sizeof(sr_ip_hdr_t)); 
            
            /* Verify checksum */
            if(recd_chk_sum != calcd_chk_sum) {
                printf("Checksum Error, Dropping packet\n"); 
                printf("From Packet: %i, Calculated: %i\n", recd_chk_sum, calcd_chk_sum); 
                return; 
            }

            receive_IP_packet(sr, packet, interface); 

            /*dest_entry = sr_arpcache_lookup(&sr->cache, ip_hdr->ip_dst); 
            if(dest_entry == NULL) {
                struct sr_ethernet_hdr* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
                memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 
                struct sr_arpreq* outstanding_reqs = sr_arpcache_insert(&sr->cache, eth_hdr->ether_shost, ip_hdr->ip_src); 
                for(outstanding_reqs; outstanding_reqs != NULL; outstanding_reqs = outstanding_reqs->next) {
                    if (outstanding_reqs->ip == ip_hdr->ip_src) {
                      
                        construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); 
                    }
                }
            }*/
            /*
            if(packet_is_directly_for_me(sr, (sr_ip_hdr_t* ) (packet + sizeof(struct sr_ethernet_hdr)))) { 
                
                fprintf(stderr, "I am end destination of this IP packet\n"); 

                if(packet_is_an_echo_req(packet)) {
                    
                    if(dest_entry != NULL) {
                        free(dest_entry);
                        send_echo_reply(sr, packet, interface);
                    }
                    /*else { 
                        construct_and_send_ARP(sr, (uint16_t) 1, packet, interface);
                    }
                }
                else/* if(packet_is_TCP_UDP())*/ {
                    /* Only handling echo reply right now */
                    /*if(dest_entry != NULL) {
                        send_ICMP_type3(sr, packet, interface, (uint8_t)(1)); 
                    }*/
                        /*else { /* send an ARP request 
                            construct_and_send_ARP(sr, (uint16_t) 1, packet, interface);
                        }*/
             /*   }
            }   
            */
            /*else { /* Packet is not for me */
                /*if(LPM_Match() == NULL) {
                    send_ICMP_net_unreachable(); 
                }
                else { /* check ARP cache */
                    /*if(ip_exists_in_ARP_cache()) {
                        forward_to_next_hop(); 
                    }
                    else {
                        /*send_ARP_request();*/
                        /*construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); */
                        /*repeat 5x (handled in sr_arpcache.c)*/
                   /* }
                }
            }*/
            break; 
        case ethertype_arp: 
            fprintf(stderr, "Got an ARP Packet\n"); 
            sr_arp_hdr_t* arp_hdr = (sr_arp_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
            uint8_t arp_op_code = get_op_code(arp_hdr);
            
            /* If you get an ARP packet from someone you haven't talked to yet, add them to your cache*/
            dest_entry = sr_arpcache_lookup(&sr->cache, arp_hdr->ar_sip);
            if(dest_entry == NULL) {
                struct sr_arpreq* outstanding_reqs = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip); 
                for(outstanding_reqs; outstanding_reqs != NULL; outstanding_reqs = outstanding_reqs->next) {
                    if (outstanding_reqs->ip == arp_hdr->ar_sip) {
                        /* send outstanding requests */
                        construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); 
                    }
                }
            }

            if(arp_op_code == arp_op_request) {
                /*fprintf(stderr, "Received a request\n"); */
                dest_entry = sr_arpcache_lookup(&sr->cache, arp_hdr->ar_sip); /* Can we send to the guy who sent to us??*/
                
                if(dest_entry != NULL) {
                    construct_and_send_ARP(sr, (uint16_t) 2, packet, interface);
                } 
                else { /* We cannot even reply without this person's HW address */
                    construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); 
                }
            }
            else if(arp_op_code == arp_op_reply) { /* the packet is a reply to me */
                /* cache it! */
                struct sr_arpreq* outstanding_reqs = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip); 
                /* send_outstanding_packets(); Just gonna do this here, no time for functionality */ 
                for(outstanding_reqs; outstanding_reqs != NULL; outstanding_reqs = outstanding_reqs->next) {
                    if (outstanding_reqs->ip == arp_hdr->ar_sip) {
                        /* send outstanding requests */
                        construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); 
                    }
                }
            }
            else { 
                fprintf(stderr, "Just got an ARP packet that was not a request or reply\n");
                return; 
            }
            break; 
        default:
            printf("No clue what happened here\n");
            fprintf(stderr, "Received something not IP or ARP\n"); 
            break;
    }


}/* end sr_ForwardPacket */