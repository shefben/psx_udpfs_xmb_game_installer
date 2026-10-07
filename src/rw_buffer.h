#ifndef PSXI_RW_BUFFER_H
#define PSXI_RW_BUFFER_H

/* fileXio's IOP-side transfer buffer (pure; the RPC is passed in).
 *
 * fileXio splits every EE read/write into iomanX calls of at most this
 * size. Its default (16 KiB) turns each 1 MiB install read into 64
 * UDPFS round trips of 16 KiB and each HDD write into 16 KiB pieces;
 * the patched udpfs_ioman fetches 128 KiB per request (UDPFS_MAX_READ,
 * patches/neutrino/0003; udpfsd's read buffer is 128 KiB), so 128 KiB
 * cuts the round trips by 8. It is also the size of one LZ4 wire frame
 * request (source_wire.h). */

#define RW_BUFFER_FAST (128 * 1024)
#define RW_BUFFER_MID (64 * 1024)
#define RW_BUFFER_DEFAULT (16 * 1024) /* FILEXIO_DEFAULT_RW_BUFFER_SIZE */

/* Calls set(size) (fileXioSetRWBufferSize) with RW_BUFFER_FAST, then
 * the smaller sizes if the IOP heap could not hold it: a failed set
 * frees the old buffer, so fileXio must never be left without one.
 * Returns the size in use, or 0 if none could be allocated. */
int rw_buffer_setup(int (*set)(int size));

#endif
