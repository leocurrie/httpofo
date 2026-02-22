/* network.c - Shared network stack for Atari Portfolio */

#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "network.h"

/*============================================================================
 * UART Definitions
 *============================================================================*/

#define RBR 0  /* Receiver Buffer Register (read) */
#define THR 0  /* Transmitter Holding Register (write) */
#define IER 1  /* Interrupt Enable Register */
#define IIR 2  /* Interrupt Identification Register */
#define LCR 3  /* Line Control Register */
#define MCR 4  /* Modem Control Register */
#define LSR 5  /* Line Status Register */
#define MSR 6  /* Modem Status Register */

#define LSR_DATA_READY    0x01
#define LSR_THRE          0x20
#define IER_RX_DATA       0x01
#define SIVR_PORT         0x807F
#define SERIAL_INT_VECTOR 0x60

/*============================================================================
 * Serial Layer
 *============================================================================*/

volatile unsigned char rx_buf[RX_BUF_SIZE];
volatile unsigned char rx_head = 0;
volatile unsigned char rx_tail = 0;

unsigned short uart_base = 0;
void (__interrupt __far *old_serial_handler)() = 0;

unsigned short get_uart_base(void) {
    unsigned short base = 0;
    __asm {
        push es
        mov ax, 0x0040
        mov es, ax
        xor bx, bx
        mov ax, es:[bx]
        mov base, ax
        pop es
    }
    return base;
}

unsigned char read_uart(unsigned char reg) {
    unsigned char value = 0;
    unsigned short addr = uart_base + reg;
    __asm {
        mov dx, addr
        in al, dx
        mov value, al
    }
    return value;
}

void write_uart(unsigned char reg, unsigned char value) {
    unsigned short addr = uart_base + reg;
    __asm {
        mov dx, addr
        mov al, value
        out dx, al
    }
}

void __interrupt __far serial_interrupt_handler(void) {
    unsigned char lsr, c, next_head;
    (void)read_uart(IIR);
    lsr = read_uart(LSR);
    while (lsr & LSR_DATA_READY) {
        c = read_uart(RBR);
        next_head = (rx_head + 1) % RX_BUF_SIZE;
        if (next_head != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next_head;
        }
        lsr = read_uart(LSR);
    }
}

unsigned char rx_available(void) {
    return (rx_head != rx_tail);
}

