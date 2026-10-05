#include <stdio.h>
#include <string.h>

#include "partname.h"
#include "transaction.h"
#include "util.h"

static const char *NAMES[TX__COUNT] = {
    "TX_NONE",          "TX_PLANNED",          "TX_HDL_CREATED",
    "TX_STREAMING",     "TX_HDL_COMPLETE",     "TX_HDL_VERIFIED",
    "TX_CHANNEL_CREATED", "TX_CHANNEL_VERIFIED", "TX_COMPLETE",
    "TX_FAILED",
};

const char *tx_state_name(tx_state_t s) {
  return (unsigned)s < TX__COUNT ? NAMES[s] : "TX_INVALID";
}

int tx_state_parse(const char *name, tx_state_t *out) {
  for (int i = 0; i < TX__COUNT; i++)
    if (strcmp(name, NAMES[i]) == 0) {
      *out = (tx_state_t)i;
      return 0;
    }
  return -1;
}

int tx_transition_allowed(tx_state_t from, tx_state_t to) {
  if ((unsigned)from >= TX__COUNT || (unsigned)to >= TX__COUNT)
    return 0;
  /* Strict single-step forward progress through the happy path. */
  if (to == from + 1 && from < TX_COMPLETE && to <= TX_COMPLETE)
    return 1;
  /* Any started, unfinished transaction may fail. */
  if (to == TX_FAILED)
    return from >= TX_PLANNED && from < TX_COMPLETE;
  /* Retry from start / reinstall. */
  if (to == TX_PLANNED)
    return from == TX_FAILED || from == TX_COMPLETE;
  /* Channel (re)build on a hidden game whose data already verified:
   * fresh repair, after a failure or completion, after a data-only
   * install (OPL missing), or after power loss mid-channel. */
  if (to == TX_HDL_VERIFIED)
    return from == TX_FAILED || from == TX_COMPLETE || from == TX_HDL_VERIFIED ||
           from == TX_CHANNEL_CREATED || from == TX_CHANNEL_VERIFIED;
  return 0;
}

inst_err_t tx_advance(tx_journal_t *j, tx_state_t to) {
  if (!tx_transition_allowed(j->state, to))
    return ERR_INTERNAL;
  j->state = to;
  if (to != TX_FAILED) {
    memset(j->last_error, 0, sizeof(j->last_error));
    j->failed_from = TX_NONE;
  }
  return ERR_OK;
}

void tx_fail(tx_journal_t *j, inst_err_t err) {
  if (j->state != TX_FAILED)
    j->failed_from = j->state;
  j->state = TX_FAILED;
  str_copy(j->last_error, err_name(err), sizeof(j->last_error));
}

int tx_channel_creation_allowed(const tx_journal_t *j) {
  return j->state == TX_HDL_VERIFIED || j->state == TX_CHANNEL_CREATED ||
         j->state == TX_CHANNEL_VERIFIED;
}

int tx_hidden_data_read_back(const tx_journal_t *j) {
  return j->bytes_expected > 0 && j->bytes_verified == j->bytes_expected &&
         j->has_source_crc && j->has_installed_crc && j->source_crc32 == j->installed_crc32;
}

int tx_hidden_data_verified(const tx_journal_t *j) {
  tx_state_t s = j->state == TX_FAILED ? j->failed_from : j->state;
  return !j->deleting && s >= TX_HDL_VERIFIED && s <= TX_COMPLETE &&
         j->bytes_expected > 0 && j->bytes_written == j->bytes_expected &&
         j->has_source_crc && (j->verify_skipped || tx_hidden_data_read_back(j));
}

