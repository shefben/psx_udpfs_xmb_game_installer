#include "../../src/ui_style.h"
#include "test.h"

TEST(ui_bold_glyph_rows) {
  CHECK_EQ_INT(ui_bold_row(0x00), 0x00);
  CHECK_EQ_INT(ui_bold_row(0x80), 0xC0); /* 1-px stroke becomes 2 px */
  CHECK_EQ_INT(ui_bold_row(0x7C), 0x7E);
  CHECK_EQ_INT(ui_bold_row(0x01), 0x01); /* never spills into the next cell */
}

TEST(ui_line_tones) {
  CHECK_EQ_INT(ui_line_tone("PASS HDD driver"), UI_TONE_GOOD);
  CHECK_EQ_INT(ui_line_tone("   FAIL smap.irx"), UI_TONE_BAD);
  CHECK_EQ_INT(ui_line_tone(" FAILED    Game.iso"), UI_TONE_BAD);
  CHECK_EQ_INT(ui_line_tone(" ERROR: could not mount"), UI_TONE_BAD);
  CHECK_EQ_INT(ui_line_tone("WARN no OPL-Launcher"), UI_TONE_WARN);
  CHECK_EQ_INT(ui_line_tone(" WARNING: network.ini invalid"), UI_TONE_WARN);
  CHECK_EQ_INT(ui_line_tone("installed Game.iso"), UI_TONE_GOOD);
  CHECK_EQ_INT(ui_line_tone(" Install Games from UDPFS"), UI_TONE_NORMAL);
  CHECK_EQ_INT(ui_line_tone("PASSWORD"), UI_TONE_NORMAL); /* whole words only */
  CHECK_EQ_INT(ui_line_tone(""), UI_TONE_NORMAL);
}

TEST(ui_layout_inside_tv_safe_area) {
  /* 80x28 character screen; keep a margin for TV overscan */
  CHECK(UI_MARGIN_X >= 3 && UI_MARGIN_X + UI_COLS <= 80 - 3);
  CHECK(UI_MARGIN_Y >= 1 && UI_MARGIN_Y + UI_ROWS <= 28 - 1);
}
