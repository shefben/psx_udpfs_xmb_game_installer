#include "../../src/dhcp_proto.h"
#include "test.h"

static const uint8_t MAC[6] = {0x00, 0x15, 0xC1, 0x12, 0x34, 0x56};

/* Turn a request into the server's reply of `type` offering `ip`. */
static size_t reply(uint8_t *m, int type, uint32_t xid, uint32_t ip, uint32_t sid) {
  dhcp_build(m, DHCP_DISCOVER, xid, MAC, 0, 0);
  m[0] = 2;
  m[16] = (uint8_t)(ip >> 24), m[17] = (uint8_t)(ip >> 16), m[18] = (uint8_t)(ip >> 8),
  m[19] = (uint8_t)ip;
  uint8_t *o = m + 240;
  *o++ = 53, *o++ = 1, *o++ = (uint8_t)type;
  *o++ = 1, *o++ = 4, *o++ = 255, *o++ = 255, *o++ = 255, *o++ = 0;
  *o++ = 54, *o++ = 4, *o++ = (uint8_t)(sid >> 24), *o++ = (uint8_t)(sid >> 16),
  *o++ = (uint8_t)(sid >> 8), *o++ = (uint8_t)sid;
  *o++ = 255;
  return (size_t)(o - m);
}

TEST(dhcp_discover_and_request_layout) {
  uint8_t m[DHCP_MSG_MAX];
  size_t n = dhcp_build(m, DHCP_DISCOVER, 0xA1B2C3D4, MAC, 0, 0);
  CHECK(n >= 300);
  CHECK(m[0] == 1 && m[1] == 1 && m[2] == 6);
  CHECK(m[4] == 0xA1 && m[7] == 0xD4);
  CHECK(m[10] == 0x80); /* broadcast reply */
  CHECK(memcmp(m + 28, MAC, 6) == 0);
  CHECK(m[236] == 0x63 && m[239] == 0x63);
  CHECK(m[240] == 53 && m[242] == DHCP_DISCOVER);
  n = dhcp_build(m, DHCP_REQUEST, 1, MAC, 0xC0A8000Au, 0xC0A80001u);
  int has50 = 0, has54 = 0;
  for (size_t i = 240; i < n && m[i] != 255; i += 2 + m[i + 1]) {
    has50 |= m[i] == 50 && m[i + 2] == 192 && m[i + 5] == 10;
    has54 |= m[i] == 54 && m[i + 5] == 1;
  }
  CHECK(has50 && has54);
  CHECK(m[242] == DHCP_REQUEST);
}

TEST(dhcp_parse_offer_ack_nak) {
  uint8_t m[DHCP_MSG_MAX];
  dhcp_reply_t r;
  size_t n = reply(m, DHCP_OFFER, 7, 0xC0A8000Au, 0xC0A80001u);
  CHECK_EQ_INT(dhcp_parse(m, n, 7, MAC, &r), 0);
  CHECK_EQ_INT(r.type, DHCP_OFFER);
  CHECK_EQ_U64(r.yiaddr, 0xC0A8000Au);
  CHECK_EQ_U64(r.server_id, 0xC0A80001u);
  n = reply(m, DHCP_ACK, 7, 0xC0A8000Au, 0xC0A80001u);
  CHECK_EQ_INT(dhcp_parse(m, n, 7, MAC, &r), 0);
  CHECK_EQ_INT(r.type, DHCP_ACK);
  n = reply(m, DHCP_NAK, 7, 0, 0xC0A80001u);
  CHECK_EQ_INT(dhcp_parse(m, n, 7, MAC, &r), 0);
  CHECK_EQ_INT(r.type, DHCP_NAK);
}

TEST(dhcp_parse_rejects_other_clients_and_junk) {
  uint8_t m[DHCP_MSG_MAX];
  dhcp_reply_t r;
  size_t n = reply(m, DHCP_OFFER, 7, 0xC0A8000Au, 1);
  CHECK(dhcp_parse(m, n, 8, MAC, &r) < 0); /* other transaction */
  uint8_t other[6] = {1, 2, 3, 4, 5, 6};
  CHECK(dhcp_parse(m, n, 7, other, &r) < 0); /* other client */
  CHECK(dhcp_parse(m, 200, 7, MAC, &r) < 0); /* truncated */
  m[241] = 200;                              /* option runs past the end */
  CHECK(dhcp_parse(m, n, 7, MAC, &r) < 0);
  n = dhcp_build(m, DHCP_DISCOVER, 7, MAC, 0, 0); /* a request, not a reply */
  CHECK(dhcp_parse(m, n, 7, MAC, &r) < 0);
}
