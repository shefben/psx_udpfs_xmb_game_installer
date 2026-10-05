#ifndef PSXI_NETWORK_H
#define PSXI_NETWORK_H

/* Brings up smap + ministack (static IP) + udpfs_ioman and tracks the
 * NETWORK_* state in g_app. Runs in the background: returns at once with
 * the state NETWORK_DISCOVERING, which later becomes NETWORK_READY or
 * NETWORK_ERROR (ui_wake() is called on each change). Discovery happens
 * inside udpfs_ioman's device init. */
void network_start(void);

/* 1 while the background start-up is still running. */
int network_busy(void);

/* Block (with a short notice) until the background start-up is done.
 * Required before anything that resets the IOP or leaves the app. */
void network_wait_idle(void);

/* Controlled restart (e.g. after changing the IP): full IOP reboot. */
void network_restart(void);

/* Message for flows that need the server while it is not ready. */
const char *network_not_ready_text(void);

/* One-line human status, e.g. "NETWORK_READY 192.168.1.10". */
const char *network_status_line(void);

#endif
