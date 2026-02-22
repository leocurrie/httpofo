/* network.h - Shared network stack for Atari Portfolio */

#ifndef NETWORK_H
#define NETWORK_H

/*============================================================================
 * Configuration
 *============================================================================*/

/* IP addresses (network byte order - big endian) */
extern unsigned long local_ip;

/*============================================================================
 * Byte Order Helpers
 *============================================================================*/

#define htons(x) ((unsigned short)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))
#define ntohs(x) htons(x)
#define htonl(x) ((unsigned long)( \
    (((x) & 0x000000FFUL) << 24) | \
    (((x) & 0x0000FF00UL) << 8)  | \
    (((x) & 0x00FF0000UL) >> 8)  | \
    (((x) & 0xFF000000UL) >> 24)))
#define ntohl(x) htonl(x)

/*============================================================================
 * Protocol Constants
 *============================================================================*/

/* IP protocols */
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/* IP header */
#define IP_HEADER_LEN 20

/* TCP header offsets */
#define TCP_SRC_PORT    0
#define TCP_DST_PORT    2
#define TCP_SEQ_OFF     4
#define TCP_ACK_OFF     8
#define TCP_DATA_OFF    12
#define TCP_FLAGS       13
#define TCP_WINDOW      14
#define TCP_CHECKSUM    16
#define TCP_URGENT      18
#define TCP_HEADER_LEN  20

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* TCP connection states (superset for client and server) */
#define TCP_STATE_CLOSED       0
#define TCP_STATE_LISTEN       1
#define TCP_STATE_SYN_SENT     2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED  4
#define TCP_STATE_FIN_WAIT_1   5
#define TCP_STATE_FIN_WAIT_2   6
#define TCP_STATE_CLOSING      7
#define TCP_STATE_TIME_WAIT    8

/* SLIP constants */
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

/* Buffer sizes */
#define RX_BUF_SIZE  256
#define PKT_BUF_SIZE 576  /* Standard SLIP MTU */

/*============================================================================
 * Global Variables (defined in network.c)
 *============================================================================*/

/* Packet buffer */
extern unsigned char pkt_buf[];
extern unsigned short pkt_len;

/* TCP state */
extern unsigned char tcp_state;
extern unsigned long tcp_remote_ip;
extern unsigned short tcp_local_port;
extern unsigned short tcp_remote_port;
extern unsigned long tcp_seq_num;
extern unsigned long tcp_ack_num;
extern unsigned long tcp_last_ack;

/*============================================================================
 * Function Declarations
 *============================================================================*/

/* Serial layer */
void init_serial(void);
void cleanup_serial(void);
unsigned char rx_available(void);
unsigned char rx_getchar(void);
void tx_putchar(unsigned char c);

/* SLIP layer */
unsigned char slip_poll(void);
void slip_send(unsigned char *data, unsigned char len);

/* IP layer */
void ip_receive(unsigned char *pkt, unsigned short len);
void ip_send(unsigned long dst_ip, unsigned char protocol,
             unsigned char *payload, unsigned char payload_len);

/* Console output */
void print_char(char c);
void print_str(char *s);
void print_uint(unsigned short n);
void print_ulong(unsigned long n);

/* Helper functions */
unsigned short checksum(unsigned char *data, unsigned short len);
unsigned short get_u16(unsigned char *p);
void put_u16(unsigned char *p, unsigned short val);
unsigned long get_u32(unsigned char *p);
void put_u32(unsigned char *p, unsigned long val);
void print_ip(unsigned long ip);
unsigned long parse_ip(char *s);

/* TCP layer */
unsigned short tcp_checksum(unsigned char *tcp_pkt, unsigned short tcp_len,
                            unsigned long src_ip, unsigned long dst_ip);
void tcp_send_flags(unsigned char flags, unsigned char *data, unsigned char data_len);
void tcp_send(unsigned char *data, unsigned char len);
void tcp_close(void);
void tcp_listen(unsigned short port);
void tcp_check_retransmit(void);  /* Call from main loop */

/*============================================================================
 * Application Callbacks (implement in your app)
 *============================================================================*/

/* Called when TCP data is received in ESTABLISHED state */
void app_tcp_data_received(unsigned char *data, unsigned short len);

/* Called when TCP connection state changes */
void app_tcp_state_changed(unsigned char old_state, unsigned char new_state,
                           unsigned long remote_ip, unsigned short remote_port);

/* Called to check if we should accept an incoming SYN (server mode) */
/* Return 1 to accept, 0 to ignore */
unsigned char app_tcp_accept(unsigned long remote_ip, unsigned short remote_port);

#endif /* NETWORK_H */
