#!/usr/bin/env python3
"""Exercise production cover inspection and resource-only refresh."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/xmb_game_channel.c').read_text()
functions = source[source.index('/* Inspect both on-disk jackets'):]
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "hdd_partitions.h"
#include "manifest.h"
#include "pfs_channel.h"
#include "partname.h"
#include "util.h"
#include "xmb_game_channel.h"
#define FIO_MT_RDONLY 1
#define FIO_MT_RDWR 2
#define JACKET_MAX 524288
app_state_t g_app;
int g_manifest_loaded;
static int mounted, mount_fail, mode, served, writes;
static const unsigned char default_art[4]={1,2,3,4};
static unsigned char disk[2][4];
static int sizes[2];
int pfs_mount(const char *w,const char *p,int m) { (void)w;(void)m; assert(!strcmp(p,"PP.SLUS-20312..GAME")); if(mount_fail)return -5; mounted=1;return 0; }
void pfs_umount(const char *w) { (void)w; mounted=0; }
int file_load(const char *p,void **out,uint32_t max) { (void)max; assert(mounted); int i=strstr(p,"001")?0:1; *out=NULL; if(!sizes[i])return -1; *out=malloc(4); memcpy(*out,disk[i],4);return sizes[i]; }
int png_is_size(const void *p,uint32_t n,uint32_t w,uint32_t h) { (void)w;(void)h; return n==4 && ((const unsigned char *)p)[0]!=0; }
void payload_default_jackets(jacket_pair_t *j) { *j=(jacket_pair_t){default_art,4,default_art,4}; }
int hdd_stat(const char *p,uint16_t *t,uint32_t *a,uint32_t *b) { (void)p;(void)a;(void)b; *t=APA_TYPE_PFS_ID;return 0; }
const char *game_load_jackets(const char *id,jacket_pair_t *j,void *owned[2]) {
  assert(!strcmp(id,"SLUS_203.12")); owned[0]=owned[1]=NULL;
  static const unsigned char a[4]={42,2,3,4}, b[4]={43,2,3,4};
  *j=(jacket_pair_t){a,4,b,4}; return served?"server":"missing";
}
int channel_get_title(const char *p,char *t,size_t n) { (void)p;str_copy(t,"GAME",n);return 0; }
int game_load_info(const char *id,xmb_game_info_t *gi) { assert(!strcmp(id,"SLUS_203.12")); memset(gi,0,sizeof(*gi)); strcpy(gi->description,"Correct game's story"); return mode; }
int install_date(char d[9]) { strcpy(d,"20261008");return 0; }
int fileXioMkdir(const char *p,int m) { (void)m;assert(mounted && !strcmp(p,PFS_WORK "res"));return 0; }
int extras_write_verified(const char *p,const void *data,uint32_t n) {
  assert(mounted);writes++;
  if(strstr(p,"jkt_001")){assert(n==4);memcpy(disk[0],data,4);sizes[0]=4;}
  else if(strstr(p,"jkt_002")){assert(n==4);memcpy(disk[1],data,4);sizes[1]=4;}
  else { assert(!strcmp(p,PFS_WORK "res/info.sys")); assert(strstr(data,"note = Correct game's story")); assert(strstr(data,"title_id = SLUS-20312")); }
  return 0;
}
'''
tests = r'''
int main(void) {
 const char *p="PP.SLUS-20312..GAME";
 install_report_t rep;
 assert(game_cover_status(p)==GAME_COVER_MISSING && !mounted);
 sizes[0]=sizes[1]=4;memcpy(disk[0],default_art,4);memcpy(disk[1],default_art,4);
 assert(game_cover_status(p)==GAME_COVER_DEFAULT);
 disk[0][0]=42;disk[1][0]=0; assert(game_cover_status(p)==GAME_COVER_INVALID);
 disk[1][0]=43; assert(game_cover_status(p)==GAME_COVER_READY);
 mount_fail=1;assert(game_cover_status(p)==GAME_COVER_UNREADABLE);mount_fail=0;
 memcpy(disk[0],default_art,4);memcpy(disk[1],default_art,4);
 g_app.net=NETWORK_READY;g_manifest_loaded=1;
 game_refresh_resources(p,&rep);assert(rep.err && writes==0 && game_cover_status(p)==GAME_COVER_DEFAULT);
 served=1;mode=1;game_refresh_resources(p,&rep);assert(!rep.err && writes==3 && game_cover_status(p)==GAME_COVER_READY);
 mode=0;writes=0;game_refresh_resources(p,&rep);assert(!rep.err && writes==2); /* keep metadata without server DB */
 assert(!mounted);puts("Production missing/default cover detection and refresh passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'covers.c'
    src.write_text(harness + functions + tests)
    binary = Path(tmp) / 'covers'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(ROOT / 'src'), str(src),
                    *[str(ROOT / 'src' / (n + '.c')) for n in ['util', 'partname', 'xmb_text']],
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
