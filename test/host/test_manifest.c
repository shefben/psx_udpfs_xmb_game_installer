#include <stdlib.h>

#include "../../src/manifest.h"
#include "test.h"

static manifest_t m;

static const char GOOD[] =
    "udpfsd-manifest 1 launcher=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:1561728 auto=1\n"
    "/DVD/GT4.iso\tok\tSLUS_203.12\tGran Turismo 4\t8547991552\tDVD\t1234\tjkt/SLUS_203.12.png\t/CFG/SLUS_203.12.cfg\n"
    "/CD/a.zso.iso\tok\tSLUS_210.90\tAlien Hominid\t2048000\tCD\t0\t-\t-\textra-future-column\r\n"
    "/DVD/broken.iso\tinvalid:not an ISO9660 image\t-\t-\t81920\t-\t0\t-\t-\n";

TEST(manifest_parses_header_and_entries) {
  CHECK_EQ_INT(manifest_parse(GOOD, strlen(GOOD), &m), 0);
  CHECK_EQ_INT(m.version, 1);
  CHECK(m.has_launcher);
  CHECK_EQ_INT(m.launcher_size, 1561728);
  CHECK_EQ_INT(strlen(m.launcher_sha), 64);
  CHECK(m.auto_install);
  CHECK_EQ_INT(m.n, 3);
  CHECK_EQ_INT(m.n_bad, 0);
  CHECK_STR(m.e[0].path, "/DVD/GT4.iso");
  CHECK(m.e[0].ok && m.e[0].dvd);
  CHECK_STR(m.e[0].id, "SLUS_203.12");
  CHECK_STR(m.e[0].title, "Gran Turismo 4");
  CHECK_EQ_U64(m.e[0].bytes, 8547991552ull);
  CHECK_EQ_INT(m.e[0].layer1, 1234);
  CHECK_STR(m.e[0].jacket, "jkt/SLUS_203.12.png");
  CHECK_STR(m.e[0].cfg, "/CFG/SLUS_203.12.cfg");
  CHECK(!m.e[1].dvd);
  CHECK_STR(m.e[1].jacket, "");
  CHECK_STR(m.e[1].cfg, "");
  CHECK(!m.e[2].ok);
  CHECK_STR(m.e[2].reason, "not an ISO9660 image");
}

TEST(manifest_header_variants) {
  const char *t = "udpfsd-manifest 1 auto=0\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(!m.has_launcher && !m.auto_install && m.n == 0);
  t = "udpfsd-manifest 2 auto=1\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), -1);
  t = "garbage\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), -1);
  t = "udpfsd-manifest 1 launcher=xyz:12 auto=1\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(!m.has_launcher); /* malformed hash: ignored, never trusted */
  CHECK_EQ_INT(manifest_parse("", 0, &m), -1);
}

TEST(manifest_rejects_bad_lines) {
  const char *t =
      "udpfsd-manifest 1 auto=0\n"
      "/DVD/a.iso\tok\tSLUS_203.12\tA\n"                                      /* too few */
      "relative.iso\tok\tSLUS_203.12\tA\t1\tDVD\t0\t-\t-\n"                   /* no '/' */
      "/DVD/b.iso\tok\tBADID\tB\t1\tDVD\t0\t-\t-\n"                           /* id */
      "/DVD/c.iso\tok\tSLUS_203.12\tC\t12x\tDVD\t0\t-\t-\n"                   /* bytes */
      "/DVD/d.iso\tok\tSLUS_203.12\tD\t1\tBD\t0\t-\t-\n"                      /* disc */
      "/DVD/e.iso\tok\tSLUS_203.12\tE\t1\tDVD\t0\t../x.png\t-\n"              /* jacket escape */
      "/DVD/f.iso\tok\tSLUS_203.12\tF\t2048\tDVD\t0\t-\t-\n";                 /* good */
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK_EQ_INT(m.n, 1);
  CHECK_EQ_INT(m.n_bad, 6);
  CHECK_STR(m.e[0].path, "/DVD/f.iso");
}

TEST(manifest_long_title_truncated_safely) {
  char t[600];
  snprintf(t, sizeof(t), "udpfsd-manifest 1 auto=0\n/DVD/a.iso\tok\tSLUS_203.12\t%0100d\t2048\tDVD\t0\t-\t-\n", 0);
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK_EQ_INT(m.n, 1);
  CHECK_EQ_INT(strlen(m.e[0].title), 63);
}

TEST(manifest_lookups) {
  manifest_parse(GOOD, strlen(GOOD), &m);
  CHECK(manifest_find_id(&m, "SLUS_210.90") == &m.e[1]);
  CHECK(manifest_find_id(&m, "SLUS_999.99") == NULL);
  CHECK(manifest_find_path(&m, "udpfs:/DVD/GT4.iso") == &m.e[0]);
  CHECK(manifest_find_path(&m, "/DVD/GT4.iso") == &m.e[0]);
  CHECK(manifest_find_path(&m, "udpfs:/DVD/broken.iso") == &m.e[2]);
  CHECK(manifest_find_path(&m, "udpfs:/nope.iso") == NULL);
}

TEST(manifest_opl_runtime_header) {
  const char *t =
      "udpfsd-manifest 1 launcher=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:1561728 "
      "opl=fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210:1360884 auto=1\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(m.has_launcher && m.has_opl && m.auto_install);
  CHECK_EQ_INT(m.opl_size, 1360884);
  CHECK_STR(m.opl_sha, "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
  t = "udpfsd-manifest 1 opl=nothex:12 auto=0\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(!m.has_opl);
}
