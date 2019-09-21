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
    check packet length, drop if too short
    print_hdrs(packet, len); 

    switch(packettype) {
        case IP packet: 
            if(packet is directly for me) { 
                if(packet is an echo reply) {
                    send echo reply
                }
                if(packet is TCP/UDP) {
                    send ICMP port unreachable
                }
            }
            else { /* Packet is not for me */
                if(!LPM Match) {
                    send ICMP net unreachable
                }
                else { /* check ARP cache */
                    if(ip exists in ARP cache) {
                        forward the message to the next hop
                    }
                    else {
                        send ARP request
                        repeat 5x (handled in sr_arpcache.c)
                    }
                }
            }
        case ARP Packet: 
            if(packet is a request to me) {
                construct an ARP reply and send it back
            }
            else { /* the packet is a reply to me */
                cache it
                go through my request queue and send outstanding packets
            }
    }


}/* end sr_ForwardPacket */