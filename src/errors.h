#ifndef PSXI_ERRORS_H
#define PSXI_ERRORS_H

/* Structured error codes (plan section 32). Every destructive function
 * returns one of these; ERR_OK is zero so `if (err)` reads naturally. */
typedef enum {
  ERR_OK = 0,
  ERR_NETWORK,
  ERR_UDPFS_DISCOVERY,
  ERR_SOURCE_OPEN,
  ERR_SOURCE_READ,
  ERR_SOURCE_INVALID_ISO,
  ERR_SOURCE_SYSTEM_CNF,
  ERR_HDD_MISSING,
  ERR_HDD_NOT_FORMATTED,
  ERR_NO_SPACE,
  ERR_PARTITION_EXISTS,
  ERR_HDL_CREATE,
  ERR_HDL_FORMAT,
  ERR_HDL_MOUNT,
  ERR_HDL_WRITE,
  ERR_HDL_VERIFY,
  ERR_PFS_CREATE,
  ERR_PFS_FORMAT,
  ERR_PFS_MOUNT,
  ERR_XMB_RESOURCE_WRITE,
  ERR_XMB_HEADER_WRITE,
  ERR_XMB_VERIFY,
  ERR_OPL_NOT_FOUND,
  ERR_KELF_MISSING,
  ERR_USER_ABORT,
  /* Additions beyond the plan's minimum list. */
  ERR_INVALID_ARG,
  ERR_JOURNAL,
  ERR_PARTITION_DELETE,
  ERR_INTERNAL,
  ERR_HDL_PLAN,
  ERR_PAUSED, /* the user paused a copy: Resume copy continues it */
  ERR__COUNT
} inst_err_t;

/* Stable identifier, e.g. "ERR_HDL_WRITE". Never NULL. */
const char *err_name(inst_err_t err);

/* Short human description for the error screen. Never NULL. */
const char *err_text(inst_err_t err);

#endif