unsigned char rx_getchar(void) {
    unsigned char c;
    c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

void tx_putchar(unsigned char c) {
    while (!(read_uart(LSR) & LSR_THRE));
    write_uart(THR, c);
}

void (__interrupt __far *get_vector(unsigned char intnum))() {
    void __far *handler = 0;
    __asm {
        mov ah, 0x35
        mov al, intnum
        int 0x21
        mov word ptr handler, bx
        mov word ptr handler+2, es
    }
    return (void (__interrupt __far *)())handler;
}

void set_vector(unsigned char intnum, void (__interrupt __far *handler)()) {
    __asm {
        push ds
        mov ah, 0x25
        mov al, intnum
        lds dx, dword ptr handler
        int 0x21
        pop ds
    }
}

void init_int61(void) {
    __asm {
        mov ah, 0x00
        int 0x61
    }
}

void write_sivr(unsigned char vector) {
    __asm {
        mov ah, 0x1C
        mov al, 0
        mov bh, 5
        mov bl, vector
        mov dx, SIVR_PORT
        int 0x61
    }
}

void init_serial(void) {
    uart_base = get_uart_base();
    write_uart(IER, 0x00);
    write_uart(LCR, 0x80);
    write_uart(0, 12);
    write_uart(1, 0);
    write_uart(LCR, 0x03);
    write_uart(MCR, 0x03);
    read_uart(LSR);
    read_uart(RBR);
    read_uart(IIR);
    read_uart(MSR);
    old_serial_handler = get_vector(SERIAL_INT_VECTOR);
    set_vector(SERIAL_INT_VECTOR, serial_interrupt_handler);
    init_int61();
    __asm { sti }
    write_sivr(SERIAL_INT_VECTOR);
    write_uart(IER, IER_RX_DATA);
}

void cleanup_serial(void) {
    write_uart(IER, 0x00);
    write_sivr(0);
    set_vector(SERIAL_INT_VECTOR, old_serial_handler);
}

/*============================================================================
 * SLIP Layer
 *============================================================================*/

unsigned char pkt_buf[PKT_BUF_SIZE];
unsigned short pkt_len = 0;
unsigned char slip_escaped = 0;

unsigned char slip_poll(void) {
    unsigned char c;

    while (rx_available()) {
        c = rx_getchar();

        if (slip_escaped) {
            slip_escaped = 0;
            if (c == SLIP_ESC_END) {
                c = SLIP_END;
            } else if (c == SLIP_ESC_ESC) {
                c = SLIP_ESC;
            }
            if (pkt_len < PKT_BUF_SIZE) {
                pkt_buf[pkt_len++] = c;
            }
        } else if (c == SLIP_END) {
            if (pkt_len > 0) {
                return 1;
            }
        } else if (c == SLIP_ESC) {
            slip_escaped = 1;
        } else {
            if (pkt_len < PKT_BUF_SIZE) {
                pkt_buf[pkt_len++] = c;
            }
        }
    }
    return 0;
}

void slip_send(unsigned char *data, unsigned char len) {
    unsigned char i, c;
    tx_putchar(SLIP_END);
    for (i = 0; i < len; i++) {
        c = data[i];
        if (c == SLIP_END) {
            tx_putchar(SLIP_ESC);
            tx_putchar(SLIP_ESC_END);
        } else if (c == SLIP_ESC) {
            tx_putchar(SLIP_ESC);
            tx_putchar(SLIP_ESC_ESC);
        } else {
            tx_putchar(c);
        }
    }
    tx_putchar(SLIP_END);
}

/*============================================================================
 * Helper Functions
 *============================================================================*/

unsigned short checksum(unsigned char *data, unsigned short len) {
    unsigned long sum = 0;
    unsigned short i;

    for (i = 0; i + 1 < len; i += 2) {
        sum += ((unsigned short)data[i] << 8) | data[i + 1];
    }
    if (len & 1) {
        sum += (unsigned short)data[len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (unsigned short)(~sum);
}

unsigned long get_u32(unsigned char *p) {
    return ((unsigned long)p[0] << 24) |
           ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) |
           (unsigned long)p[3];
}

void put_u32(unsigned char *p, unsigned long val) {
    p[0] = (unsigned char)(val >> 24);
    p[1] = (unsigned char)(val >> 16);
    p[2] = (unsigned char)(val >> 8);
    p[3] = (unsigned char)val;
}

unsigned short get_u16(unsigned char *p) {
    return ((unsigned short)p[0] << 8) | p[1];
}

void put_u16(unsigned char *p, unsigned short val) {
    p[0] = (unsigned char)(val >> 8);
    p[1] = (unsigned char)val;
}

void print_ip(unsigned long ip) {
    printf("%u.%u.%u.%u",
           (unsigned char)(ip >> 24),
           (unsigned char)(ip >> 16),
           (unsigned char)(ip >> 8),
           (unsigned char)ip);
}

unsigned long parse_ip(char *s) {
    unsigned long ip = 0;
    unsigned char octet = 0;
    char c;

    while ((c = *s++) != '\0') {
        if (c >= '0' && c <= '9') {
            octet = octet * 10 + (c - '0');
        } else if (c == '.') {
            ip = (ip << 8) | octet;
            octet = 0;
        }
    }
    ip = (ip << 8) | octet;
    return ip;
}

/*============================================================================
 * IP Layer
 *============================================================================*/

#define IP_VER_IHL   0
#define IP_TOS       1
#define IP_TOTAL_LEN 2
#define IP_ID        4
#define IP_FRAG      6
#define IP_TTL       8
#define IP_PROTO     9
#define IP_CHECKSUM  10
#define IP_SRC_IP    12
#define IP_DST_IP    16

/* Forward declarations */
void icmp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip);
void tcp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip);
void udp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip);

