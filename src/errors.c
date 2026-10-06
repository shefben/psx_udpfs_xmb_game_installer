#include "errors.h"

static const struct {
  const char *name;
  const char *text;
} ERRS[ERR__COUNT] = {
    [ERR_OK] = {"ERR_OK", "no error"},
    [ERR_NETWORK] = {"ERR_NETWORK", "network stack failed"},
    [ERR_UDPFS_DISCOVERY] = {"ERR_UDPFS_DISCOVERY", "udpfsd server not found"},
    [ERR_SOURCE_OPEN] = {"ERR_SOURCE_OPEN", "cannot open source file"},
    [ERR_SOURCE_READ] = {"ERR_SOURCE_READ", "source read failed"},
    [ERR_SOURCE_INVALID_ISO] = {"ERR_SOURCE_INVALID_ISO", "Not a valid PS2 ISO"},
    [ERR_SOURCE_SYSTEM_CNF] = {"ERR_SOURCE_SYSTEM_CNF",
                               "SYSTEM.CNF missing or has no BOOT2"},
    [ERR_HDD_MISSING] = {"ERR_HDD_MISSING", "internal HDD not detected"},
    [ERR_HDD_NOT_FORMATTED] = {"ERR_HDD_NOT_FORMATTED",
                               "HDD is not APA formatted"},
    [ERR_NO_SPACE] = {"ERR_NO_SPACE", "not enough free HDD space"},
    [ERR_PARTITION_EXISTS] = {"ERR_PARTITION_EXISTS",
                              "partition already exists"},
    [ERR_HDL_CREATE] = {"ERR_HDL_CREATE", "HDL partition create failed"},
    [ERR_HDL_FORMAT] = {"ERR_HDL_FORMAT", "HDL header format failed"},
    [ERR_HDL_MOUNT] = {"ERR_HDL_MOUNT", "hdl0: mount failed"},
    [ERR_HDL_WRITE] = {"ERR_HDL_WRITE", "HDD write failed"},
    [ERR_HDL_VERIFY] = {"ERR_HDL_VERIFY", "HDL verification failed"},
    [ERR_PFS_CREATE] = {"ERR_PFS_CREATE", "PFS partition create failed"},
    [ERR_PFS_FORMAT] = {"ERR_PFS_FORMAT", "PFS format failed"},
    [ERR_PFS_MOUNT] = {"ERR_PFS_MOUNT", "PFS mount failed"},
    [ERR_XMB_RESOURCE_WRITE] = {"ERR_XMB_RESOURCE_WRITE",
                                "writing XMB resources failed"},
    [ERR_XMB_HEADER_WRITE] = {"ERR_XMB_HEADER_WRITE",
                              "writing PPAA/system.cnf header failed"},
    [ERR_XMB_VERIFY] = {"ERR_XMB_VERIFY", "XMB channel verification failed"},
    [ERR_OPL_NOT_FOUND] = {"ERR_OPL_NOT_FOUND", "OPL runtime not found"},
    [ERR_KELF_MISSING] = {"ERR_KELF_MISSING", "signed EXECUTE.KELF missing"},
    [ERR_USER_ABORT] = {"ERR_USER_ABORT", "aborted by user"},
    [ERR_INVALID_ARG] = {"ERR_INVALID_ARG", "invalid argument"},
    [ERR_JOURNAL] = {"ERR_JOURNAL", "transaction journal I/O failed"},
    [ERR_PARTITION_DELETE] = {"ERR_PARTITION_DELETE",
                              "partition delete failed"},
    [ERR_INTERNAL] = {"ERR_INTERNAL", "internal error"},
    [ERR_HDL_PLAN] = {"ERR_HDL_PLAN",
                      "game does not fit the drive's APA partition limits"},
    [ERR_PAUSED] = {"ERR_PAUSED", "copy paused (Resume copy continues it)"},
    [ERR_PARTITION_RENAME] = {"ERR_PARTITION_RENAME",
                              "partition rename failed (game shown/hidden in the XMB)"},
    [ERR_DATA_LIMIT] = {"ERR_DATA_LIMIT",
                        "the 128 GiB limit for games and data would be passed"},
};

const char *err_name(inst_err_t err) {
  if ((unsigned)err >= ERR__COUNT || !ERRS[err].name)
    return "ERR_UNKNOWN";
  return ERRS[err].name;
}

const char *err_text(inst_err_t err) {
  if ((unsigned)err >= ERR__COUNT || !ERRS[err].text)
    return "unknown error";
  return ERRS[err].text;
}
