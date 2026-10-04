#include <malloc.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "hdd_partitions.h"
#include "opl_launcher_payload.h"
#include "pfs_channel.h"
#include "settings.h"
#include "xmb_installer_app.h"
#include "xmb_text.h"

#define INSTALLER_TITLE "UDPFS Game Installer"
#define INSTALLER_TITLE_ID "UDPFS-INSTALLER"

static void build_content(channel_content_t *c, const payload_t *kelf,
                          char *info, size_t infosz) {
  const uint8_t *jkt;
  uint32_t jkt_size;
  payload_installer_jacket(&jkt, &jkt_size);
  c->kelf = kelf->data;
  c->kelf_size = kelf->size;
  c->info_sys = info;
  c->info_sys_len =
      (uint32_t)xmb_render_info_sys(info, infosz, INSTALLER_TITLE, INSTALLER_TITLE_ID);
  c->jacket = jkt;
  c->jacket_size = jkt_size;
}

/* Extra (non-XMB) content: config, state dir, OPL-Launcher stash. */
static int write_app_extras(const payload_t *opl, selfinstall_report_t *rep) {
  if (pfs_mount(PFS_WORK, INSTALLER_PARTITION, FIO_MT_RDWR) < 0)
    return -1;
  fileXioMkdir(PFS_WORK "config", 0777);
  fileXioMkdir(PFS_WORK "state", 0777);
  fileXioMkdir(PFS_WORK "payload", 0777);
  char buf[64];
  size_t n = settings_serialize(&g_app.settings, buf, sizeof(buf));
  int r = file_write_all(PFS_WORK "config/network.ini", buf, (uint32_t)n);
  if (r >= 0 && opl && opl->size &&
      file_write_all(PFS_WORK "payload/OPL-LAUNCHER.KELF", opl->data, opl->size) >= 0)
    rep->opl_launcher_stashed = 1;
  pfs_umount(PFS_WORK);
  return r;
}

void installer_app_install(selfinstall_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  payload_t kelf, opl;
  memset(&opl, 0, sizeof(opl));

  if ((rep->err = payload_installer(&kelf, g_app.app_mounted,
                                    g_app.net == NETWORK_READY))) {
    rep->detail = "put installer-EXECUTE.KELF in udpfs:/PAYLOAD/";
    return;
  }
  rep->kelf_origin = kelf.origin;
  /* Optional: keep a copy of OPL-Launcher so later game installs do
   * not depend on the server's PAYLOAD folder. */
  payload_opl_launcher(&opl, g_app.app_mounted, g_app.net == NETWORK_READY);

  /* The partition cannot be mounted twice; release pfs0: first. Any
   * payload we loaded from pfs0: is already in memory. */
  app_unmount();

  char info[1024];
  channel_content_t c;
  build_content(&c, &kelf, info, sizeof(info));

  int rc = 0;
  int exists = hdd_exists(INSTALLER_PARTITION);
  if (exists < 0) {
    rep->err = ERR_HDD_MISSING;
    goto out;
  }
  if (!exists) {
    if ((rep->err = pfs_create_partition(INSTALLER_PARTITION, CHANNEL_SIZE_STR, &rc))) {
      rep->rc = rc;
      goto out;
    }
    rep->created = 1;
  }

  channel_result_t cr = channel_populate(INSTALLER_PARTITION, &c);
  if (!cr.err && write_app_extras(&opl, rep) < 0)
    cr = (channel_result_t){ERR_XMB_RESOURCE_WRITE, 0, "config/network.ini"};
  if (!cr.err)
    cr = channel_verify(INSTALLER_PARTITION, &c);
  rep->err = cr.err;
  rep->rc = cr.rc;
  rep->detail = cr.step;
  /* Only remove a partition we created in this run; an existing one
   * holds the user's config and journals. */
  if (cr.err && rep->created)
    hdd_remove_exact(INSTALLER_PARTITION, NULL);

out:
  payload_release(&opl);
  payload_release(&kelf);
  app_mount();
}

inst_err_t installer_app_verify(const char **detail) {
  *detail = NULL;
  if (hdd_exists(INSTALLER_PARTITION) <= 0) {
    *detail = "PP.UDPFS-INSTALLER does not exist";
    return ERR_XMB_VERIFY;
  }
  int was = g_app.app_mounted;
  app_unmount();
  inst_err_t e = channel_quick_check(INSTALLER_PARTITION);
  if (e)
    *detail = "files or PPAA/system.cnf header invalid";
  if (was)
    app_mount();
  return e;
}
