#!/usr/bin/env python3
"""Drive the production repair/removal screens with reordered selection events.

Ensures only selected games are written, cancel performs no writes, unsafe
repair entries stay excluded, and a failed game does not stop the batch.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/flows.c').read_text()
remove = source[source.index('void flow_remove_games(void)'):source.index('/* Batch repair:')]
repair = source[source.index('void flow_repair(void)'):source.index('/* Settings entry point')]
harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "flows.h"
#include "listui.h"
#include "remove_games.h"
#include "opl_dependency.h"
#include "apa_osd_header.h"
#define MAX_PAIRS 4
typedef struct { char visible[33], hidden[33]; pair_state_t state; int can_cover, missing_cover; } pair_row_t;
static pair_row_t pairs[MAX_PAIRS];
static remove_entry_t rm[MAX_PAIRS];
static char rm_rows[MAX_PAIRS][UI_ROW_LEN];
static lv_item_t pair_items[MAX_PAIRS];
static listui_state_t rm_ls;
typedef struct {const game_plan_t *p; install_stage_t stage; int no_skip, skip_verify, paused;} progress_ctx_t;
static int event, scenario, writes, seen[4], confirm;
static const char *space_line(void) {return "space";}
static int collect_pairs(void) {
  for (int i=0;i<4;i++) {
    snprintf(pairs[i].visible,33,"PP.SLUS-2031%d..GAME%d",i,i);
    snprintf(pairs[i].hidden,33,"__.SLUS-2031%d..GAME%d",i,i);
    pairs[i].state=i==3?PAIR_HIDDEN_UNVERIFIED:PAIR_CHANNEL_BROKEN;
    pairs[i].can_cover=pairs[i].missing_cover=0;
  }
  return 4;
}
void ui_header(const char *t,const char *s) {(void)t;(void)s;}
void ui_at(int row,const char *f,...) {(void)row;(void)f;}
void ui_message(const char *t,const char *s) {(void)t;(void)s;}
void ui_text_view(const char *t,const char *s) {(void)t;assert(s && *s);}
int ui_confirm(const char *t,const char *s) {(void)t;(void)s;return confirm;}
int ui_confirm_destructive(const char *t,const char *s) {return ui_confirm(t,s);}
int hdd_space_mb(uint32_t *a,uint32_t *b,uint32_t *c) {(void)a;(void)c;if(b)*b=10000;return 0;}
int hdd_exists(const char *p) {(void)p;return 0;}
static void record(const char *p) {
  int i=p[12]-'0'; assert(i>=0 && i<4);seen[i]++;writes++;
}
inst_err_t game_delete_pair(const char *v,const char *h,const char **f,int *rc) {
  (void)h;record(v);*rc=0;*f=NULL;return ERR_OK;
}
static void cb_stage(void *c,install_stage_t s) {(void)c;(void)s;}
static int ensure_opl(const char *t,int a,const char **w) {(void)t;(void)a;(void)w;return 1;}
inst_err_t opl_check_runtime(opl_runtime_t *o,int *r) {(void)o;*r=0;return ERR_OK;}
void game_create_channel(const char *h,const install_ui_t *u,install_report_t *r) {
  (void)u;record(h);r->err=h[12]=='0'?ERR_XMB_VERIFY:ERR_OK;
}
void game_add_cover(const char *h,const install_ui_t *u,install_report_t *r) {game_create_channel(h,u,r);}
void game_refresh_resources(const char *v,install_report_t *r) {record(v);r->err=ERR_OK;}
int channel_get_title(const char *p,char *out,size_t n) {(void)p;snprintf(out,n,"Game");return 0;}
inst_err_t osd_header_write(const char *p,const char *s,const char *t,const char *id,int *r) {
  (void)s;(void)t;(void)id;record(p);*r=0;return ERR_OK;
}
int listui_pick(const char *t,const char *s,const lv_item_t *items,char rows[][UI_ROW_LEN],
                int n,listui_state_t *ls,const char *footer,int mask,int *key) {
  (void)t;(void)s;(void)items;(void)ls;(void)footer;assert(n==4 && (mask&UI_SQUARE));
  if(event==0) for(int i=0;i<n;i++) assert(!strncmp(rows[i],"[ ]",3));
  *key=0;
  int step=event++;
  if(scenario==2) return -1;
  if(scenario==1) { /* select all, clear all, then select a single row */
    if(step<2) {*key=UI_START;return 0;}
    if(step==2) {*key=UI_SQUARE;return 2;}
    return 0;
  }
  /* Sorted view returns caller indices, not screen-row numbers. */
  if(step==0) {*key=UI_SQUARE;return 2;}
  if(step==1) {*key=UI_SQUARE;return 0;}
  if(step==2) {*key=UI_SQUARE;return 3;}
  return 1; /* X on an unselected row must not add that game. */
}
'''
tests = r'''
int main(void) {
  for(int removal=0;removal<2;removal++) for(scenario=0;scenario<3;scenario++) {
    for(confirm=0;confirm<2;confirm++) {
      event=writes=0;memset(seen,0,sizeof(seen));
      if(removal)flow_remove_games();else flow_repair();
      if(!confirm || scenario==2)assert(writes==0);
      else if(scenario==1)assert(writes==1 && seen[2]==1);
      else {
        assert(seen[0]==1 && seen[2]==1 && seen[1]==0);
        assert(seen[3]==removal && writes==2+removal);
      }
    }
  }
  puts("Production game multi-selection, cancellation and batch continuation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='psxi-game-selection-') as tmp:
    src = Path(tmp) / 'selection.c'
    src.write_text(harness + remove + repair + tests)
    binary = Path(tmp) / 'selection'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-Wl,--gc-sections', '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'src'), str(src),
                    *[str(ROOT / 'src' / (n + '.c')) for n in ['remove_games', 'game_pair', 'util', 'partname', 'errors']],
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
