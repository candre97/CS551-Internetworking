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

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */);

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
/*
    if(sizeof(dest) != (ETHER_ADDR_LEN * sizeof(uint8_t))) {
        fprintf(stderr, "inputted destination not right size\n"); 
        return; 
    }
    if(sizeof(src) != (ETHER_ADDR_LEN * sizeof(uint8_t))) {
        fprintf(stderr, "inputted source not right size\n"); 
        return; 
    }*/
    memcpy(eth_hdr->ether_dhost, dest, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    memcpy(eth_hdr->ether_shost, src, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    eth_hdr->ether_type = htons(e_type); 
    return; 
}

void create_ip_hdr2(struct sr_ip_hdr* copy_to, struct  sr_ip_hdr* copy_from) {
    copy_to->ip_hl = copy_from->ip_hl;
    copy_to->ip_v = copy_from->ip_v;
    copy_to->ip_tos = copy_from->ip_tos;
    copy_to->ip_len = copy_from->ip_len;
    copy_to->ip_id = copy_from->ip_id;   /* TODO: calculate new IP ID */
    copy_to->ip_off = copy_from->ip_off;
    copy_to->ip_ttl = copy_from->ip_ttl;
    copy_to->ip_p = copy_from->ip_p; 
    copy_to->ip_src = copy_from->ip_src;
    copy_to->ip_dst = copy_from->ip_dst;
    copy_to->ip_sum = 0; 
    copy_to->ip_sum = cksum((uint8_t* )copy_to, sizeof(sr_ip_hdr_t)); 
}

void create_eth_hdr2(struct sr_ethernet_hdr* eth_hdr_copy, struct sr_ethernet_hdr* eth_hdr) {
    memcpy(eth_hdr_copy->ether_dhost, eth_hdr->ether_dhost, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    memcpy(eth_hdr_copy->ether_shost, eth_hdr->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t)); 
    eth_hdr_copy->ether_type = (eth_hdr->ether_type); 
    /*fprintf(stderr, "with htons: %i\n", htons(eth_hdr->ether_type)); 
    fprintf(stderr, "without htons: %i\n", (eth_hdr->ether_type)); */
    return;
}

void create_icmp_hdr2(struct sr_icmp_hdr* icmp_hdr_copy, struct sr_icmp_hdr* icmp_hdr) {
    icmp_hdr_copy->icmp_type = icmp_hdr->icmp_type; 
    icmp_hdr_copy->icmp_code = icmp_hdr->icmp_code;
    icmp_hdr_copy->icmp_sum = 0;
    icmp_hdr_copy->icmp_sum = cksum((void*) icmp_hdr_copy, sizeof(sr_icmp_hdr_t)); 
}

/* Fills inputted ARP header with stuff */
void create_arp_hdr(unsigned short op_code, unsigned char* s_hw_addr, uint32_t s_ip_addr, unsigned char* t_hw_addr, uint32_t t_ip_addr, sr_arp_hdr_t* arp_hdr) {
    arp_hdr->ar_hrd = htons(arp_hrd_ethernet); /* == ETHERNET!! */
    arp_hdr->ar_pro = htons(ethertype_ip); /* TODO, verify this is really IP == IP */
    arp_hdr->ar_hln = ETHER_ADDR_LEN * sizeof(uint8_t); /* == 6 */
    arp_hdr->ar_pln = sizeof(uint32_t); /* == 4 */
    arp_hdr->ar_op = htons(op_code); 
    memcpy(arp_hdr->ar_sha, s_hw_addr, ETHER_ADDR_LEN * sizeof(unsigned char));
    arp_hdr->ar_sip = (s_ip_addr); 
    memcpy(arp_hdr->ar_tha, t_hw_addr, ETHER_ADDR_LEN * sizeof(unsigned char));
    arp_hdr->ar_tip = t_ip_addr; 
}

void create_icmp_hdr(uint8_t type, uint8_t code, uint16_t sum, struct sr_icmp_hdr* icmp_hdr) {
    icmp_hdr->icmp_type = type; 
    icmp_hdr->icmp_code = code;
    icmp_hdr->icmp_sum = 0;
    icmp_hdr->icmp_sum = cksum((void*) icmp_hdr, sizeof(sr_icmp_hdr_t)); 
}

void create_icmp_t3_hdr(uint8_t type, uint8_t code, uint16_t sum, uint8_t* data, struct sr_icmp_t3_hdr* icmp_hdr) {
    icmp_hdr->icmp_type = type; 
    icmp_hdr->icmp_code = code;
    memcpy(icmp_hdr->data, data, ICMP_DATA_SIZE * sizeof(uint8_t)); 
    icmp_hdr->icmp_sum = 0;
    icmp_hdr->icmp_sum = cksum((void*) icmp_hdr, sizeof(sr_icmp_t3_hdr_t)); 
}
/* 
    Here data_size will either be == sizeof(icmpt3_hdr_) or sizeof(icmp_hdr)
    IP off assumes that you are going to be sending only short packets
*/
void create_ip_hdr(uint8_t ttl, uint16_t sum, uint32_t src, uint32_t dest, sr_ip_hdr_t* ip_hdr, uint16_t data_size) {
    /* IP header filling */
    /* TODO: change er_xxxx, er means echo reply */
    ip_hdr->ip_hl = 5;
    ip_hdr->ip_v = 4;
    ip_hdr->ip_tos = 0; 
    ip_hdr->ip_len = htons(data_size);
    ip_hdr->ip_id = htons(ip_id_num++);   /* TODO: calculate new IP ID */
    ip_hdr->ip_off = htons(0);
    ip_hdr->ip_ttl = ttl;
    ip_hdr->ip_p = ip_protocol_icmp; 
    ip_hdr->ip_src = htonl(src);
    ip_hdr->ip_dst = htonl(dest);
    ip_hdr->ip_sum = 0; 
    ip_hdr->ip_sum = cksum((void* )ip_hdr, sizeof(sr_ip_hdr_t)); 
}

int get_mask_length(uint32_t mask) {
    int retval = 0;
    uint32_t bits = 0x80000000; 

    while(1) {
        if((bits != 0) && ((bits & mask) != 0)) {
            return retval;
        }
        bits >>= 1; 
        retval += 1; 
    }

    return retval;
}


struct sr_rt* LPM(struct sr_instance* sr, in_addr_t dest_addr) {
    struct sr_rt* retval = NULL;
    int longest = -10; 

    struct sr_rt* rt_itr = sr->routing_table; 

    if(rt_itr == NULL) {
        fprintf(stderr, "change to a while loop\n"); 
    }

    for(rt_itr; rt_itr != NULL; rt_itr = rt_itr->next) {
        if(get_mask_length(rt_itr->mask.s_addr) > longest) {
            fprintf(stderr, "Found a longer mask\n");
            if((ntohl(rt_itr->dest.s_addr) & rt_itr->mask.s_addr) == (dest_addr & rt_itr->mask.s_addr)) {
                fprintf(stderr, "Found a match!\n"); 
                longest = get_mask_length(rt_itr->mask.s_addr);
                retval = rt_itr; 
                break;
            }
        }
    }
    if(retval != NULL){ 
        struct sr_if* interf = sr_get_interface(sr, retval->interface); 
        fprintf(stderr, "Found a match: %s\n", retval->interface);
        fprintf(stderr, "can route to: "); 
        print_addr_ip(retval->dest); 
        fprintf(stderr, "via my interface at:\n");
        print_addr_ip_int(htonl(interf->ip));
    }
    return retval; 
}

void forward_packet(struct sr_instance* sr, uint8_t* packet, char* interface, unsigned int len, uint32_t dest_addr) {
    /*if(LPM_Match() == NULL) {
                    send_ICMP_net_unreachable(); 
                }
                else { check ARP cache 
                    if(ip_exists_in_ARP_cache()) {
                        forward_to_next_hop(); 
                    }
                    else {
                        send_ARP_request();
                        construct_and_send_ARP(sr, (uint16_t) 1, packet, interface); 
                        repeat 5x (handled in sr_arpcache.c)
                    }
                }*/
    sr_ethernet_hdr_t* eth_hdr = (sr_ethernet_hdr_t* )(packet); 
    sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
    sr_icmp_hdr_t* icmp_hdr = (sr_icmp_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));
    
    /* LPM match */
    struct sr_rt* route = LPM(sr, ntohl(dest_addr));
    if(route == NULL) {
        fprintf(stderr, "No route was found!\n"); 
        /* SEND ICMP NET UNREACHABLE */
    }
    else {
        /* See if this destination is in our cache */
        struct sr_if* interf = sr_get_interface(sr, route->interface); 

        fprintf(stderr, "Looking up at this IP:\n");
        print_addr_ip_int(htonl((uint32_t)(route->dest.s_addr))); 

        struct sr_arpentry* dest_entry = sr_arpcache_lookup(&(sr->cache), htonl((uint32_t) route->dest.s_addr)); 

        if (dest_entry == NULL) {

            fprintf(stderr, "DEST IP: ");
            print_addr_ip_int(htonl(dest_addr)); 

            int pac_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t); 
            uint8_t* pac_copy = malloc(pac_len); 
            struct sr_ethernet_hdr* eth_hdr_copy = malloc(sizeof(sr_ethernet_hdr_t)); 
            struct sr_ip_hdr* ip_hdr_copy = malloc(sizeof(sr_ip_hdr_t)); 
            struct sr_icmp_hdr* icmp_hdr_copy = malloc(sizeof(sr_icmp_hdr_t)); 

            create_eth_hdr2(eth_hdr_copy, eth_hdr); 
            create_ip_hdr2(ip_hdr_copy, ip_hdr);
            ip_hdr_copy->ip_id = 27; /* HACK: using this as a flag to send to IP DST, not reflect back towards source */
            create_icmp_hdr2(icmp_hdr_copy, icmp_hdr); 

            memcpy(pac_copy, eth_hdr_copy, sizeof(sr_ethernet_hdr_t)); 
            memcpy(pac_copy + sizeof(sr_ethernet_hdr_t), ip_hdr_copy, sizeof(sr_ip_hdr_t)); 
            memcpy(pac_copy + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), icmp_hdr_copy, sizeof(sr_icmp_hdr_t)); 

            fprintf(stderr, "Adding this packet to request Queue: \n");
            print_hdrs((uint8_t* ) packet, len); 
            fprintf(stderr, "as\n");
            print_hdrs((uint8_t* ) pac_copy, pac_len);
            sr_arpcache_queuereq(&sr->cache, (route->gw.s_addr), (uint8_t* ) pac_copy, pac_len, interface);
            fprintf(stderr, "Added a request to the queue for a MAC to match IP: ");
            print_addr_ip_int(htonl(ip_hdr->ip_dst)); 
            return; /* Do not do anything in this function, you cannot send to this IP yet */
        }
        else {
            /* If you get here then Dest-Entry is not null!! */
            /* */
            int pac_len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t); 
            uint8_t* pac_copy = malloc(pac_len); 
            struct sr_ethernet_hdr* eth_hdr_copy = malloc(sizeof(sr_ethernet_hdr_t)); 
            struct sr_ip_hdr* ip_hdr_copy = malloc(sizeof(sr_ip_hdr_t)); 
            struct sr_icmp_hdr* icmp_hdr_copy = malloc(sizeof(sr_icmp_hdr_t)); 

            create_eth_hdr2(eth_hdr_copy, eth_hdr); 
            create_ip_hdr2(ip_hdr_copy, ip_hdr);
            ip_hdr_copy->ip_id = 27; /* HACK: using this as a flag to send to IP DST, not reflect back towards source */
            create_icmp_hdr2(icmp_hdr_copy, icmp_hdr); 

            memcpy(eth_hdr_copy->ether_shost, interf->addr, ETHER_ADDR_LEN * sizeof(unsigned char));
            memcpy(eth_hdr_copy->ether_dhost, dest_entry->mac, ETHER_ADDR_LEN * sizeof(unsigned char));

            ip_hdr_copy->ip_src = interf->ip;
            ip_hdr_copy->ip_dst = dest_entry->ip; 

            memcpy(pac_copy, eth_hdr_copy, sizeof(sr_ethernet_hdr_t)); 
            memcpy(pac_copy + sizeof(sr_ethernet_hdr_t), ip_hdr_copy, sizeof(sr_ip_hdr_t)); 
            memcpy(pac_copy + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), icmp_hdr_copy, sizeof(sr_icmp_hdr_t)); 

            sr_send_packet(sr, (uint8_t* ) pac_copy, pac_len, interf->name); 
            free(dest_entry); 
            free(pac_copy); 
        }
        

    }
}

