#ifdef _IOP
#include <sysclib.h>
#else
#include <string.h>
#endif

#include "dhcp_proto.h"

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

size_t dhcp_build(uint8_t *out, int type, uint32_t xid, const uint8_t mac[6], uint32_t req_ip,
                  uint32_t server_id) {
  memset(out, 0, DHCP_MSG_MAX);
  out[0] = 1; /* BOOTREQUEST */
  out[1] = 1; /* Ethernet */
  out[2] = 6; /* MAC length */
  put32(out + 4, xid);
  out[10] = 0x80; /* flags: broadcast */
  memcpy(out + 28, mac, 6);
  put32(out + 236, 0x63825363); /* magic cookie */
  uint8_t *o = out + 240;
  *o++ = 53, *o++ = 1, *o++ = (uint8_t)type;
  *o++ = 61, *o++ = 7, *o++ = 1; /* client id: Ethernet + MAC */
  memcpy(o, mac, 6), o += 6;
  if (req_ip) {
    *o++ = 50, *o++ = 4;
    put32(o, req_ip), o += 4;
  }
  if (server_id) {
    *o++ = 54, *o++ = 4;
    put32(o, server_id), o += 4;
  }
  *o++ = 55, *o++ = 2, *o++ = 1, *o++ = 3; /* ask for subnet mask, router */
  *o++ = 255;
  /* Some servers ignore requests shorter than a BOOTP datagram. */
  return (size_t)(o - out) < 300 ? 300 : (size_t)(o - out);
}

int dhcp_parse(const uint8_t *msg, size_t len, uint32_t xid, const uint8_t mac[6],
               dhcp_reply_t *r) {
  memset(r, 0, sizeof(*r));
  if (len < 241 || msg[0] != 2 || get32(msg + 4) != xid || memcmp(msg + 28, mac, 6) ||
      get32(msg + 236) != 0x63825363)
    return -1;
  r->yiaddr = get32(msg + 16);
  for (size_t i = 240; i < len;) {
    uint8_t code = msg[i];
    if (code == 255)
      break;
    if (code == 0) {
      i++;
      continue;
    }
    if (i + 1 >= len || i + 2 + msg[i + 1] > len)
      return -1;
    uint8_t n = msg[i + 1];
    const uint8_t *v = msg + i + 2;
    if (code == 53 && n == 1)
      r->type = v[0];
    else if (code == 54 && n == 4)
      r->server_id = get32(v);
    i += 2 + (size_t)n;
  }
  if (r->type != DHCP_OFFER && r->type != DHCP_ACK && r->type != DHCP_NAK)
    return -1;
  return 0;
}