int tx_journal_equal(const tx_journal_t *a, const tx_journal_t *b) {
  return !strcmp(a->source_path, b->source_path) && a->source_size == b->source_size &&
         !strcmp(a->startup_id, b->startup_id) &&
         !strcmp(a->visible_partition, b->visible_partition) &&
         !strcmp(a->hidden_partition, b->hidden_partition) &&
         a->bytes_expected == b->bytes_expected && a->bytes_written == b->bytes_written &&
         a->bytes_verified == b->bytes_verified && a->has_source_crc == b->has_source_crc &&
         a->source_crc32 == b->source_crc32 &&
         a->has_installed_crc == b->has_installed_crc &&
         a->installed_crc32 == b->installed_crc32 && a->deleting == b->deleting &&
         a->has_hdl_identity == b->has_hdl_identity && a->hdl_start == b->hdl_start &&
         a->hdl_size == b->hdl_size && a->hdl_header_crc32 == b->hdl_header_crc32 &&
         a->state == b->state && a->failed_from == b->failed_from &&
         !strcmp(a->last_error, b->last_error) &&
         !strcmp(a->launcher_source, b->launcher_source) && !strcmp(a->opl_cfg, b->opl_cfg) &&
         a->verify_skipped == b->verify_skipped;
}

int tx_identity_matches(const tx_journal_t *j, uint32_t start, uint32_t size,
                        uint32_t header_crc32) {
  return j->has_hdl_identity && j->hdl_start == start && j->hdl_size == size &&
         j->hdl_header_crc32 == header_crc32;
}

static int parse_crc(const char *v, int *has, uint32_t *out) {
  *has = 0;
  *out = 0;
  if (!v[0])
    return 0;
  uint32_t x = 0;
  int n = 0;
  for (; *v; v++, n++) {
    char c = *v;
    int d = (c >= '0' && c <= '9')   ? c - '0'
            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
            : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                     : -1;
    if (d < 0 || n >= 8)
      return -1;
    x = (x << 4) | (uint32_t)d;
  }
  *has = 1;
  *out = x;
  return 0;
}

int tx_journal_filename_for(const char *partition, char out[96]) {
  out[0] = 0;
  if (!partition_is_game_channel(partition) && !partition_is_hidden_game(partition))
    return -1;
  snprintf(out, 96, "install-%s.ini", partition + 3);
  return 0;
}

size_t tx_serialize(const tx_journal_t *j, char *out, size_t outsz) {
  char scrc[9] = "", icrc[9] = "";
  if (j->has_source_crc)
    snprintf(scrc, sizeof(scrc), "%08lx", (unsigned long)j->source_crc32);
  if (j->has_installed_crc)
    snprintf(icrc, sizeof(icrc), "%08lx", (unsigned long)j->installed_crc32);
  char id_start[12] = "", id_size[12] = "", id_crc[9] = "";
  if (j->has_hdl_identity) {
    snprintf(id_start, sizeof(id_start), "%lu", (unsigned long)j->hdl_start);
    snprintf(id_size, sizeof(id_size), "%lu", (unsigned long)j->hdl_size);
    snprintf(id_crc, sizeof(id_crc), "%08lx", (unsigned long)j->hdl_header_crc32);
  }
  int n = snprintf(out, outsz,
                   "source_path=%s\n"
                   "source_size=%llu\n"
                   "startup_id=%s\n"
                   "visible_partition=%s\n"
                   "hidden_partition=%s\n"
                   "bytes_expected=%llu\n"
                   "bytes_written=%llu\n"
                   "bytes_verified=%llu\n"
                   "source_crc32=%s\n"
                   "installed_crc32=%s\n"
                   "deleting=%d\n"
                   "hdl_start=%s\n"
                   "hdl_size=%s\n"
                   "hdl_header_crc32=%s\n"
                   "state=%s\n"
                   "failed_from=%s\n"
                   "last_error=%s\n"
                   "launcher_source=%s\n"
                   "opl_cfg=%s\n"
                   "verify_skipped=%d\n",
                   j->source_path, (unsigned long long)j->source_size,
                   j->startup_id, j->visible_partition, j->hidden_partition,
                   (unsigned long long)j->bytes_expected,
                   (unsigned long long)j->bytes_written,
                   (unsigned long long)j->bytes_verified, scrc, icrc,
                   j->deleting ? 1 : 0, id_start, id_size, id_crc, tx_state_name(j->state),
                   tx_state_name(j->failed_from), j->last_error, j->launcher_source,
                   j->opl_cfg, j->verify_skipped ? 1 : 0);
  if (n < 0 || (size_t)n >= outsz)
    return 0;
  return (size_t)n;
}