void send_icmp_t3_message() {

}

void send_ip_packet() {

}

void send_icmp_echo_reply(struct sr_instance* sr, uint32_t dest_ip, unsigned char* dest_mac, char* interface) {
    
    uint8_t* er_pac = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_ethernet_hdr_t) + sizeof(sr_icmp_hdr_t)); 
    struct sr_ethernet_hdr* er_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_ip_hdr* er_ip_hdr = malloc(sizeof(sr_ip_hdr_t));
    struct sr_icmp_hdr* er_icmp_hdr = malloc(sizeof(sr_icmp_hdr_t)); 

    struct sr_if* interf = sr_get_interface(sr, interface); 

    create_eth_hdr((uint8_t*) dest_mac, (uint8_t*) interf->addr, ethertype_ip, er_eth_hdr);
    create_ip_hdr((uint8_t) 64, (uint16_t) 0, htonl(interf->ip), htonl(dest_ip), er_ip_hdr, 84); 
    create_icmp_hdr((uint8_t) 0, (uint8_t) 0, (uint16_t) 0, er_icmp_hdr);


    memcpy(er_pac, er_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(er_pac + sizeof(sr_ethernet_hdr_t), er_ip_hdr, sizeof(sr_ip_hdr_t)); 
    memcpy(er_pac + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), er_icmp_hdr, sizeof(sr_icmp_hdr_t)); 

    fprintf(stderr, "ECHO REPLY THAT I'M SENDING:\n");
    print_hdrs(er_pac, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); 
    sr_send_packet(sr, (uint8_t* ) er_pac, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t), interface);
}

