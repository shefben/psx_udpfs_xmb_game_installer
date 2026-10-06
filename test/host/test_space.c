#include "../../src/space.h"
#include "test.h"

#define GIB_SECT (2u * 1024u * 1024u) /* 512-byte sectors per GiB */

/* A DESR APA list as dread returns it: main partitions and sub-partitions
 * (same name, attr 1), free entries (type 0). */
static const space_part_t PARTS[] = {
    {"__mbr", 0x0001, 0, 0, 262144},
    {"__net", 0x0100, 0, 262144, 262144},
    {"__system", 0x0100, 0, 524288, 524288},
    {"__sysconf", 0x0100, 0, 1048576, 1048576},
    {"__common", 0x0100, 0, 2097152, 2097152},
    {"PP.SLUS-20066..HALF_LIFE", 0x1337, 0, 4194304, 262144},      /* 128 MiB game */
    {"+OPL", 0x0100, 0, 4456448, 262144},                          /* 128 MiB data */
    {"PP.SLUS-20380..JURASSIC_PARK___", 0x1337, 0, 4718592, 4 * GIB_SECT / 4},
    {"PP.SLUS-20380..JURASSIC_PARK___", 0x1337, 1, 6815744, GIB_SECT}, /* its sub */
    {"__.SLES-50330..OLD_BUILD", 0x1337, 0, 8912896, 2 * GIB_SECT},
    {"PP.SLES-50330..OLD_BUILD", 0x0100, 0, 13107200, 262144},     /* its cover */
    {"PP.UDPF-00001..INSTALLER", 0x0100, 0, 13369344, 262144},     /* data, no game */
    {"", 0x0000, 0, 13631488, 262144},                             /* free */
};
#define NPARTS ((int)(sizeof(PARTS) / sizeof(PARTS[0])))

TEST(space_tally_counts_games_data_and_system) {
  space_usage_t u;
  space_tally(PARTS, NPARTS, &u);
  /* system: 128+128+256+512+1024 MiB */
  CHECK_EQ_U64(u.system_mb, 2048);
  /* games: 128 + (1024 + 1024) + 2048 + 128 cover = 4352 MiB */
  CHECK_EQ_U64(u.games_mb, 128 + 2048 + 2048 + 128);
  /* data: games + +OPL 128 + installer 128 */
  CHECK_EQ_U64(u.data_mb, u.games_mb + 256);
  CHECK_EQ_U64(u.limit_left_mb, SPACE_LIMIT_MB - u.data_mb);
  CHECK_EQ_INT(u.beyond_limit, 0);
}

TEST(space_limit_is_exactly_128_gib) {
  CHECK_EQ_U64(SPACE_LIMIT_MB, 131072);
  CHECK_EQ_U64(SPACE_LIMIT_SECTORS, 0x10000000ull);
}

TEST(space_check_refuses_over_the_limit) {
  space_usage_t u;
  space_tally(PARTS, NPARTS, &u);
  CHECK_EQ_INT(space_check(&u, 1024, 100000), ERR_OK);
  /* more than the HDD has free */
  CHECK_EQ_INT(space_check(&u, 2048, 1024), ERR_NO_SPACE);
  /* exactly up to the limit is allowed, one MiB more is not */
  CHECK_EQ_INT(space_check(&u, (uint32_t)u.limit_left_mb, 200000), ERR_OK);
  CHECK_EQ_INT(space_check(&u, (uint32_t)u.limit_left_mb + 1, 200000), ERR_DATA_LIMIT);
  /* usable free space for planning is min(HDD free, what the limit leaves) */
  CHECK_EQ_U64(space_usable_mb(&u, 500), 500);
  CHECK_EQ_U64(space_usable_mb(&u, 200000), u.limit_left_mb);
}

TEST(space_full_quota_leaves_nothing) {
  /* 128 GiB of games already: nothing more, even with free HDD space. */
  space_part_t p[2] = {{"__.SLUS-20312..A", 0x1337, 0, 4194304, 64u * GIB_SECT},
                       {"__.SLUS-20313..B", 0x1337, 0, 4194304 + 64u * GIB_SECT, 64u * GIB_SECT}};
  space_usage_t u;
  space_tally(p, 2, &u);
  CHECK_EQ_U64(u.data_mb, SPACE_LIMIT_MB);
  CHECK_EQ_U64(u.limit_left_mb, 0);
  CHECK_EQ_INT(space_check(&u, 1, 1000000), ERR_DATA_LIMIT);
  CHECK_EQ_U64(space_usable_mb(&u, 1000000), 0);
}

TEST(space_position_beyond_128_gib) {
  /* A segment ending exactly at 128 GiB is fine; one sector more is not. */
  CHECK_EQ_INT(space_segment_beyond(SPACE_LIMIT_SECTORS - 262144, 262144), 0);
  CHECK_EQ_INT(space_segment_beyond(SPACE_LIMIT_SECTORS - 262144, 262145), 1);
  CHECK_EQ_INT(space_segment_beyond(0xF0000000u, 262144), 1);
  /* no 32-bit wrap: start near 2^32 */
  CHECK_EQ_INT(space_segment_beyond(0xFFFFFF00u, 0x200), 1);
  space_part_t p[1] = {{"__.SLUS-20312..A", 0x1337, 1, SPACE_LIMIT_SECTORS, 262144}};
  space_usage_t u;
  space_tally(p, 1, &u);
  CHECK_EQ_INT(u.beyond_limit, 1);
  CHECK_EQ_INT(space_name_beyond(p, 1, "__.SLUS-20312..A"), 1);
  CHECK_EQ_INT(space_name_beyond(p, 1, "__.SLUS-20313..B"), 0);
}

TEST(space_summary_line) {
  space_usage_t u;
  space_tally(PARTS, NPARTS, &u);
  char line[96];
  space_format(&u, 5 * 1024, line, sizeof(line));
  CHECK_STR(line, "Games 4.3 GiB, games+data 4.5 of 128 GiB, HDD free 5.0 GiB");
}