void ip_receive(unsigned char *pkt, unsigned short len) {
    unsigned char ver_ihl, ihl, protocol;
    unsigned short total_len, header_checksum, calc_checksum;
    unsigned long src_ip, dst_ip;

    if (len < IP_HEADER_LEN) return;

    ver_ihl = pkt[IP_VER_IHL];
    if ((ver_ihl >> 4) != 4) return;

    ihl = (ver_ihl & 0x0F) * 4;
    if (ihl < IP_HEADER_LEN || ihl > len) return;

    total_len = get_u16(&pkt[IP_TOTAL_LEN]);
    if (total_len > len) return;

    header_checksum = get_u16(&pkt[IP_CHECKSUM]);
    put_u16(&pkt[IP_CHECKSUM], 0);
    calc_checksum = checksum(pkt, ihl);
    put_u16(&pkt[IP_CHECKSUM], header_checksum);

    if (calc_checksum != header_checksum) return;

    src_ip = get_u32(&pkt[IP_SRC_IP]);
    dst_ip = get_u32(&pkt[IP_DST_IP]);

    if (dst_ip != LOCAL_IP) return;

    protocol = pkt[IP_PROTO];

    if (protocol == IP_PROTO_ICMP) {
        icmp_receive(&pkt[ihl], total_len - ihl, src_ip);
    } else if (protocol == IP_PROTO_TCP) {
        tcp_receive(&pkt[ihl], total_len - ihl, src_ip);
    } else if (protocol == IP_PROTO_UDP) {
        udp_receive(&pkt[ihl], total_len - ihl, src_ip);
    }
}

unsigned short ip_id = 1;
unsigned char tx_buf[PKT_BUF_SIZE];

void ip_send(unsigned long dst_ip, unsigned char protocol,
             unsigned char *payload, unsigned char payload_len) {
    unsigned short total_len;
    unsigned short cksum;

    total_len = IP_HEADER_LEN + payload_len;

    tx_buf[IP_VER_IHL] = 0x45;
    tx_buf[IP_TOS] = 0;
    put_u16(&tx_buf[IP_TOTAL_LEN], total_len);
    put_u16(&tx_buf[IP_ID], ip_id++);
    put_u16(&tx_buf[IP_FRAG], 0);
    tx_buf[IP_TTL] = 64;
    tx_buf[IP_PROTO] = protocol;
    put_u16(&tx_buf[IP_CHECKSUM], 0);
    put_u32(&tx_buf[IP_SRC_IP], LOCAL_IP);
    put_u32(&tx_buf[IP_DST_IP], dst_ip);

    cksum = checksum(tx_buf, IP_HEADER_LEN);
    put_u16(&tx_buf[IP_CHECKSUM], cksum);

    memcpy(&tx_buf[IP_HEADER_LEN], payload, payload_len);
    slip_send(tx_buf, (unsigned char)total_len);
}

/*============================================================================
 * ICMP Layer
 *============================================================================*/

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8
#define ICMP_TYPE     0
#define ICMP_CODE     1
#define ICMP_CHECKSUM 2
#define ICMP_ID       4
#define ICMP_SEQ      6
#define ICMP_HEADER_LEN 8

unsigned short ping_replied = 0;

