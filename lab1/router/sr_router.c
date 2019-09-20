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
    struct sr_ethernet_hdr_t* eth_hdr;
    eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    struct sr_if* rx_if = malloc(sizeof(struct sr_if)); 
    rx_if = sr_get_interface(sr, interface); 

    struct sr_ip_hdr_t* ip_hdr;

    printf("*** -> Received packet of length %d \n",len);

    /* Decide what to do based on what type of packet you received */
    switch(ethertype(packet)) { 
        case ethertype_arp:

            break; 
        case ethertype_ip:
/*            memcpy(ip_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_ip_hdr_t));  */            
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
            if(recd_chk_sum != calcd_chk_sum) {
                printf("Checksum Error, Dropping packet\n"); 
                printf("From Packet: %i, Calculated: %i\n", recd_chk_sum, calcd_chk_sum); 
                return; 
            } /* otherwise continue and handle the packet! */

            break; 
        default: 
            printf("The Chuck Router only handles ARP & IP Packets at this time, sorry!\n"); 

    }



    /* FREE UP MEMORY */
    free(eth_hdr); 
    free(rx_if); 
    free(ip_hdr);

}/* end sr_ForwardPacket */
