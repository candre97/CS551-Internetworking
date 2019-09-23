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
bool packet_is_an_echo_req(uint8_t* in_pac) {

    struct sr_ip_hdr* ip_hdr = malloc(sizeof(sr_ip_hdr_t)); 
    struct sr_icmp_hdr* icmp_hdr = malloc(sizeof(sr_icmp_hdr_t));
    memcpy(ip_hdr, in_pac + sizeof(sr_ethernet_hdr_t), sizeof(sr_ip_hdr_t));

    print_hdr_ip((uint8_t* ) ip_hdr); 

    /* TODO fix this 
    if(htons(ip_hdr->ip_tos) != ip_protocol_icmp) {
        fprintf(stderr, "Received an IP packet that is not an ICMP message\n");
        fprintf(stderr, "TOS from packet: %i\n", htons(ip_hdr->ip_tos)); 
        fprintf(stderr, "ICMP TOS: %i\n", ip_protocol_icmp); 
        free(ip_hdr); 
        free(icmp_hdr); 
        return false; 
    }*/

    memcpy(icmp_hdr, in_pac + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), sizeof(sr_icmp_hdr_t)); 
    print_hdr_icmp((uint8_t* ) icmp_hdr); 

    if(icmp_hdr->icmp_type == 8) {
        fprintf(stderr, "Received an ICMP echo request\n");
        free(ip_hdr); 
        free(icmp_hdr); 
        return true;
    }


    return false; 
}

/* 
    Echoes back on the same interface, using much of the same data that was in the original packet
    A lot of this function was borrowed from my first unorganized attempt at this lab
*/
void send_echo_reply(struct sr_instance* sr, uint8_t* packet, char* interface) {
    
    /* Extract the ethernet header from the message */
    struct sr_ethernet_hdr* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    struct sr_ip_hdr* ip_hdr = malloc(sizeof(sr_ip_hdr_t));
    memcpy(ip_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_ip_hdr_t));

    struct sr_if* rx_if = malloc(sizeof(struct sr_if)); 
    rx_if = sr_get_interface(sr, interface); 
    

    /* Create some headers for the new packet */ 
    uint8_t* er_packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); 
    struct sr_ethernet_hdr* er_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_ip_hdr* er_ip_hdr = malloc(sizeof(sr_ip_hdr_t)); 
    struct sr_icmp_hdr* er_icmp_hdr = malloc(sizeof(sr_icmp_hdr_t)); 

    /* fill the packet with the correct info */

    /* Ethernet header filling */
    /* No need to waste time looking this stuff up, just bounce it back */                          
    memcpy(er_eth_hdr->ether_dhost, eth_hdr->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t));
    memcpy(er_eth_hdr->ether_shost, eth_hdr->ether_dhost, ETHER_ADDR_LEN * sizeof(uint8_t));
    er_eth_hdr->ether_type = htons(ethertype_ip); 

    /* IP header filling */
    er_ip_hdr->ip_hl = ip_hdr->ip_hl;
    er_ip_hdr->ip_v = ip_hdr->ip_v;
    er_ip_hdr->ip_tos = ip_hdr->ip_tos;     
    er_ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t));
    er_ip_hdr->ip_id = htons(ip_id_num++);   /* TODO: calculate new IP ID */
    er_ip_hdr->ip_off = ip_hdr->ip_off;
    er_ip_hdr->ip_ttl = ip_hdr->ip_ttl; /* == default TTL */
    er_ip_hdr->ip_p = ip_protocol_icmp; 
    er_ip_hdr->ip_src = ip_hdr->ip_dst;
    er_ip_hdr->ip_dst = ip_hdr->ip_src; 
    er_ip_hdr->ip_sum = 0; 
    uint16_t er_ip_sum = cksum(er_ip_hdr, sizeof(sr_ip_hdr_t)); 
    er_ip_hdr->ip_sum = er_ip_sum; 

    /* ICMP fill up */
    er_icmp_hdr->icmp_type = 0; 
    er_icmp_hdr->icmp_code = 0; 
    er_icmp_hdr->icmp_sum = 0;
    uint16_t er_icmp_sum = cksum((void* ) er_icmp_hdr, sizeof(sr_icmp_hdr_t)); 
    er_icmp_hdr->icmp_sum = er_icmp_sum; 

    /* Assemble the packet */
    memcpy(er_packet, er_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(er_packet + sizeof(sr_ethernet_hdr_t), er_ip_hdr, sizeof(sr_ip_hdr_t)); 
    memcpy(er_packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), er_icmp_hdr, sizeof(sr_icmp_hdr_t)); 

    fprintf(stderr, "Packet I Received\n");
    print_hdrs(packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); 

    fprintf(stderr, "Packet I Made\n");
    print_hdrs(er_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); 

    /* Routing is straight forward, echo back on same interface */
    sr_send_packet(sr, (uint8_t*) er_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t), rx_if->name); 

    free(rx_if); 

    free(eth_hdr); 
    free(ip_hdr); 

    free(er_eth_hdr); 
    free(er_ip_hdr); 
    free(er_icmp_hdr); 

    free(er_packet); 
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