void handle_ip_packet_for_me(struct sr_instance* sr, uint8_t* packet, char* interface, unsigned int len) {
    
    struct sr_ip_hdr* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ip_hdr));
    uint8_t protocol = ip_hdr->ip_p; 

    fprintf(stderr, "IP Protocol: %i\n", protocol);
    fprintf(stderr, "IP Protocol ntohs: %i\n", ntohs(protocol));

    struct sr_if* intf = sr_get_interface(sr, interface);
    int one = 1; 

    struct sr_arpentry* dest_entry = sr_arpcache_lookup(&(sr->cache), ip_hdr->ip_src); 

    if(dest_entry == NULL) { /* we cannot send here, add an ARP request*/
        /*uint8_t* pac_copy = (sr_ethernet_hdr_t* )malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); */
        struct sr_ip_hdr* ip_hdr_copy = malloc(sizeof(sr_ip_hdr_t)); 

        create_ip_hdr2(ip_hdr_copy, ip_hdr);

        fprintf(stderr, "Adding this packet to request Queue: \n");
        print_hdr_ip((uint8_t* )ip_hdr); 
        fprintf(stderr, "as\n");
        print_hdr_ip((uint8_t* ) ip_hdr_copy);
        sr_arpcache_queuereq(&sr->cache, ip_hdr->ip_src, (uint8_t* ) ip_hdr_copy, ip_hdr->ip_len, interface);
        fprintf(stderr, "Added a request to the queue\n");
        return; /* Do not do anything in this function, you cannot send to this IP yet */
    }

    switch(one) {
        case 1: /* get type, if its an echo request, send an echo reply if you can */
            /* TODO: fix this so you can handle TCP/UDP */
             /* Assume You can send, so send an echo reply */           
            fprintf(stderr, "we have this in our arp cache, so send an echo reply\n");
            print_addr_eth((uint8_t* ) dest_entry->mac) ;
            send_icmp_echo_reply(sr, ip_hdr->ip_src, dest_entry->mac, interface);
            free(dest_entry); 
            break;
        default:  /* otherwise send ICMP port unreachable, type = 3, code = 3 */
            send_icmp_t3_message(); 
            /* free all the stuff you created */
            /*free(er_eth_hdr);
            free(er_ip_hdr); 
            free(er_icmp_hdr);
            free(er_pac); */
            
            break;
    }
}

