/* Byte-for-byte parity with hdl-dump's hdl_pname(), compiled from the
 * pinned reference/hdl-dump/hdl.c (extracted by the Makefile). */
#include "../../src/partname.h"
#include "test.h"

#ifdef HAVE_HDLDUMP_REF
void hdl_pname(const char *startup_name, const char *name,
               const char part_prefix[3], char partition_name[33]);

static const char *IDS[] = {"SLUS_203.12", "SCUS_971.99", "SLES_503.30",
                            "SCES_503.62", "SLPS_250.50", "SLPM_650.51"};
static const char *TITLES[] = {
    "Gran Turismo 4",
    "ICO",
    "Metal Gear Solid 3: Snake Eater",
    "a-b.c'd e!f",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    "0123456789abcdefghij",
    "lower case title",
    "Tab\tand~tilde",
    "",
};

TEST(partname_matches_hdldump_hdl_pname) {
  for (unsigned i = 0; i < sizeof(IDS) / sizeof(IDS[0]); i++) {
    for (unsigned j = 0; j < sizeof(TITLES) / sizeof(TITLES[0]); j++) {
      char v[33], h[33], ref_v[33], ref_h[33];
      CHECK_EQ_INT(build_game_partition_pair(IDS[i], TITLES[j], v, h), 0);
      hdl_pname(IDS[i], TITLES[j], "PP.", ref_v);
      hdl_pname(IDS[i], TITLES[j], "__.", ref_h);
      CHECK_STR(v, ref_v);
      CHECK_STR(h, ref_h);
    }
  }
}
#endif