bool packet_is_a_request_to_me(sr_arp_hdr_t* arp_hdr) {
    
    bool retval = false; 

    /*print_hdr_arp((uint8_t* ) arp_hdr); */
    /* Was having lots of trouble here before realizing I had to do ntohs
    fprintf(stderr, "%i\n", ntohs(arp_hdr->ar_op)); 
    fprintf(stderr, "%i\n", arp_op_request); */

    if(ntohs(arp_hdr->ar_op) == arp_op_request) {
        /*fprintf(stderr, "Request to me\n");*/
        retval = true; 
    }

    return retval; 
}

/* 
    Borrow the packet, make a copy, change around some variables, and send it!!
*/
void construct_and_send_ARP_reply(struct sr_instance* sr, uint8_t* packet, char* interface) {
    /* Extract the ethernet header from the message */
    struct sr_ethernet_hdr* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    /* Get the ARP header from the message */
    struct sr_arp_hdr* arp_hdr = malloc(sizeof(sr_arp_hdr_t));
    memcpy(arp_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_arp_hdr_t));

    /* Create some headers for the new packet */ 
    uint8_t* ar_packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
    struct sr_ethernet_hdr* ar_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_arp_hdr* ar_arp_hdr = malloc(sizeof(sr_arp_hdr_t)); 

    struct sr_if* intf = malloc(sizeof(struct sr_if)); 
    intf = sr_get_interface(sr, interface); 

    if(intf == NULL) {
        fprintf(stderr, "Interface non-existent\n");
        free(eth_hdr); 
        free(arp_hdr); 
        free(intf); 
        free(ar_eth_hdr); 
        free(ar_arp_hdr); 
        free(ar_packet); 
        return; 
    }

    /* Need to look up whether we can send packets to whoever echo requested us here */
    uint8_t* eth_addr = malloc(ETHER_ADDR_LEN * sizeof(unsigned char)); 
    struct sr_arpentry* dest_entry = sr_arpcache_lookup(sr.cache, packet->ip_src); /* Can we send to the guy who sent to us??*/

    /* Ethernet header filling */
    /* No need to waste time looking this stuff up, just bounce it back */    
    if(dest_entry != NULL) {
        memcpy(ar_eth_hdr->ether_dhost, eth_hdr->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t));
    }
    else {
        /* Leave dest HW address blank, gonna send an ARP request, not a reply! */
    }                     
    memcpy(ar_eth_hdr->ether_shost, (uint8_t* )intf->addr, ETHER_ADDR_LEN * sizeof(uint8_t));
    ar_eth_hdr->ether_type = htons(ethertype_arp); 

    /* Fill the ARP header */
    ar_arp_hdr->ar_hrd = arp_hdr->ar_hrd; /* == ETHERNET!! */
    ar_arp_hdr->ar_pro = arp_hdr->ar_pro; /* == IP */
    ar_arp_hdr->ar_hln = arp_hdr->ar_hln; /* == 6 */
    ar_arp_hdr->ar_pln = sizeof(uint32_t); /* == 4 */
    ar_arp_hdr->ar_op = htons((unsigned short) 2); 
    memcpy(ar_arp_hdr->ar_sha, intf->addr, ETHER_ADDR_LEN * sizeof(unsigned char));
    ar_arp_hdr->ar_sip = intf->ip;             /* FIX THIS */
    memcpy(ar_arp_hdr->ar_tha, arp_hdr->ar_sha, ETHER_ADDR_LEN * sizeof(unsigned char));
    ar_arp_hdr->ar_tip = arp_hdr->ar_sip; 

    /* Assemble the packet TODO */
    memcpy(ar_packet, ar_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(ar_packet + sizeof(sr_ethernet_hdr_t), ar_arp_hdr, sizeof(sr_arp_hdr_t)); 

    fprintf(stderr, "HEADERS THAT I JUST MADE:\n");
    print_hdr_eth((uint8_t* ) ar_eth_hdr); 
    print_hdr_arp((uint8_t* ) ar_arp_hdr);

    /* Need to look up whether we can send packets to whoever echo requested us here */

    fprintf(stderr, "Sending an ARP reply\n");
    /* Routing is straight forward, echo back on same interface */
    if(dest_entry == NULL) {
        /* Send an ARP request */
        
    }
    sr_send_packet(sr, (uint8_t*) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t), (const char*) intf->name); 

    free(eth_hdr); 
    free(arp_hdr); 
    free(intf); 
    free(ar_eth_hdr); 
    free(ar_arp_hdr); 
    free(ar_packet); 
    free(eth_addr); 
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
        return; 
    }

    print_hdrs(packet, len); 

    fprintf(stderr, "ETHERTYPE: %i\n", ethertype(packet));
    fprintf(stderr, "ETHERTYPE_IP: %i\n", ethertype_ip);

    switch(ethertype(packet)) {
        case ethertype_ip: 
            /* Check if I need to forward the packet or not */
            fprintf(stderr, "Got an IP Packet\n"); 

            /* Checksum Stuff! */
            sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));

            /*fprintf(stderr, "IP HDRRRRRR: \n"); 
            print_hdr_ip((uint8_t*)ip_hdr); */

            uint16_t recd_chk_sum = ip_hdr->ip_sum; 
            ip_hdr->ip_sum = 0; 
            uint16_t calcd_chk_sum = cksum((void* ) ip_hdr, sizeof(sr_ip_hdr_t)); 
            
            /* Verify checksum */
            if(recd_chk_sum != calcd_chk_sum) {
                printf("Checksum Error, Dropping packet\n"); 
                printf("From Packet: %i, Calculated: %i\n", recd_chk_sum, calcd_chk_sum); 
                return; 
            }

            if(packet_is_directly_for_me(sr, (sr_ip_hdr_t* ) (packet + sizeof(struct sr_ethernet_hdr)))) { 
                
                fprintf(stderr, "I am end destination of this IP packet\n"); 

                if(packet_is_an_echo_req(packet)) {
                    send_echo_reply(sr, packet, interface);
                }
                else/* if(packet_is_TCP_UDP())*/ {
                    /* Only handling echo reply right now */
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
            fprintf(stderr, "Got an ARP Packet\n"); 
            sr_arp_hdr_t* arp_hdr = (sr_arp_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));

            if(packet_is_a_request_to_me(arp_hdr)) {
                /*fprintf(stderr, "Received a request\n"); */
                construct_and_send_ARP_reply(sr, packet, interface); 
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