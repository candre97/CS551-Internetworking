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

/*  
    Specifically for IP packets:
    -- checks if I am the final destion of a packet 
    -- if this is false, then I have to forward the packet 
*/ 
bool packet_is_directly_for_me(struct sr_instance* sr, struct sr_ip_hdr* ip_hdr) {
    
    bool retval = false; 
    uint32_t dest_addr = ip_hdr->ip_dst; 

    struct sr_if* inf;
    for (inf = sr->if_list; inf != NULL; inf = inf->next) {
        if (inf->ip == dest_addr) {
            retval = true;
        }
    }
    return retval; 
}

/*
    returns true if the packet passed in is an echo request
    -- checks if it is ICMP
    -- checks if that ICMP packet is an echo request
*/
bool packet_is_an_echo_req(uint8_t in_pac) {
    bool retval = false; 

    struct sr_ip_hdr* ip_hdr = malloc(sizeof(sr_ip_hdr_t)); 
    struct sr_icmp_hdr* icmp_hdr = malloc(sizeof(sr_icmp_hdr_t));
    memcpy(ip_hdr, in_pac + sizeof(sr_ethernet_hdr_t), sizeof(sr_ip_hdr_t));

    if(ip_hdr->ip_tos != ip_protocol_icmp) {
        fprintf(stderr, "Received an IP packet that is not an ICMP message\n");
        return retval; 
    }
    memcpy(icmp_hdr, in_pac + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), sizeof(sr_icmp_hdr_t)); 

    




    return retval; 
}

void send_echo_reply() {

}
                
bool packet_is_TCP_UDP() {
    bool retval = false; 
    return retval; 
}

void send_ICMP_port_unreachable() {

}
            
struct sr_rt* LPM_Match() {
    struct sr_rt* retval = NULL; 
    return retval; 
}

void send_ICMP_net_unreachable() {

}
                
bool ip_exists_in_ARP_cache() {
    bool retval = false; 
    return retval; 
}

void forward_to_next_hop() {

}
                    
void send_ARP_request() {

}

bool packet_is_a_request_to_me() {
    bool retval = false; 
    return retval; 
}

void construct_and_send_ARP_reply() {

}

void cache_packet() {

}

void send_outstanding_packets() {

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
    }

    print_hdrs(packet, len); 

    switch(ethertype(packet)) {
        case ethertype_ip: 
            /* Check if I need to forward the packet or not */
            if(packet_is_directly_for_me(sr, (sr_ip_hdr_t* ) (packet + sizeof(struct sr_ethernet_hdr)))) { 
                
                fprintf(stderr, "I am end destination of this IP packet\n"); 

                if(packet_is_an_echo_req(packet)) {
                    send_echo_reply();
                }
                else if(packet_is_TCP_UDP()) {
                    send_ICMP_port_unreachable();
                }
            }
            else { /* Packet is not for me */
                if(LPM_Match() == NULL) {
                    send_ICMP_net_unreachable(); 
                }
                else { /* check ARP cache */
                    if(ip_exists_in_ARP_cache()) {
                        forward_to_next_hop(); 
                    }
                    else {
                        send_ARP_request();
                        /*repeat 5x (handled in sr_arpcache.c)*/
                    }
                }
            }
            break; 
        case ethertype_arp: 
            if(packet_is_a_request_to_me()) {
                construct_and_send_ARP_reply(); 
            }
            else { /* the packet is a reply to me */
                cache_packet(); 
                send_outstanding_packets(); 
            }
            break; 
        default:
            printf("No clue what happened here\n");
            fprintf(stderr, "Received something not IP or ARP\n"); 
            break;
    }


}/* end sr_ForwardPacket */