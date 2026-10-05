#ifndef PSXI_DHCP_PROTO_H
#define PSXI_DHCP_PROTO_H

#include <stddef.h>
#include <stdint.h>

/* Minimal DHCP client messages (RFC 2131): DISCOVER / REQUEST out, OFFER
 * / ACK / NAK in. Portable: host-tested here and compiled into the
 * patched Neutrino ministack (IOP), which runs the exchange. */

#define DHCP_MSG_MAX 548 /* BOOTP minimum datagram */
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6

typedef struct {
  int type;           /* DHCP_OFFER / DHCP_ACK / DHCP_NAK */
  uint32_t yiaddr;    /* offered / assigned address (host order) */
  uint32_t server_id; /* option 54 (host order), 0 if absent */
} dhcp_reply_t;

/* DISCOVER (req_ip/server_id 0) or REQUEST. Broadcast flag set, so the
 * server answers to 255.255.255.255 (ministack has no address yet).
 * Returns the message length. `out` holds DHCP_MSG_MAX bytes. */
size_t dhcp_build(uint8_t *out, int type, uint32_t xid, const uint8_t mac[6], uint32_t req_ip,
                  uint32_t server_id);

/* A reply for this client (op 2, xid, chaddr, magic cookie, option 53).
 * 0, or -1 if it is not one. */
int dhcp_parse(const uint8_t *msg, size_t len, uint32_t xid, const uint8_t mac[6],
               dhcp_reply_t *r);

#endif
