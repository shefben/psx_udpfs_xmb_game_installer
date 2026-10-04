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
  /* Channel repair on a hidden game that re-verified. */
  if (to == TX_HDL_VERIFIED)
    return from == TX_NONE || from == TX_FAILED || from == TX_COMPLETE;
  return 0;
}

inst_err_t tx_advance(tx_journal_t *j, tx_state_t to) {
  if (!tx_transition_allowed(j->state, to))
    return ERR_INTERNAL;
  j->state = to;
  if (to != TX_FAILED)
    j->last_error[0] = 0;
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

int tx_journal_filename(const char *startup_id, char out[64]) {
  char part_id[PART_ID_LEN + 1];
  out[0] = 0;
  if (boot_id_to_part_id(startup_id, part_id) < 0)
    return -1;
  snprintf(out, 64, "install-%s.ini", part_id);
  return 0;
}

size_t tx_serialize(const tx_journal_t *j, char *out, size_t outsz) {
  int n = snprintf(out, outsz,
                   "source_path=%s\n"
                   "source_size=%llu\n"
                   "startup_id=%s\n"
                   "visible_partition=%s\n"
                   "hidden_partition=%s\n"
                   "bytes_expected=%llu\n"
                   "bytes_written=%llu\n"
                   "state=%s\n"
                   "failed_from=%s\n"
                   "last_error=%s\n",
                   j->source_path, (unsigned long long)j->source_size,
                   j->startup_id, j->visible_partition, j->hidden_partition,
                   (unsigned long long)j->bytes_expected,
                   (unsigned long long)j->bytes_written, tx_state_name(j->state),
                   tx_state_name(j->failed_from), j->last_error);
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
    str_rtrim(line);
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
    else if (!strcmp(k, "source_size")) {
      if (parse_u64(v, &out->source_size) < 0)
        return -1;
    } else if (!strcmp(k, "bytes_expected")) {
      if (parse_u64(v, &out->bytes_expected) < 0)
        return -1;
    } else if (!strcmp(k, "bytes_written")) {
      if (parse_u64(v, &out->bytes_written) < 0)
        return -1;
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

static char io_buf[1024];

static void journal_path(const char *dir, const char *startup_id, char *out,
                         size_t sz) {
  char fn[64];
  tx_journal_filename(startup_id, fn);
  snprintf(out, sz, "%s/%s", dir, fn);
}

inst_err_t tx_save(const char *dir, const tx_journal_t *j) {
  char path[128];
  fileXioMkdir(dir, 0777); /* may already exist */
  journal_path(dir, j->startup_id, path, sizeof(path));
  size_t n = tx_serialize(j, io_buf, sizeof(io_buf));
  if (n == 0)
    return ERR_JOURNAL;
  int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0)
    return ERR_JOURNAL;
  int w = fileXioWrite(fd, io_buf, (int)n);
  fileXioClose(fd);
  return w == (int)n ? ERR_OK : ERR_JOURNAL;
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

inst_err_t tx_load(const char *dir, const char *startup_id, tx_journal_t *j) {
  char path[128];
  journal_path(dir, startup_id, path, sizeof(path));
  return load_path(path, j);
}

inst_err_t tx_remove(const char *dir, const char *startup_id) {
  char path[128];
  journal_path(dir, startup_id, path, sizeof(path));
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
