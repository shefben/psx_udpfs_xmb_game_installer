#ifndef PSXI_BROWSER_H
#define PSXI_BROWSER_H

/* Game browsers (plan sections 9, 29, 30). */

#define USB_ROOT "mass0:/"

/* udpfsd's folders; requires NETWORK_READY. */
void browser_run(void);

/* First USB drive (FAT32/exFAT via bdm): .iso and raw .zso files. */
void browser_run_usb(void);

#endif
