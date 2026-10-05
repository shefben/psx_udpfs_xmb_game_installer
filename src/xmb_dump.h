#ifndef PSXI_XMB_DUMP_H
#define PSXI_XMB_DUMP_H

/* Diagnostics: copy what the XMB reads from every PP. partition to
 * mass0:/xmb-dump/ (read-only on the HDD): partitions.txt (all
 * partitions, type, size, and each PP. partition's APA type/size/start
 * and file list), <name>/osd_header_0x1000.bin (first 128 KiB of the
 * OSD header area) and <name>/files/ (files up to 1 MiB whole, larger
 * ones their first 4 KiB). Returns >= 0 on success, *parts = number of
 * PP. partitions dumped. */
int xmb_dump_to_usb(int *parts);

#endif
