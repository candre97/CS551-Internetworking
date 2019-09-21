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
 * Method: ip_is_one_of_mine(struct sr_instance* in_sr_inst, unsigned long in_dest_ip)
 * 
 * this method is called to check if the IP address passed in belongs to one of its interfaces
 * if it does, this method returns true
 * if false, you will probably want to forward the packet
*/
bool ip_is_one_of_mine(struct sr_instance* in_sr_inst, unsigned long in_dest_ip) {

    bool retval = false; 
    
    struct sr_if* intf;
    for(intf= in_sr_inst->if_list; intf != NULL; intf = intf->next) {
        if(in_dest_ip == intf->ip) {
            retval = true; 
        }
    }

    return retval; 
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
 /*---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* length */,
        unsigned int len,
        char* interface/* length */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    uint16_t min_ip_len = 5; 

    uint16_t calcd_chk_sum = 0;
    uint16_t recd_chk_sum = 0;
    uint16_t recd_ip_len = 0;

    /* Get info out of the packet */
    if (len > sizeof(sr_ethernet_hdr_t)) {
        printf("Packet is not long enough\n"); 
        return; 
    }

    /* Extract the ethernet header from the message */
    struct sr_ethernet_hdr_t* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    struct sr_if* rx_if = malloc(sizeof(struct sr_if)); 
    rx_if = sr_get_interface(sr, interface); 

    struct sr_ip_hdr* ip_hdr = malloc(sizeof(sr_ip_hdr_t)); 

    printf("*** -> Received packet of length %d \n",len);

    /* Decide what to do based on what type of packet you receive */
    switch(ethertype(packet)) { 
        case ethertype_arp:

            break; 
        case ethertype_ip:
            /* POTENTIAL SOURCE OF ERROR */
            memcpy(ip_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_ip_hdr_t));
            recd_ip_len = ip_hdr->ip_len;  

            if (recd_ip_len < min_ip_len) {
                char* recd_name;
                recd_name = rx_if->name; 
                printf("IP Packet received over %s too short & dropped\n", recd_name);
                return; 
            } 
            calcd_chk_sum = cksum(ip_hdr, ip_hdr->ip_len); 
            recd_chk_sum = ip_hdr->ip_sum; 

            /* Verify checksum */
            if(recd_chk_sum != calcd_chk_sum) {
                printf("Checksum Error, Dropping packet\n"); 
                printf("From Packet: %i, Calculated: %i\n", recd_chk_sum, calcd_chk_sum); 
                return; 
            } /* otherwise continue and handle the packet! */
            
            if(ip_hdr->ip_v != 4) {   
                printf("Routing table cannot hold IPv6 Addresses\n"); 
                return;
            }

            
            if(ip_is_one_of_mine(sr, ip_hdr->ip_dst)) { /* this packet is intended for me! */ 

            }
            else { /* The packet is not for me, gotta forward it! */

            }

            break; 
        default: 
            printf("The Chuck Router only handles ARP & IP Packets at this time, sorry!\n"); 

    }



    /* FREE UP MEMORY */
    free(eth_hdr); 
    free(rx_if); 
    free(ip_hdr);
}