/* Handles receiving an IP packet and moves responsibility over to other functions to do heavy lifting */
void receive_IP_packet(struct sr_instance* sr, uint8_t* packet, char* interface, unsigned int len) {

    /* Determine if the packet is for me */
    sr_ip_hdr_t* ip_hdr = (sr_ip_hdr_t* )(packet + sizeof(struct sr_ethernet_hdr));
    uint32_t dest_addr = ip_hdr->ip_dst; 

    struct sr_if* inf = sr_get_interface(sr, interface);

    /* Figure out if I am the final destionation of the packet */
    uint32_t my_address = inf->ip; 

    if(dest_addr == my_address) {
        fprintf(stderr, "IP packet was sent to me!\n");
        handle_ip_packet_for_me(sr, packet, interface, len);
        /*print_addr_eth((uint8_t* ) dest_entry->mac) ;
        send_icmp_echo_reply(sr, ip_hdr->ip_src, dest_entry->mac, interface);
        free(dest_entry); */
    }
    else {
        /* This packet is not for me, find the person its intended for and send it to them */
        fprintf(stderr, "IP packet received on %s needs to be forwarded to: ", inf->name); 

        /*struct sr_arpentry* = sr_arpcache_lookup()*/
        forward_packet(sr, packet, interface, len, dest_addr); 
    }

    return;
}