int tx_parse(const char *text, tx_journal_t *out) {
  memset(out, 0, sizeof(*out));
  int have_state = 0;
  const char *p = text;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    char line[320];
    if (len >= sizeof(line))
      return -1;
    memcpy(line, p, len);
    line[len] = 0;
    /* Strip only the line ending; values may end in spaces. */
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
      line[--len] = 0;
    p = eol ? eol + 1 : p + len;
    if (!line[0])
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      return -1;
    *eq = 0;
    const char *k = line, *v = eq + 1;

    if (!strcmp(k, "source_path"))
      str_copy(out->source_path, v, sizeof(out->source_path));
    else if (!strcmp(k, "startup_id"))
      str_copy(out->startup_id, v, sizeof(out->startup_id));
    else if (!strcmp(k, "visible_partition"))
      str_copy(out->visible_partition, v, sizeof(out->visible_partition));
    else if (!strcmp(k, "hidden_partition"))
      str_copy(out->hidden_partition, v, sizeof(out->hidden_partition));
    else if (!strcmp(k, "last_error"))
      str_copy(out->last_error, v, sizeof(out->last_error));
    else if (!strcmp(k, "launcher_source"))
      str_copy(out->launcher_source, v, sizeof(out->launcher_source));
    else if (!strcmp(k, "opl_cfg"))
      str_copy(out->opl_cfg, v, sizeof(out->opl_cfg));
    else if (!strcmp(k, "source_size")) {
      if (parse_u64(v, &out->source_size) < 0)
        return -1;
    } else if (!strcmp(k, "bytes_expected")) {
      if (parse_u64(v, &out->bytes_expected) < 0)
        return -1;
    } else if (!strcmp(k, "bytes_written")) {
      if (parse_u64(v, &out->bytes_written) < 0)
        return -1;
    } else if (!strcmp(k, "bytes_verified")) {
      if (parse_u64(v, &out->bytes_verified) < 0)
        return -1;
    } else if (!strcmp(k, "source_crc32")) {
      if (parse_crc(v, &out->has_source_crc, &out->source_crc32) < 0)
        return -1;
    } else if (!strcmp(k, "installed_crc32")) {
      if (parse_crc(v, &out->has_installed_crc, &out->installed_crc32) < 0)
        return -1;
    } else if (!strcmp(k, "hdl_start") || !strcmp(k, "hdl_size")) {
      uint64_t x = 0;
      if (v[0]) {
        if (parse_u64(v, &x) < 0 || x > 0xFFFFFFFFull)
          return -1;
        *(!strcmp(k, "hdl_start") ? &out->hdl_start : &out->hdl_size) = (uint32_t)x;
        out->has_hdl_identity = 1;
      }
    } else if (!strcmp(k, "hdl_header_crc32")) {
      int has = 0;
      if (parse_crc(v, &has, &out->hdl_header_crc32) < 0)
        return -1;
      if (has)
        out->has_hdl_identity = 1;
    } else if (!strcmp(k, "deleting") || !strcmp(k, "verify_skipped")) {
      if (strcmp(v, "0") && strcmp(v, "1"))
        return -1;
      *(!strcmp(k, "deleting") ? &out->deleting : &out->verify_skipped) = v[0] == '1';
    } else if (!strcmp(k, "state")) {
      if (tx_state_parse(v, &out->state) < 0)
        return -1;
      have_state = 1;
    } else if (!strcmp(k, "failed_from")) {
      if (tx_state_parse(v, &out->failed_from) < 0)
        return -1;
    }
    /* Unknown keys are ignored for forward compatibility. */
  }
  if (!have_state || !boot_id_is_valid(out->startup_id) ||
      !partition_pair_matches(out->visible_partition, out->hidden_partition))
    return -1;
  return 0;
}