void icmp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip) {
    unsigned char type;
    unsigned short cksum, calc_cksum;
    unsigned short id, seq;
    unsigned char ip_bytes[4];

    if (len < ICMP_HEADER_LEN) return;

    cksum = get_u16(&pkt[ICMP_CHECKSUM]);
    put_u16(&pkt[ICMP_CHECKSUM], 0);
    calc_cksum = checksum(pkt, len);
    put_u16(&pkt[ICMP_CHECKSUM], cksum);

    if (calc_cksum != cksum) return;

    type = pkt[ICMP_TYPE];
    id = get_u16(&pkt[ICMP_ID]);
    seq = get_u16(&pkt[ICMP_SEQ]);

    ip_bytes[0] = (unsigned char)(src_ip >> 24);
    ip_bytes[1] = (unsigned char)(src_ip >> 16);
    ip_bytes[2] = (unsigned char)(src_ip >> 8);
    ip_bytes[3] = (unsigned char)src_ip;

    if (type == ICMP_ECHO_REQUEST) {
        printf("Ping from %u.%u.%u.%u seq=%u\n",
               ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3], seq);

        pkt[ICMP_TYPE] = ICMP_ECHO_REPLY;
        pkt[ICMP_CODE] = 0;
        put_u16(&pkt[ICMP_CHECKSUM], 0);
        cksum = checksum(pkt, len);
        put_u16(&pkt[ICMP_CHECKSUM], cksum);

        ip_send(src_ip, IP_PROTO_ICMP, pkt, len);
        ping_replied++;
    }

    (void)id;
}

/*============================================================================
 * UDP Layer
 *============================================================================*/

#define UDP_SRC_PORT   0
#define UDP_DST_PORT   2
#define UDP_LENGTH     4
#define UDP_CHECKSUM   6
#define UDP_HEADER_LEN 8

void udp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip) {
    (void)pkt;
    (void)len;
    (void)src_ip;
    /* UDP not used in current apps - stub for future use */
}

/*============================================================================
 * TCP Layer
 *============================================================================*/

unsigned char tcp_state = TCP_STATE_CLOSED;
unsigned long tcp_remote_ip = 0;
unsigned short tcp_local_port = 0;
unsigned short tcp_remote_port = 0;
unsigned long tcp_seq_num = 0;
unsigned long tcp_ack_num = 0;
unsigned long tcp_last_ack = 0;

unsigned char tcp_buf[TCP_HEADER_LEN + 64];
unsigned char pseudo_hdr[12];

/* Retransmission support */
#define RETX_BUF_SIZE 64
#define RETX_TIMEOUT  2   /* seconds */
#define RETX_MAX_ATTEMPTS 3

unsigned char retx_buf[RETX_BUF_SIZE];  /* Buffer for unACKed data */
unsigned char retx_len = 0;              /* Length of data in buffer */
unsigned long retx_seq = 0;              /* Sequence number of buffered data */
unsigned long retx_time = 0;             /* Tick count when sent */
unsigned char retx_attempts = 0;         /* Retry counter */

/* Forward declaration */
unsigned long get_tick_count(void);

/* Connection queue for pending SYNs */
#define CONN_QUEUE_SIZE 16
#define CONN_QUEUE_TIMEOUT 10  /* seconds - expire old entries */

struct pending_conn {
    unsigned long  remote_ip;
    unsigned short remote_port;
    unsigned long  their_seq;    /* Their initial sequence number */
    unsigned long  timestamp;    /* When SYN was received */
    unsigned char  valid;        /* Entry in use */
};

struct pending_conn conn_queue[CONN_QUEUE_SIZE];
unsigned char conn_queue_count = 0;

/* Add connection to queue */
void conn_queue_add(unsigned long ip, unsigned short port, unsigned long seq) {
    unsigned char i;
    unsigned long now = get_tick_count();

    /* Find empty slot or oldest entry */
    for (i = 0; i < CONN_QUEUE_SIZE; i++) {
        if (!conn_queue[i].valid) {
            conn_queue[i].remote_ip = ip;
            conn_queue[i].remote_port = port;
            conn_queue[i].their_seq = seq;
            conn_queue[i].timestamp = now;
            conn_queue[i].valid = 1;
            conn_queue_count++;
            return;
        }
    }
    /* Queue full - could replace oldest, but just drop for now */
}

