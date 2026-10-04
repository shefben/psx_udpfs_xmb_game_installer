#ifndef PSXI_NETWORK_H
#define PSXI_NETWORK_H

/* Brings up smap + ministack (static IP) + udpfs_ioman and tracks the
 * NETWORK_* state in g_app. Discovery happens inside udpfs_ioman's
 * device init; failure leaves the state at NETWORK_ERROR. */
void network_start(void);

/* Controlled restart (e.g. after changing the IP): full IOP reboot. */
void network_restart(void);

/* One-line human status, e.g. "NETWORK_READY 192.168.1.10". */
const char *network_status_line(void);

#endif