#ifdef _EE
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static char io_buf[1536];

static int journal_path(const char *dir, const char *partition, char *out,
                        size_t sz) {
  char fn[96];
  if (tx_journal_filename_for(partition, fn) < 0)
    return -1;
  snprintf(out, sz, "%s/%s", dir, fn);
  return 0;
}

static inst_err_t load_path(const char *path, tx_journal_t *j) {
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0)
    return ERR_JOURNAL;
  int r = fileXioRead(fd, io_buf, sizeof(io_buf) - 1);
  fileXioClose(fd);
  if (r <= 0)
    return ERR_JOURNAL;
  io_buf[r] = 0;
  return tx_parse(io_buf, j) == 0 ? ERR_OK : ERR_JOURNAL;
}

static inst_err_t write_file(const char *path, const char *data, size_t n) {
  int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0)
    return ERR_JOURNAL;
  int w = fileXioWrite(fd, (void *)data, (int)n);
  int c = fileXioClose(fd);
  return (w == (int)n && c >= 0) ? ERR_OK : ERR_JOURNAL;
}

/* Write <file>.tmp, read it back, remove <file>, rename .tmp over it.
 * tx_load falls back to <file>.tmp, so a power cut at any point leaves
 * either the old or the new state readable. */
inst_err_t tx_save(const char *dir, const tx_journal_t *j) {
  char path[160], tmp[168];
  fileXioMkdir(dir, 0777); /* may already exist */
  if (journal_path(dir, j->hidden_partition, path, sizeof(path)) < 0)
    return ERR_JOURNAL;
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  size_t n = tx_serialize(j, io_buf, sizeof(io_buf));
  if (n == 0 || write_file(tmp, io_buf, n) != ERR_OK)
    return ERR_JOURNAL;
  tx_journal_t back;
  if (load_path(tmp, &back) != ERR_OK || !tx_journal_equal(&back, j))
    return ERR_JOURNAL;
  fileXioRemove(path);
  if (fileXioRename(tmp, path) < 0)
    return ERR_JOURNAL;
  return ERR_OK;
}

inst_err_t tx_load(const char *dir, const char *partition, tx_journal_t *j) {
  char path[160];
  if (journal_path(dir, partition, path, sizeof(path)) < 0)
    return ERR_JOURNAL;
  inst_err_t e = load_path(path, j);
  if (e) {
    /* Power cut between remove and rename in tx_save. */
    char tmp[168];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    e = load_path(tmp, j);
  }
  /* Defensive: the file must describe this exact pair. */
  if (!e && strcmp(j->hidden_partition + 2, partition + 2) != 0)
    return ERR_JOURNAL;
  return e;
}

inst_err_t tx_remove(const char *dir, const char *partition) {
  char path[160];
  if (journal_path(dir, partition, path, sizeof(path)) < 0)
    return ERR_JOURNAL;
  char tmp[168];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  fileXioRemove(tmp);
  int r = fileXioRemove(path);
  return (r >= 0 || r == -2 /* ENOENT */) ? ERR_OK : ERR_JOURNAL;
}

int tx_scan_unfinished(const char *dir, tx_journal_t *out, int max) {
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return 0;
  iox_dirent_t de;
  int n = 0;
  while (n < max && fileXioDread(dd, &de) > 0) {
    if (strncmp(de.name, "install-", 8) != 0 || !str_ends_with_ci(de.name, ".ini"))
      continue;
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", dir, de.name);
    if (load_path(path, &out[n]) == ERR_OK && out[n].state != TX_COMPLETE)
      n++;
  }
  fileXioDclose(dd);
  return n;
}
#endif
