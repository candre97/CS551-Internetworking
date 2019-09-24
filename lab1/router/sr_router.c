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

/* fills the inputted ethernet header with stuff */
void create_eth_hdr(uint8_t* dest, uint8_t* src, uint16_t e_type, sr_ethernet_hdr_t* eth_hdr) {

    if(sizeof(dest) != (ETHER_ADDR_LEN * sizeof(uint8_t))) {
        fprintf(stderr, "inputted destination not right size\n"); 
        return; 
    }
    if(sizeof(src) != (ETHER_ADDR_LEN * sizeof(uint8_t))) {
        fprintf(stderr, "inputted source not right size\n"); 
        return; 
    }
    memcpy(eth_hdr->ether_dhost, dest, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    memcpy(eth_hdr->ether_shost, src, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    eth_hdr->ether_type = htons(e_type); 
    return; 
}

/* Fills inputted ARP header with stuff */
void create_arp_hdr(unsigned short op_code, unsigned char* s_hw_addr, uint32_t s_ip_addr, unsigned char* t_hw_addr, uint32_t t_ip_addr, sr_ip_hdr_t* ip_hdr) {
    arp_hdr->ar_hrd = htons(arp_hrd_ethernet); /* == ETHERNET!! */
    arp_hdr->ar_pro = htons(ethertype_ip); /* TODO, verify this is really IP == IP */
    arp_hdr->ar_hln = ETHER_ADDR_LEN * sizeof(uint8_t); /* == 6 */
    arp_hdr->ar_pln = sizeof(uint32_t); /* == 4 */
    arp_hdr->ar_op = htons(op_code); 
    memcpy(arp_hdr->ar_sha, s_hw_addr, ETHER_ADDR_LEN * sizeof(unsigned char));
    arp_hdr->ar_sip = htonl(s_ip_addr); 
    memcpy(arp_hdr->ar_tha, t_hw_addr, ETHER_ADDR_LEN * sizeof(unsigned char));
    arp_hdr->ar_tip = htonl(t_ip_addr); 
}

void create_icmp_hdr(uint8_t type, uint8_t code, uint16_t sum, sr_icmp_hdr_t* icmp_hdr) {
    icmp_hdr->icmp_type = type; 
    icmp_hdr->icmp_code = code;
    icmp_hdr->icmp_sum = 0;
    icmp_hdr->icmp_sum = cksum((uint8_t*) icmp_hdr, sizeof(sr_icmp_hdr_t)); 
}

void create_icmp_t3_hdr(uint8_t type, uint8_t code, uint16_t sum, uint8_t* data, sr_icmp_t3_hdr_t* icmp_hdr) {
    icmp_hdr->icmp_type = type; 
    icmp_hdr->icmp_code = code;
    memcpy(icmp_hdr->data, data, ICMP_DATA_SIZE * sizeof(uint8_t)); 
    icmp_hdr->icmp_sum = 0;
    icmp_hdr->icmp_sum = cksum((uint8_t*) icmp_hdr, sizeof(sr_icmp_hdr_t)); 
}
/* 
    Here data_size will either be == sizeof(icmpt3_hdr_) or sizeof(icmp_hdr)
    IP off assumes that you are going to be sending only short packets
*/
void create_ip_hdr(uint8_t ttl, uint16_t sum, uint32_t src, uint32_t dest, sr_ip_hdr_t* ip_hdr, unsigned int data_size) {
    /* IP header filling */
    /* TODO: change er_xxxx, er means echo reply */
    ip_hdr->ip_hl = 4;
    ip_hdr->ip_v = 4;
    ip_hdr->ip_tos = 0; /* TODO: update based on what your receive */
    ip_hdr->ip_len = htons((uint16_t)(sizeof(sr_ip_hdr_t) + data_size));
    ip_hdr->ip_id = htons(ip_id_num++);   /* TODO: calculate new IP ID */
    ip_hdr->ip_off = 0;
    ip_hdr->ip_ttl = ttl;
    ip_hdr->ip_p = ip_protocol_icmp; 
    ip_hdr->ip_src = htonl(src);
    ip_hdr->ip_dst = htonl(dest);
    ip_hdr->ip_sum = 0; 
    ip_hdr->ip_sum = cksum((uint8_t* )ip_hdr, sizeof(sr_ip_hdr_t)); 
}

void handle_ip_packet_for_me(sr_instance_t* sr, uint8_t* packet, char* interface, unsigned int len) {
    
    sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
    uint8_t protocol = ip_hdr->ip_p; 
    
    switch(protocol) {
        case ip_protocol_icmp: /* get type, if its an echo request, send an echo reply if you can */
            sr_arp_entry_t* dest_entry = sr_arpcache_lookup(&sr.cache, ip_hdr->ip_src); 
            if(dest_entry == NULL) {
                /* Create an ARP request */
                sr_arpcache_queuereq((struct sr_arpcache*) &sr->cache,
                                       (uint32_t) ip_hdr->ip_src,
                                       (uint8_t*) packet,           /* borrowed */
                                       (unsigned int) len,
                                       (char*) interface);
            }
            else { /* You can send here so send an echo reply */
                sr_ethernet_hdr_t er_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
                sr_ip_hdr_t* er_ip_hdr = malloc(sizeof(sr_ip_hdr_t));
                sr_icmp_hdr_t* er_icmp_hdr = malloc(sizeof(sr_icmp_hdr_t)); 
                
            }
            
            break;
        default:  /* otherwise send ICMP port unreachable */
            fprintf(stderr, "Received an IP Packet that is not ICMP, probably TCP/UDP\n"); 

            break
    }
}


void forward_packet(sr_instance_t* sr, uint8_t* packet, char* interface, unsigned int len, uint32_t dest_addr) {
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
                }*/
}

void send_ip_packet() {
    /* Make sure we know the hardware address and its valid */

    /* Otherwise add an ARP request to the ARP request queue */ 
    create_ip_hdr(uint8_t ttl, uint16_t sum, uint32_t src, uint32_t dest, er_ip_hdr, unsigned int data_size); 
}

/* Handles receiving an IP packet and moves responsibility over to other functions to do heavy lifting */
void receive_IP_packet(sr_instance_t* sr, uint8_t* packet, char* interface, unsigned int len) {

    /* Determine if the packet is for me */
    sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
    uint32_t dest_addr = ip_hdr->ip_dst; 

    struct sr_if* inf;

    /* verify that this was received legitamately on my interface PSOE */
    for (inf = sr->if_list; inf != NULL; inf = inf->next) {
        if (inf->ip == dest_addr) {
            break; 
        }
    }

    inf = sr_get_interface(interface);

    /* Figure out if I am the final destionation of the packet */
    uint32_t my_address = inf->ip; 

    if(dest_addr == my_address) {
        fprintf(stderr, "DO NOT USE ATON\n", );
        handle_ip_packet_for_me(sr, packet, interface, len); 
    }
    else if(dest_addr == inet_aton(my_address)) {
        fprintf(stderr, "USE ATON\n", );
        handle_ip_packet_for_me(sr, packet, interface, len); 
    }
    else {
        /* This packet is not for me, find the person its intended for and send it to them */
        forward_packet(sr, packet, interface, len, dest_addr); 
    }

    return;
}

/* 
    You just received an ARP request. 
    1) create an ARP reply packet
    2) send the ARP reply packet
*/
void send_arp_reply(sr_instance_t* sr, uint8_t* packet, char* interface, unsigned int len) {
    /* get ethernet header */
    struct sr_ethernet_hdr* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    /* Get the ARP header from the message */
    struct sr_arp_hdr* arp_hdr = malloc(sizeof(sr_arp_hdr_t));
    memcpy(arp_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_arp_hdr_t));

    /* AR as in ARP reply */
    uint8_t* ar_packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
    struct sr_ethernet_hdr* ar_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_arp_hdr* ar_arp_hdr = malloc(sizeof(sr_arp_hdr_t)); 

    struct sr_if* intf = sr_get_interface(sr, interface); 

    /* Fill in the Ethernet header of your message */
    create_eth_hdr((uint8_t*) eth_hdr->ether_shost, (uint8_t*) intf->addr, (uint16_t) ethertype_arp, ar_eth_hdr);

    /* Create an ARP header for an ARP reply */
    create_arp_hdr((unsigned short) 2, (unsigned char*) intf->addr, (uint32_t) intf->ip, (unsigned char*) arp_hdr->ar_sha, 
        (uint32_t) arp_hdr->ar_sip, ar_ip_hdr); 

    /* Fill the ARP header */
    memcpy(ar_packet, ar_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(ar_packet + sizeof(sr_ethernet_hdr_t), ar_arp_hdr, sizeof(sr_arp_hdr_t)); 

    fprintf(stderr, "PACKET THAT I JUST MADE:\n");
    print_hdrs((uint8_t* ) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 

    sr_send_packet(sr, (uint8_t*) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t), (const char*) interface); 

    free(eth_hdr); 
    free(arp_hdr); 
    free(intf); 
    free(ar_eth_hdr); 
    free(ar_arp_hdr); 
    free(ar_packet); 
    free(eth_addr); 
}


/* Takes in an almost ready to go packet, fills in the ethernet header, and sends it off */
void send_outstanding_packet(pac->buf, arp_hdr->ar_sha) {
    /* check packet type, fill in accordingly */
    switch(ethertype(packet)) {
        case ethertype_ip:
            break; 
        case ethertype_arp:
            fprintf(stderr, "Outstanding ARP packet, weird!\n");
            break;
        default: 
            fprintf(stderr, "Almost sent out a whacko packet\n");
            break;  
    }
    return; 
}


/* 
    You just received an ARP reply. 
    1) Cache the response
    2) Go through my request queue and send outstanding packets
*/
void handle_arp_reply(sr_instance_t* sr, uint8_t* packet, char* interface, unsigned int len) {

        /*
        This method performs two functions:
           1) Looks up this IP in the request queue. If it is found, returns a pointer
              to the sr_arpreq with this IP. Otherwise, returns NULL.
           2) Inserts this IP to MAC mapping in the cache, and marks it valid. 
        struct sr_arpreq *sr_arpcache_insert(struct sr_arpcache *cache,
                                             unsigned char *mac,
                                             uint32_t ip)
        */

    /* get ethernet header */
    struct sr_ethernet_hdr* eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    memcpy(eth_hdr, packet, sizeof(sr_ethernet_hdr_t)); 

    /* Get the ARP header from the message */
    struct sr_arp_hdr* arp_hdr = malloc(sizeof(sr_arp_hdr_t));
    memcpy(arp_hdr, packet + sizeof(sr_ethernet_hdr_t), sizeof(sr_arp_hdr_t));

    /* Cache the response */
    struct sr_arpreq* waiting = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip); 

    if(waiting == NULL)
    {
        /* No one waiting on this request to be fulfilled */
        fprintf(stderr, "No one was waiting on this reply (excpet me)\n"); 
        return; 
    } /* Inform others waiting on this reply */
    else {
        /* Loop through all the requests waiting on this reply */
        for(waiting; waiting != NULL; waiting = waiting->next) {
            struct sr_packet* pac = waiting->packets;
            /* Loop through all of the packets waiting on this request */
            for(pac; pac!= NULL; pac = pac->next) {
                send_outstanding_packet(pac->buf, arp_hdr->ar_sha); 
            }
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

            if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t))) {
                fprintf(stderr, "Packet is too short to be an IP packet\n");
                return; 
            }

            /* If you have both of these you can process the IP Packet!! */
            receive_IP_packet(sr, packet, interface, len); 

            break; 
        case ethertype_arp: 

            fprintf(stderr, "Got an ARP Packet\n"); 
            sr_arp_hdr_t* arp_hdr = (sr_arp_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
            uint8_t arp_op_code = arp_hdr->ar_op; 

            if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t))) {
                fprintf(stderr, "Packet is too short to be an ARP packet\n");
                return; 
            }

            if(arp_op_code == arp_op_request) {
                /*fprintf(stderr, "Received a request\n"); */                
                send_arp_reply(sr, packet, interface, len); 
            }
            else if(arp_op_code == arp_op_reply) { /* the packet is a reply to me */
                /* cache it! Go through requests queue and send outstanding packets */
                handle_arp_reply(sr, packet, interface, len); 
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