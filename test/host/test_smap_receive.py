"""Run the production FIFO reader against simulated DMA and PIO registers."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
if os.environ.get('SMAP_SOURCE'):
    source = Path(os.environ['SMAP_SOURCE']).read_text()
else:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'iop/smap/src/xfer.c'
        path.parent.mkdir(parents=True)
        path.write_bytes((ROOT / 'reference/neutrino/iop/smap/src/xfer.c').read_bytes())
        subprocess.run(['patch', '-p1', '--quiet'], cwd=directory,
                       input=(ROOT / 'patches/neutrino/0003-smap-receive-tail.patch').read_bytes(), check=True)
        source = path.read_text()
start = source.index('void smap_fifo_read(')
end = source.index('\n}\n', start) + 3
function = source[start:end]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
static unsigned char fifo[4096], output[4096];
static unsigned position;
static uint16_t pointer_register, half_register;
static struct { volatile u8 *smap_regbase; } SmapDriverData;
static uint16_t g_current_frame_ptr, g_dma_max_block_size = 512;
#define SMAP_R_RXFIFO_RD_PTR 1
#define SMAP_R_RXFIFO_DATA 2
#define DMAC_TO_MEM 0
static uint16_t *reg16(unsigned reg) {
  if(reg == SMAP_R_RXFIFO_RD_PTR) return &pointer_register;
  half_register = fifo[position] | ((unsigned)fifo[position+1] << 8);
  position += 4; /* FIFO advances by one word */
  return &half_register;
}
static uint32_t reg32(unsigned reg) {
  (void)reg; uint32_t word; memcpy(&word,fifo+position,4); position+=4; return word;
}
#define SMAP_REG16(reg) (*reg16(reg))
#define SMAP_REG32(reg) reg32(reg)
static void dev9DmaTransfer(int channel, void *dst, uint32_t bcr, int direction) {
  (void)channel;(void)direction;
  unsigned bytes = (bcr >> 16) * (bcr & 65535) * 4;
  memcpy(dst,fifo+position,bytes); position+=bytes;
}
'''
tests = r'''
int main(void) {
  for(unsigned i=0;i<sizeof(fifo);i++) fifo[i]=(i*37+11)&255;
  for(unsigned size=1;size<=2048;size++) {
    memset(output,0xcc,sizeof(output)); position=0;
    smap_fifo_read(0,output,size);
    if(memcmp(output,fifo,size)) {fprintf(stderr,"lost bytes in %u-byte transfer\n",size);return 1;}
    assert(output[size]==0xcc);
  }
  puts("All 1..2048-byte SMAP transfers preserve tails and buffer bounds");
  return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'receive.c').write_text(harness + function + tests)
    subprocess.run(['cc', '-std=gnu11', '-fsanitize=address,undefined', str(path / 'receive.c'), '-o', str(path / 'receive')], check=True)
    subprocess.run([str(path / 'receive')], check=True)
