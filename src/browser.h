#ifndef PSXI_BROWSER_H
#define PSXI_BROWSER_H

/* Game browsers (plan sections 9, 29, 30). */

#define USB_ROOT "mass0:/"

/* udpfsd's folders; requires NETWORK_READY. */
void browser_run(void);

/* First USB drive (FAT32/exFAT via bdm): .iso and raw .zso files. */
void browser_run_usb(void);

/* Homebrew .ELF files, starting in the server's (usb 0) or the USB
 * drive's (usb 1) APPS folder; the game browsers list them too. */
void browser_run_apps(int usb);

#endif