void send_arp_req(struct sr_instance* sr, uint32_t dest_ip, char* interface) {
    unsigned char dest_hw_addr[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    struct sr_if* intf = sr_get_interface(sr, interface); 
    
    uint8_t* ar_packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
    struct sr_ethernet_hdr* ar_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_arp_hdr* ar_arp_hdr = malloc(sizeof(sr_arp_hdr_t)); 

    create_eth_hdr((uint8_t*) dest_hw_addr, (uint8_t*) intf->addr, (uint16_t) ethertype_arp, ar_eth_hdr);
    create_arp_hdr(1, intf->addr, intf->ip, dest_hw_addr, 
        dest_ip, ar_arp_hdr); 

    memset(ar_arp_hdr->ar_tha, 0, ETHER_ADDR_LEN * sizeof(unsigned char));
    /* Fill the ARP header */
    memcpy(ar_packet, ar_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(ar_packet + sizeof(sr_ethernet_hdr_t), ar_arp_hdr, sizeof(sr_arp_hdr_t)); 

    fprintf(stderr, "ARP REQUEST THAT I JUST SENT:\n");
    print_hdrs((uint8_t* ) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
    sr_send_packet(sr, (uint8_t*) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t), (const char*) interface); 

    free(ar_packet); 
}

/* 
    You just received an ARP request. 
    1) create an ARP reply packet
    2) send the ARP reply packet
*/
void send_arp_message(struct sr_instance* sr, uint8_t* packet, char* interface, unsigned int len, unsigned short arp_type) {
    /* get ethernet header */
    struct sr_ethernet_hdr* eth_hdr = (sr_ethernet_hdr_t*)(packet); 

    /* Get the ARP header from the message */
    struct sr_arp_hdr* arp_hdr = (sr_arp_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t)); 

    /* AR as in ARP reply */
    uint8_t* ar_packet = malloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
    struct sr_ethernet_hdr* ar_eth_hdr = malloc(sizeof(sr_ethernet_hdr_t)); 
    struct sr_arp_hdr* ar_arp_hdr = malloc(sizeof(sr_arp_hdr_t)); 

    struct sr_if* intf = sr_get_interface(sr, interface); 

    unsigned char dest_hw_addr[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if(arp_type == 1) { /* You are sending an ARP request*/
        create_eth_hdr((uint8_t*) dest_hw_addr, (uint8_t*) intf->addr, (uint16_t) ethertype_arp, ar_eth_hdr);
        create_arp_hdr(arp_type, intf->addr, intf->ip, dest_hw_addr, 
        arp_hdr->ar_sip, ar_arp_hdr); 
    }
    else { /* You are sending a reply */
        create_eth_hdr((uint8_t*) eth_hdr->ether_shost, (uint8_t*) intf->addr, (uint16_t) ethertype_arp, ar_eth_hdr);
        create_arp_hdr(arp_type, intf->addr, intf->ip, eth_hdr->ether_shost, 
        arp_hdr->ar_sip, ar_arp_hdr); 
    }
    /* Fill in the Ethernet header of your message */
    
    /*print_hdr_eth((uint8_t* ) eth_hdr); */
    /* (unsigned short op_code, unsigned char* s_hw_addr, uint32_t s_ip_addr, unsigned char* t_hw_addr, 
    uint32_t t_ip_addr, sr_arp_hdr_t* arp_hdr) {*/

    /* Create an ARP header for an ARP reply */
    create_arp_hdr(arp_type, intf->addr, intf->ip, eth_hdr->ether_shost, 
        arp_hdr->ar_sip, ar_arp_hdr); 

    /* Fill the ARP header */
    memcpy(ar_packet, ar_eth_hdr, sizeof(sr_ethernet_hdr_t)); 
    memcpy(ar_packet + sizeof(sr_ethernet_hdr_t), ar_arp_hdr, sizeof(sr_arp_hdr_t)); 

    fprintf(stderr, "PACKET THAT I JUST MADE:\n");
    print_hdrs((uint8_t* ) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 

    sr_send_packet(sr, (uint8_t*) ar_packet, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t), (const char*) interface); 
    fprintf(stderr, "sent\n");
    /*free(eth_addr); */
    /*free(ar_packet); 
    free(ar_eth_hdr);
    free(ar_arp_hdr); */


}

/* Takes in the unprocessed packet that is waiting to be dealt with, deals with it now that it can */
void send_outstanding_packet(struct sr_instance* sr, struct sr_arpreq* waiting) {
    /* check packet type, fill in accordingly */
    struct sr_packet* pac_itr = waiting->packets;

    for (pac_itr; pac_itr != NULL; pac_itr = pac_itr->next) {

        fprintf(stderr, "Handling this packet: \n");

        print_hdrs((uint8_t*) pac_itr->buf, pac_itr->len);

        /* Only handles outstanding packets for forwarding */
        struct sr_arpentry* dest_entry = sr_arpcache_lookup(&(sr->cache), waiting->ip);
        
        if(dest_entry != NULL) {

            struct sr_rt* route = LPM(sr, ntohl(waiting->ip)); 
            struct sr_if* inf = sr_get_interface(sr, route->interface);

            memcpy(((sr_ethernet_hdr_t *) pac_itr->buf)->ether_dhost, dest_entry->mac, sizeof(unsigned char) * ETHER_ADDR_LEN);
            memcpy(((sr_ethernet_hdr_t *) pac_itr->buf)->ether_shost, inf->addr, sizeof(unsigned char) * ETHER_ADDR_LEN);
            fprintf(stderr, "replying using this packet: \n"); 
            print_hdrs(pac_itr->buf, pac_itr->len);    
            sr_send_packet(sr, pac_itr->buf, pac_itr->len, route->interface); 
            free(dest_entry); 
        }
    }

    /*struct sr_if* inf = sr_get_interface(sr, pac->iface);

    uint32_t my_address = inf->ip; 

    if(ip_hdr->ip_dst == my_address) {
        fprintf(stderr, "Now I can respond to this packet that was for me\n"); 
        handle_ip_packet_for_me(sr, pac->buf, pac->iface, pac->len);
    }
    else {
        fprintf(stderr, "Now I can forward this packet to this IP: "); 
        print_addr_ip_int(ip_hdr->ip_dst); 
        forward_packet(sr, pac->buf, pac->iface, pac->len, ip_hdr->ip_dst); 
    }*/
    return; 
}


/* 
    You just received an ARP reply. 
    1) Cache the response
    2) Go through my request queue and send outstanding packets
*/
void handle_arp_reply(struct sr_instance* sr, struct sr_ethernet_hdr* eth_hdr , struct sr_arp_hdr* arp_hdr, char* interface, unsigned int len) {

        /*
        This method performs two functions:
           1) Looks up this IP in the request queue. If it is found, returns a pointer
              to the sr_arpreq with this IP. Otherwise, returns NULL.
           2) Inserts this IP to MAC mapping in the cache, and marks it valid. 
        struct sr_arpreq *sr_arpcache_insert(struct sr_arpcache *cache,
                                             unsigned char *mac,
                                             uint32_t ip)
        */

    fprintf(stderr, "Received an ARP reply\n"); 

/*    struct sr_ethernet_hdr* eth_hdr = (sr_ethernet_hdr_t* )(packet);

    struct sr_arp_hdr* arp_hdr = (sr_arp_hdr_t* )(packet + sizeof(sr_ethernet_hdr_t)); */

    /* Cache the response */
    fprintf(stderr, "Updating ARP cache at this IP:\n");


    print_addr_ip_int(ntohl(arp_hdr->ar_sip)); 
    
    struct sr_arpreq* waiting = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, ntohl(arp_hdr->ar_sip)); 

    if(waiting == NULL)
    {
        /* No one waiting on this request to be fulfilled */
        fprintf(stderr, "No requests waiting on this reply\n"); 
        return; 
    } /* Inform others waiting on this reply */
    else {
        /* Loop through all the requests waiting on this reply */
        /*char* ifacee = waiting->packets->iface; 

        struct sr_if* iff = sr_get_interface(sr, ifacee); */

        fprintf(stderr, "New arp entry:\n");
        print_addr_ip_int(htonl(arp_hdr->ar_sip)); 
        print_addr_eth((uint8_t* ) arp_hdr->ar_sha);

        while (waiting != NULL) {
            /* Loop through all of the packets waiting on this request */
            fprintf(stderr, "Handling a request that was waiting on this response\n");
            send_outstanding_packet(sr, waiting); 
  
            waiting = waiting->next; 
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

    struct sr_ethernet_hdr* eth_hdr = (sr_ethernet_hdr_t* )(packet); 

    switch(ethertype(packet)) {
        case ethertype_ip: 
            /* Check if I need to forward the packet or not */
            fprintf(stderr, "Got an IP Packet\n"); 

            struct sr_ip_hdr* ip_hdr = (sr_ip_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t));  
            memcpy(ip_hdr, (packet + sizeof(struct sr_ethernet_hdr)), sizeof(sr_ip_hdr_t));

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
            fprintf(stderr, "Going to handle this IP packet\n"); 
            receive_IP_packet(sr, packet, interface, len); 

            break; 
        case ethertype_arp: 

            fprintf(stderr, "Got an ARP Packet\n"); 

            if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t))) {
                fprintf(stderr, "Packet is too short to be an ARP packet\n");
                return; 
            }

            struct sr_arp_hdr* arp_hdr = (sr_arp_hdr_t*)(packet + sizeof(sr_ethernet_hdr_t)); 

            /* TODO Check if this ARP is to me */

            if(ntohs(arp_hdr->ar_op) == 2) { /* the packet is a reply to me */
                /* cache it! Go through requests queue and send outstanding packets */
                fprintf(stderr, "Received ARP reply\n");  
                print_hdr_arp((uint8_t* ) arp_hdr);
                handle_arp_reply(sr, eth_hdr, arp_hdr, interface, len); 
                return; 
            }

            struct sr_arpentry* dest_entry = sr_arpcache_lookup(&(sr->cache), arp_hdr->ar_sip); 
            if(dest_entry == NULL) {
                fprintf(stderr, "From someone new, adding them to ARP Cache\n");                
                
                struct sr_arpreq* waiting = sr_arpcache_insert(&sr->cache, eth_hdr->ether_shost, (arp_hdr->ar_sip));

                if(waiting == NULL)
                {
                    /* No one waiting on this request to be fulfilled */
                    fprintf(stderr, "No requests waiting on this reply\n"); 
                } /* Inform others waiting on this reply */
                else {
                    /* Loop through all the requests waiting on this reply */
                    char* ifacee = waiting->packets->iface; 

                    struct sr_if* iff = sr_get_interface(sr, ifacee); 

                    fprintf(stderr, "New arp entry:\n");
                    print_addr_ip_int(htonl(iff->ip)); 
                    print_addr_eth((uint8_t* )iff->addr);

                    while (waiting != NULL) {
                        /* Loop through all of the packets waiting on this request */
                        fprintf(stderr, "Handling a request that was waiting on this response\n");
                        send_outstanding_packet(sr, waiting); 
                        waiting = waiting->next; 
                    }
                }
            }
            else {
                free(dest_entry); 
            }            

            if(ntohs(arp_hdr->ar_op) == 1) {
                fprintf(stderr, "Received ARP request\n");             
                send_arp_message(sr, packet, interface, len, 2); 
                return; 
            }
            else { 
                fprintf(stderr, "Just got an ARP packet that was not a request or reply\n");
                fprintf(stderr, "opcode: %i", arp_hdr->ar_op ); 
                fprintf(stderr, "opcode, ntohs: %i", htons(arp_hdr->ar_op)); 
                return; 
            }
            break; 
        default:
            printf("No clue what happened here\n");
            fprintf(stderr, "Received something not IP or ARP\n"); 
            break;
    }


}/* end sr_ForwardPacket */