/* Get next connection from queue, return 1 if found */
unsigned char conn_queue_pop(unsigned long *ip, unsigned short *port, unsigned long *seq) {
    unsigned char i;
    unsigned long now = get_tick_count();

    for (i = 0; i < CONN_QUEUE_SIZE; i++) {
        if (conn_queue[i].valid) {
            /* Check if expired */
            if ((now - conn_queue[i].timestamp) > CONN_QUEUE_TIMEOUT) {
                conn_queue[i].valid = 0;
                conn_queue_count--;
                continue;
            }
            /* Found valid entry */
            *ip = conn_queue[i].remote_ip;
            *port = conn_queue[i].remote_port;
            *seq = conn_queue[i].their_seq;
            conn_queue[i].valid = 0;
            conn_queue_count--;
            return 1;
        }
    }
    return 0;
}

/* Process next pending connection from queue */
void tcp_process_queue(void) {
    unsigned long ip;
    unsigned short port;
    unsigned long seq;

    if (tcp_state != TCP_STATE_LISTEN) return;

    if (conn_queue_pop(&ip, &port, &seq)) {
        printf("[Dequeue: %u remaining]\n", conn_queue_count);
        if (app_tcp_accept(ip, port)) {
            tcp_remote_ip = ip;
            tcp_remote_port = port;
            tcp_seq_num = 1000;
            tcp_ack_num = seq + 1;
            tcp_send_flags(TCP_SYN | TCP_ACK, 0, 0);
            tcp_state = TCP_STATE_SYN_RECEIVED;
            app_tcp_state_changed(TCP_STATE_LISTEN, tcp_state, ip, port);
        }
    }
}

/* Read BIOS tick counter from 0040:006C */
unsigned long get_tick_count(void) {
    unsigned long ticks = 0;
    __asm {
        push es
        mov ax, 0x0040
        mov es, ax
        mov bx, 0x006C
        mov ax, es:[bx]
        mov dx, es:[bx+2]
        mov word ptr ticks, ax
        mov word ptr ticks+2, dx
        pop es
    }
    return ticks;
}

