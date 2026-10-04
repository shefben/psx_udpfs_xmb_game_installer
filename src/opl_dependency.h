#ifndef PSXI_OPL_DEPENDENCY_H
#define PSXI_OPL_DEPENDENCY_H

#include "errors.h"

/* OPL runtime resolution, reproducing OPL-Launcher 6da1af2 main.c:
 *
 *   mount hdd0:__common; read first line of OPL/conf_hdd.cfg;
 *   name = text after the first '=' up to CR/LF; else "+OPL"
 *   partition = hdd0:<name>
 *   ELF = (name[0] == '+') ? "OPNPS2LD.ELF" : "OPL/OPNPS2LD.ELF"
 */

typedef struct {
  char partition[129];   /* bare APA name, e.g. "+OPL" */
  char elf_path[32];     /* path inside that partition */
  int from_config;       /* 1 if taken from conf_hdd.cfg */
  int config_malformed;  /* 1 if the launcher would misbehave on it */
} opl_runtime_t;

/* `conf_text` is the conf_hdd.cfg content, or NULL if __common could
 * not be mounted or the file is absent. Returns 0, or -1 if the
 * config is malformed in a way that would break OPL-Launcher (no '=',
 * a '%' in the value, or a name longer than an APA name). */
int opl_resolve_from_conf(const char *conf_text, opl_runtime_t *out);

#ifdef _EE
/* Full on-console check: resolves and verifies the ELF exists.
 * ERR_OK or ERR_OPL_NOT_FOUND; `rc_out` receives a driver code. */
inst_err_t opl_check_runtime(opl_runtime_t *out, int *rc_out);

/* OPL missing: install the OPNPS2LD.ELF udpfsd offers (hash-checked)
 * where OPL-Launcher looks for it, creating only the default +OPL.
 * ERR_OK when OPL is present afterwards; else *detail says why. */
inst_err_t opl_install_from_server(const char **detail, int *rc_out);
#endif

#endif
