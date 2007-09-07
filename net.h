/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * net.h: Protocol Headers and Constants Definitions.
 */

#ifndef __NET_H__
#define __NET_H__  1

#include "utils.h"

#define N_IP_ADDR_LEN   4 
#define N_IP_ADDR_BITS  32

#define N_IPV6_ADDR_LEN   16
#define N_IPV6_ADDR_BITS  128

/* IPv4 Address definition */
typedef m_uint32_t n_ip_addr_t;

/* IP Network definition */
typedef struct {
   n_ip_addr_t net_addr;
   n_ip_addr_t net_mask;
}n_ip_network_t;

/* IPv6 Address definition */
typedef struct {
   union {
      m_uint32_t u6_addr32[4];
      m_uint16_t u6_addr16[8];
      m_uint8_t  u6_addr8[16];
   }ip6;
}n_ipv6_addr_t;

/* IPv6 Network definition */
typedef struct {
   n_ipv6_addr_t net_addr;
   u_int net_mask;
}n_ipv6_network_t;

/* IP: Common Protocols */
#define N_IP_PROTO_ICMP    1
#define N_IP_PROTO_IGMP    2
#define N_IP_PROTO_TCP     6
#define N_IP_PROTO_UDP     17
#define N_IP_PROTO_IPV6    41
#define N_IP_PROTO_GRE     47
#define N_IP_PROTO_ESP     50
#define N_IP_PROTO_AH      51
#define N_IP_PROTO_ICMPV6  58
#define N_IP_PROTO_EIGRP   88
#define N_IP_PROTO_OSPF    89
#define N_IP_PROTO_PIM     103
#define N_IP_PROTO_SCTP    132
#define N_IP_PROTO_MAX     256

#define N_IP_OFFMASK   0x1fff

/* Maximum number of ports */
#define N_IP_PORT_MAX  65536

/* TCP: Header Flags */
#define N_TCP_FIN    0x01
#define N_TCP_SYN    0x02
#define N_TCP_RST    0x04
#define N_TCP_PUSH   0x08
#define N_TCP_ACK    0x10
#define N_TCP_URG    0x20

#define N_TCP_FLAGMASK   0x3F

/* IPv6 Header Codes */
#define N_IPV6_PROTO_ICMP       58
#define N_IPV6_OPT_HOP_BY_HOP    0   /* Hop-by-Hop header */
#define N_IPV6_OPT_DST          60   /* Destination Options Header */
#define N_IPV6_OPT_ROUTE        43   /* Routing header */
#define N_IPV6_OPT_FRAG         44   /* Fragment Header */
#define N_IPV6_OPT_AH           51   /* Authentication Header */
#define N_IPV6_OPT_ESP          50   /* Encryption Security Payload */
#define N_IPV6_OPT_COMP        108   /* Payload Compression Protocol */
#define N_IPV6_OPT_END          59   /* No more headers */

/* Standard Ethernet MTU */
#define N_ETH_MTU   1500

/* Ethernet Constants */
#define N_ETH_ALEN  6
#define N_ETH_HLEN  sizeof(n_eth_hdr_t)

/* Minimum size for ethernet payload */
#define N_ETH_MIN_DATA_LEN   46
#define N_ETH_MIN_FRAME_LEN  (N_ETH_MIN_DATA_LEN + N_ETH_HLEN)

#define N_ETH_PROTO_IP       0x0800
#define N_ETH_PROTO_IPV6     0x86DD
#define N_ETH_PROTO_ARP      0x0806
#define N_ETH_PROTO_DOT1Q    0x8100
#define N_ETH_PROTO_DOT1Q_2  0x9100
#define N_ETH_PROTO_DOT1Q_3  0x9200
#define N_ETH_PROTO_MPLS     0x8847
#define N_ETH_PROTO_MPLS_MC  0x8848
#define N_ETH_PROTO_LOOP     0x9000

/* size needed for a string buffer */
#define N_ETH_SLEN  (N_ETH_ALEN*3)

/* ARP opcodes */
#define N_ARP_REQUEST  0x1
#define N_ARP_REPLY    0x2

/* Ethernet Address */
typedef struct {
   m_uint8_t eth_addr_byte[N_ETH_ALEN];
} __attribute__ ((__packed__)) n_eth_addr_t;

/* Ethernet Header */
typedef struct {
   n_eth_addr_t daddr;    /* destination eth addr */
   n_eth_addr_t saddr;    /* source ether addr    */
   m_uint16_t   type;     /* packet type ID field */
} __attribute__ ((__packed__)) n_eth_hdr_t;

/* 802.1Q Ethernet Header */
typedef struct {
   n_eth_addr_t daddr;    /* destination eth addr */
   n_eth_addr_t saddr;    /* source ether addr    */
   m_uint16_t   type;     /* packet type ID field (0x8100) */
   m_uint16_t   vlan_id;  /* VLAN id + CoS */
} __attribute__ ((__packed__)) n_eth_dot1q_hdr_t;

