#!/usr/bin/env python3
"""Exercise the production build_channel function with mocked HDD/PFS I/O."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/xmb_game_channel.c').read_text()
start = source.index('static void build_channel(')
end = source.index('\nvoid game_install(', start)
function = source[start:end]
res_src = (ROOT / 'src/pfs_channel.c').read_text()
res_start = res_src.index('static char g_man[2048];')
res_end = res_src.index('\nchannel_result_t channel_populate(', res_start)
res_function = res_src[res_start:res_end]
prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "hdd_partitions.h"
#include "pfs_channel.h"
#include "transaction.h"
#include "util.h"
#include "xmb_game_channel.h"
app_state_t g_app;
#define W PFS_WORK
static int info_writes, large_writes, small_writes;
static channel_result_t cres(inst_err_t e, int rc, const char *step) { return (channel_result_t){e,rc,step}; }
int fileXioMkdir(const char *p, int m) { (void)p; (void)m; return 0; }
int64_t file_size(const char *p) { (void)p; return 1; } /* existing manual */
void payload_copyright_strip(const uint8_t **p, uint32_t *n) { static uint8_t v; *p=&v; *n=1; }
void payload_manual_page(const uint8_t **p, uint32_t *n) { payload_copyright_strip(p,n); }
int file_write_all(const char *path, const void *data, uint32_t len) {
  if (!strcmp(path,W "res/info.sys")) { info_writes++; assert(len && strstr(data,"title_id = SLUS-20312")); }
  else if (!strcmp(path,W "res/jkt_001.png")) { large_writes++; if(len) assert(((const uint8_t *)data)[0]==42); }
  else if (!strcmp(path,W "res/jkt_002.png")) { small_writes++; if(len) assert(((const uint8_t *)data)[0]==43); }
  else assert(0);
  return 0;
}

static int where, exists, renames, creates, populates, verifies, headers, removals;
static int no_space, rename_fail, pfs_fail, journal_fail;
static uint16_t visible_type;
static int server_art, info_loads, jacket_loads;
int game_load_info(const char *id, xmb_game_info_t *info) {
  assert(!strcmp(id,"SLUS_203.12")); info_loads++;
  memset(info,0,sizeof(*info));
  strcpy(info->developer,"Test Developer");
  strcpy(info->description,"Game-specific description");
  return server_art;
}
const char *game_load_jackets(const char *id, jacket_pair_t *j, void *owned[2]) {
  assert(!strcmp(id,"SLUS_203.12")); jacket_loads++;
  if (!server_art) { memset(j,0,sizeof(*j)); return "default"; }
  owned[0]=malloc(16); owned[1]=malloc(8);
  memset(owned[0],42,16); memset(owned[1],43,8);
  *j=(jacket_pair_t){owned[0],16,owned[1],8};
  return "server";
}
int game_data_partition(const char *h, char out[APA_NAME_MAX + 1]) { strcpy(out, where == 1 ? "PP.GAME" : h); return where; }
int hdd_exists(const char *p) { (void)p; return exists; }
int hdd_stat(const char *p, uint16_t *t, uint32_t *s, uint32_t *b) {
  (void)p; (void)s; (void)b; *t = visible_type; return 0;
}
inst_err_t hdd_space_check(uint32_t n) { assert(n == 128); return no_space ? ERR_NO_SPACE : ERR_OK; }
int fileXioUmount(const char *p) { (void)p; return 0; }
void pfs_umount(const char *p) { (void)p; }
inst_err_t hdd_rename_game(const char *a, const char *b, int *rc) {
  (void)a; (void)rc; renames++;
  if (rename_fail) return ERR_PARTITION_RENAME;
  where = b[0] == '_' ? 0 : 1; return ERR_OK;
}
inst_err_t hdd_remove_exact(const char *p, int *rc) { (void)p; (void)rc; removals++; exists=0; return ERR_OK; }
channel_result_t channel_populate(const char *p, const channel_content_t *c) {
  (void)p; assert(c->kelf_size == 2048); populates++;
  assert(c->info_sys_len>0 && strstr(c->info_sys,"SLUS-20312"));
  if (server_art) {
    assert(strstr(c->info_sys,"Test Developer"));
    assert(strstr(c->info_sys,"note = Game-specific description"));
    assert(c->jkt.large_size==16 && c->jkt.small_size==8);
    assert(((const unsigned char *)c->jkt.large)[0]==42 && ((const unsigned char *)c->jkt.small)[0]==43);
  }
  if (pfs_fail) return (channel_result_t){ERR_XMB_RESOURCE_WRITE,0,"populate"};
  return channel_write_res(c->osd_title0,c->info_sys,c->info_sys_len,&c->jkt);
}
channel_result_t channel_verify(const char *p, const channel_content_t *c) {
  (void)p; (void)c; verifies++; return (channel_result_t){ERR_OK,0,NULL};
}
channel_result_t channel_create(const char *p, const channel_content_t *c) {
  creates++;
  channel_result_t r=channel_populate(p,c);
  if (!r.err) { exists=1; visible_type=APA_TYPE_PFS_ID; r=channel_verify(p,c); }
  return r;
}
inst_err_t game_header_write(const char *p,const char *t,const char *id,const void *k,uint32_t n,int *rc) {
  (void)p;(void)t;(void)id;(void)k;(void)n;(void)rc; headers++; return ERR_OK;
}
inst_err_t game_header_verify(const char *p,const char *t,const char *id,const void *k,uint32_t n,int *rc) {
  (void)p;(void)t;(void)id;(void)k;(void)n;(void)rc; verifies++; return ERR_OK;
}
int install_date(char out[9]) { strcpy(out,"20261007"); return 0; }
void payload_default_jackets(jacket_pair_t *j) { memset(j,0,sizeof(*j)); }
void payload_release(payload_t *k) { memset(k,0,sizeof(*k)); }
static void stage(const install_ui_t *ui,install_report_t *r,install_stage_t s) { (void)ui; r->stage=s; }
static inst_err_t persist(tx_journal_t *j) { (void)j; return ERR_OK; }
static inst_err_t advance(tx_journal_t *j,tx_state_t s) {
  if (journal_fail) return ERR_JOURNAL;
  return tx_advance(j,s);
}
static void fail(tx_journal_t *j,install_report_t *r,inst_err_t e,int rc,const char *step) {
  tx_fail(j,e); r->err=e; r->rc=rc; r->detail=step;
}
static const char *install_ps2_extras(const char *id, const char *path, const char **desc) { (void)id; (void)path; *desc = NULL; return "none"; }
'''
tests = r'''
static void reset(console_t console,int location,int pfs) {
  memset(&g_app,0,sizeof(g_app)); g_app.settings.console=console;
  where=location; exists=pfs; visible_type=APA_TYPE_PFS_ID;
  renames=creates=populates=verifies=headers=removals=0;
  no_space=rename_fail=pfs_fail=journal_fail=0;
  server_art=1; info_loads=jacket_loads=0;
}
static inst_err_t run(void) {
  tx_journal_t j={0}; install_report_t r={0};
  j.state=TX_HDL_VERIFIED; strcpy(j.hidden_partition,"__.GAME");
  strcpy(j.visible_partition,"PP.GAME"); strcpy(j.startup_id,"SLUS_203.12");
  unsigned char bytes[2048]={0}; payload_t k={bytes,sizeof(bytes),0,"embedded"};
  build_channel(&j,"Game",&k,NULL,&r);
  if (!r.err) assert(j.state==TX_COMPLETE);
  else assert(j.state==TX_FAILED);
  return r.err;
}
int main(void) {
  reset(CONSOLE_PSX1,0,0); assert(run()==ERR_OK);
  assert(where==0 && creates==1 && renames==0 && headers==0 && verifies==1);
  reset(CONSOLE_PSX1,1,0); assert(run()==ERR_OK);
  assert(where==0 && creates==1 && renames==1 && headers==0);
  reset(CONSOLE_PSX1,0,1); assert(run()==ERR_OK);
  assert(populates==1 && creates==0 && removals==0 && renames==0 && headers==0);
  reset(CONSOLE_PSX1,1,0); pfs_fail=1; assert(run()==ERR_XMB_RESOURCE_WRITE);
  assert(where==0 && renames==1 && headers==0); /* no PATINFO rollback */
  reset(CONSOLE_PSX1,1,0); no_space=1; assert(run()==ERR_NO_SPACE);
  assert(where==1 && renames==0 && creates==0 && headers==0);
  reset(CONSOLE_PSX1,1,0); rename_fail=1; assert(run()==ERR_PARTITION_RENAME);
  assert(where==1 && creates==0);
  reset(CONSOLE_PSX1,0,1); visible_type=APA_TYPE_HDL_ID; assert(run()==ERR_PARTITION_EXISTS);
  assert(populates==0 && removals==0 && headers==0);
  reset(CONSOLE_PSX1,0,0); journal_fail=1; assert(run()==ERR_JOURNAL);
  assert(where==0 && headers==0);
  reset(CONSOLE_UNKNOWN,0,0); assert(run()==ERR_CONSOLE_UNKNOWN);
  assert(headers==0 && creates==0 && renames==0);
  reset(CONSOLE_PSX2,0,0); assert(run()==ERR_OK);
  assert(where==0 && headers==0 && renames==0 && creates==1);
  reset(CONSOLE_PSX2,0,1); assert(run()==ERR_OK);
  assert(where==0 && removals==0 && headers==0 && creates==0 && populates==1);
  reset(CONSOLE_PSX2,1,0); assert(run()==ERR_OK);
  assert(headers==0 && renames==1 && creates==1 && where==0);
  reset(CONSOLE_PSX1,0,0); server_art=0; assert(run()==ERR_OK);
  assert(creates==1 && info_loads==1 && jacket_loads==1);
  reset(CONSOLE_PSX2,1,0); pfs_fail=1; assert(run()==ERR_XMB_RESOURCE_WRITE);
  assert(where==0 && creates==1 && removals==0 && headers==0);
  assert(info_writes > 0 && info_writes == large_writes && info_writes == small_writes);
  puts("PSX1/PSX2 production channel layout and RES writes passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    harness = Path(d) / 'channel.c'
    harness.write_text(prelude + res_function + function + tests)
    binary = Path(d) / 'channel'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(ROOT / 'src'),
                    str(harness), *[str(ROOT / 'src' / (n + '.c')) for n in
                    ['util', 'partname', 'xmb_text', 'transaction', 'errors']],
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