unsigned short tcp_checksum(unsigned char *tcp_pkt, unsigned short tcp_len,
                            unsigned long src_ip, unsigned long dst_ip) {
    unsigned long sum = 0;
    unsigned short i;

    put_u32(&pseudo_hdr[0], src_ip);
    put_u32(&pseudo_hdr[4], dst_ip);
    pseudo_hdr[8] = 0;
    pseudo_hdr[9] = IP_PROTO_TCP;
    put_u16(&pseudo_hdr[10], tcp_len);

    for (i = 0; i < 12; i += 2) {
        sum += ((unsigned short)pseudo_hdr[i] << 8) | pseudo_hdr[i + 1];
    }
    for (i = 0; i + 1 < tcp_len; i += 2) {
        sum += ((unsigned short)tcp_pkt[i] << 8) | tcp_pkt[i + 1];
    }
    if (tcp_len & 1) {
        sum += (unsigned short)tcp_pkt[tcp_len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (unsigned short)(~sum);
}

void tcp_send_flags(unsigned char flags, unsigned char *data, unsigned char data_len) {
    unsigned short tcp_len;
    unsigned short cksum;

    tcp_len = TCP_HEADER_LEN + data_len;

    put_u16(&tcp_buf[TCP_SRC_PORT], tcp_local_port);
    put_u16(&tcp_buf[TCP_DST_PORT], tcp_remote_port);
    put_u32(&tcp_buf[TCP_SEQ_OFF], tcp_seq_num);
    put_u32(&tcp_buf[TCP_ACK_OFF], tcp_ack_num);
    tcp_buf[TCP_DATA_OFF] = 0x50;
    tcp_buf[TCP_FLAGS] = flags;
    put_u16(&tcp_buf[TCP_WINDOW], 2048);
    put_u16(&tcp_buf[TCP_CHECKSUM], 0);
    put_u16(&tcp_buf[TCP_URGENT], 0);

    if (data_len > 0) {
        memcpy(&tcp_buf[TCP_HEADER_LEN], data, data_len);
    }

    cksum = tcp_checksum(tcp_buf, tcp_len, LOCAL_IP, tcp_remote_ip);
    put_u16(&tcp_buf[TCP_CHECKSUM], cksum);

    if (flags & TCP_SYN) tcp_seq_num++;
    if (flags & TCP_FIN) tcp_seq_num++;
    tcp_seq_num += data_len;

    ip_send(tcp_remote_ip, IP_PROTO_TCP, tcp_buf, (unsigned char)tcp_len);
}

void tcp_send(unsigned char *data, unsigned char len) {
    if (tcp_state != TCP_STATE_ESTABLISHED) {
        return;
    }

    /* Wait for previous data to be ACKed before sending more */
    /* This implements simple stop-and-wait for reliability */
    if (retx_len > 0) {
        /* Previous send still pending - caller should retry later */
        /* For now, we'll just send anyway (best effort) */
    }

    /* Save data for potential retransmission */
    if (len <= RETX_BUF_SIZE) {
        memcpy(retx_buf, data, len);
        retx_len = len;
        retx_seq = tcp_seq_num;  /* Sequence before sending */
        retx_time = get_tick_count();
        retx_attempts = 0;
    }

    tcp_send_flags(TCP_PSH | TCP_ACK, data, len);
}

void tcp_close(void) {
    if (tcp_state == TCP_STATE_ESTABLISHED) {
        tcp_state = TCP_STATE_FIN_WAIT_1;
        tcp_send_flags(TCP_FIN | TCP_ACK, 0, 0);
    }
    retx_len = 0;  /* Clear retransmit buffer */
}

/* Check for retransmission timeout - call from main loop */
void tcp_check_retransmit(void) {
    unsigned long now;
    unsigned long saved_seq;

    if (tcp_state != TCP_STATE_ESTABLISHED || retx_len == 0) {
        return;
    }

    now = get_tick_count();

    /* Check if timeout exceeded (handle tick wraparound) */
    if ((now - retx_time) >= RETX_TIMEOUT) {
        retx_attempts++;

        if (retx_attempts > RETX_MAX_ATTEMPTS) {
            /* Give up - connection probably dead */
            printf("[Retransmit failed]\n");
            retx_len = 0;
            return;
        }

        printf("[Retransmit #%u]\n", retx_attempts);

        /* Rewind sequence number and resend */
        saved_seq = tcp_seq_num;
        tcp_seq_num = retx_seq;
        tcp_send_flags(TCP_PSH | TCP_ACK, retx_buf, retx_len);
        /* tcp_send_flags already advances tcp_seq_num */

        retx_time = now;  /* Reset timeout */
    }
}

void tcp_receive(unsigned char *pkt, unsigned short len, unsigned long src_ip) {
    unsigned short src_port, dst_port;
    unsigned long seq_num, ack_num;
    unsigned char data_off, flags, hdr_len;
    unsigned short data_len;
    unsigned char old_state;

    if (len < TCP_HEADER_LEN) return;

    src_port = get_u16(&pkt[TCP_SRC_PORT]);
    dst_port = get_u16(&pkt[TCP_DST_PORT]);
    seq_num = get_u32(&pkt[TCP_SEQ_OFF]);
    ack_num = get_u32(&pkt[TCP_ACK_OFF]);
    data_off = pkt[TCP_DATA_OFF];
    flags = pkt[TCP_FLAGS];

    hdr_len = (data_off >> 4) * 4;
    if (hdr_len < TCP_HEADER_LEN || hdr_len > len) return;

    data_len = len - hdr_len;

    if (dst_port != tcp_local_port) return;

    /* Queue SYNs if we're busy (not in LISTEN state) */
    if ((flags & TCP_SYN) && !(flags & TCP_ACK) && tcp_state != TCP_STATE_LISTEN) {
        conn_queue_add(src_ip, src_port, seq_num);
        printf("[Queued: %u pending]\n", conn_queue_count);
        return;
    }

    /* Handle RST */
    if (flags & TCP_RST) {
        if (tcp_state != TCP_STATE_CLOSED && tcp_state != TCP_STATE_LISTEN) {
            old_state = tcp_state;
            tcp_state = TCP_STATE_LISTEN;
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
            tcp_process_queue();
        }
        return;
    }

    old_state = tcp_state;

    switch (tcp_state) {
    case TCP_STATE_LISTEN:
        if (flags & TCP_SYN) {
            if (app_tcp_accept(src_ip, src_port)) {
                tcp_remote_ip = src_ip;
                tcp_remote_port = src_port;
                tcp_seq_num = 1000;
                tcp_ack_num = seq_num + 1;
                tcp_send_flags(TCP_SYN | TCP_ACK, 0, 0);
                tcp_state = TCP_STATE_SYN_RECEIVED;
                app_tcp_state_changed(old_state, tcp_state, src_ip, src_port);
            }
        }
        break;

    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            tcp_ack_num = seq_num + 1;
            tcp_last_ack = ack_num;
            tcp_state = TCP_STATE_ESTABLISHED;
            tcp_send_flags(TCP_ACK, 0, 0);
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
        }
        break;

    case TCP_STATE_SYN_RECEIVED:
        if (flags & TCP_ACK) {
            tcp_last_ack = ack_num;
            tcp_state = TCP_STATE_ESTABLISHED;
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
        }
        break;

    case TCP_STATE_ESTABLISHED:
        if (flags & TCP_ACK) {
            tcp_last_ack = ack_num;
            /* Check if this ACK covers our retransmit buffer */
            if (retx_len > 0 && ack_num >= retx_seq + retx_len) {
                retx_len = 0;  /* Data acknowledged, clear buffer */
            }
        }
        if (data_len > 0) {
            tcp_ack_num = seq_num + data_len;
            tcp_send_flags(TCP_ACK, 0, 0);
            app_tcp_data_received(&pkt[hdr_len], data_len);
        }
        if (flags & TCP_FIN) {
            tcp_ack_num = seq_num + data_len + 1;
            tcp_send_flags(TCP_FIN | TCP_ACK, 0, 0);
            tcp_state = TCP_STATE_LISTEN;
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
            tcp_process_queue();
        }
        break;

    case TCP_STATE_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            tcp_last_ack = ack_num;
            tcp_state = TCP_STATE_FIN_WAIT_2;
        }
        if (flags & TCP_FIN) {
            tcp_ack_num = seq_num + 1;
            tcp_send_flags(TCP_ACK, 0, 0);
            tcp_state = TCP_STATE_LISTEN;
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
            tcp_process_queue();
        }
        break;

    case TCP_STATE_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            tcp_ack_num = seq_num + 1;
            tcp_send_flags(TCP_ACK, 0, 0);
            tcp_state = TCP_STATE_LISTEN;
            app_tcp_state_changed(old_state, tcp_state, tcp_remote_ip, tcp_remote_port);
            tcp_process_queue();
        }
        break;
    }
}

/* Connect to remote host (client mode) */
void tcp_connect(unsigned long remote_ip, unsigned short remote_port) {
    tcp_remote_ip = remote_ip;
    tcp_remote_port = remote_port;
    tcp_seq_num = 1000;
    tcp_ack_num = 0;
    tcp_state = TCP_STATE_SYN_SENT;
    tcp_send_flags(TCP_SYN, 0, 0);
}

/* Start listening (server mode) */
void tcp_listen(unsigned short port) {
    tcp_local_port = port;
    tcp_state = TCP_STATE_LISTEN;
}