/* LLC header */
typedef struct {
   m_uint8_t dsap;
   m_uint8_t ssap;
   m_uint8_t ctrl;
} __attribute__ ((__packed__)) n_eth_llc_hdr_t;

/* SNAP header */
typedef struct {
   m_uint8_t oui[3];
   m_uint16_t type;
} __attribute__ ((__packed__)) n_eth_snap_hdr_t;

/* Cisco ISL header */
typedef struct {
   m_uint16_t hsa1;       /* High bits of source MAC address */
   m_uint8_t  hsa2;       /* (in theory: 0x00-00-0c) */
   m_uint16_t vlan;       /* VLAN + BPDU */
   m_uint16_t index;      /* Index port of source */
   m_uint16_t res;        /* Reserved for TokenRing and FDDI */
} __attribute__ ((__packed__)) n_eth_isl_hdr_t;

#define N_ISL_HDR_SIZE  (sizeof(n_eth_llc_hdr_t) + sizeof(n_eth_isl_hdr_t))

/* Cisco SCP/RBCP header */
typedef struct {
   m_uint8_t sa;          /* Source Address */
   m_uint8_t da;          /* Destination Address */
   m_uint16_t len;        /* Data Length */
   m_uint8_t dsap;        /* Destination Service Access Point */
   m_uint8_t ssap;        /* Source Service Access Point */
   m_uint16_t opcode;     /* Opcode */
   m_uint16_t seqno;      /* Sequence Number */
   m_uint8_t flags;       /* Flags: command/response */
   m_uint8_t unk1;        /* Unknown */
   m_uint16_t unk2;       /* Unknown */
   m_uint16_t unk3;       /* Unknown */
} __attribute__ ((__packed__)) n_scp_hdr_t;

/* Check for a broadcast ethernet address */
static inline int eth_addr_is_bcast(n_eth_addr_t *addr)
{
   static const char *bcast_addr = "\xff\xff\xff\xff\xff\xff";
   return(!memcmp(addr,bcast_addr,6));

}

/* Check for a broadcast/multicast ethernet address */
static inline int eth_addr_is_mcast(n_eth_addr_t *addr)
{
   return(addr->eth_addr_byte[0] & 1);
}

/* Check for Cisco ISL destination address */
static inline int eth_addr_is_cisco_isl(n_eth_addr_t *addr)
{
   static const char *isl_addr = "\x01\x00\x0c\x00\x00";
   return(!memcmp(addr,isl_addr,5));  /* only 40 bits to compare */
}

/* Check for a SNAP header */
static inline int eth_llc_check_snap(n_eth_llc_hdr_t *llc_hdr)
{
   return((llc_hdr->dsap == 0xAA) &&
          (llc_hdr->ssap == 0xAA) &&
          (llc_hdr->ctrl == 0x03));
}

/* Number of bits in a contiguous netmask */
static inline int ip_bits_mask(n_ip_addr_t mask)
{
   int prefix = 0;

   while(mask) {
      prefix++;
      mask = mask & (mask - 1);
   }
   return(prefix);
}

/* Initialize IPv6 masks */
void ipv6_init_masks(void);

/* Convert an IPv4 address into a string */
char *n_ip_ntoa(char *buffer,n_ip_addr_t ip_addr);

/* Convert in IPv6 address into a string */
char *n_ipv6_ntoa(char *buffer,n_ipv6_addr_t *ipv6_addr);

/* Convert a string containing an IP address in binary */
int n_ip_aton(n_ip_addr_t *ip_addr,char *ip_str);

/* Convert an IPv6 address from string into binary */
int n_ipv6_aton(n_ipv6_addr_t *ipv6_addr,char *ip_str);

/* Parse an IPv4 CIDR prefix */
int ip_parse_cidr(char *token,n_ip_addr_t *net_addr,n_ip_addr_t *net_mask);

/* Parse an IPv6 CIDR prefix */
int ipv6_parse_cidr(char *token,n_ipv6_addr_t *net_addr,u_int *net_mask);

/* Parse a MAC address */
int parse_mac_addr(n_eth_addr_t *addr,char *str);

/* Convert an Ethernet address into a string */
char *n_eth_ntoa(char *buffer,n_eth_addr_t *addr,int format);

/* Create a new socket to connect to specified host */
int udp_connect(int local_port,char *remote_host,int remote_port);

/* Listen on the specified port */
int ip_listen(char *ip_addr,int port,int sock_type,int max_fd,int fd_array[]);

/* ISL rewrite */
void cisco_isl_rewrite(m_uint8_t *pkt,m_uint32_t tot_len);

#endif
