#include "test.h"
#include "console.h"
#include "settings.h"

TEST(console_model_ids) {
  for (uint32_t id = 0x20d381; id <= 0x20d389; id++)
    CHECK_EQ_INT(console_from_model_id(id), id <= 0x20d385 ? CONSOLE_PSX1 : CONSOLE_PSX2);
  CHECK_EQ_INT(console_from_model_id(0), CONSOLE_UNKNOWN);
  CHECK_EQ_INT(console_from_model_id(0x20d380), CONSOLE_UNKNOWN);
  CHECK_EQ_INT(console_from_model_id(0x20d38a), CONSOLE_UNKNOWN);
}

TEST(console_settings_fail_closed_and_roundtrip) {
  net_settings_t s, t;
  char buf[128];
  settings_parse(NULL, &s);
  CHECK_EQ_INT(s.console, CONSOLE_UNKNOWN);
  settings_parse("local_ip=10.0.0.7\nconsole=psx1\n", &s);
  CHECK_EQ_INT(s.console, CONSOLE_PSX1);
  CHECK(settings_serialize(&s, buf, sizeof(buf)) > 0);
  settings_parse(buf, &t);
  CHECK_EQ_INT(t.console, CONSOLE_PSX1);
  settings_parse("console=psx2\nip_mode=dhcp\n", &s);
  CHECK_EQ_INT(s.console, CONSOLE_PSX2);
  settings_parse("console=bogus\nip_mode=dhcp\n", &s);
  CHECK_EQ_INT(s.console, CONSOLE_UNKNOWN);
  CHECK_EQ_INT(s.warning, 1);
}
