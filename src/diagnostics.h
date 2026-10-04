#ifndef PSXI_DIAGNOSTICS_H
#define PSXI_DIAGNOSTICS_H

/* Diagnostics menu: pre-hardware readiness checks (read-only apart
 * from one probe file in the journal directory) and the explicit,
 * opt-in HDD self-test on PP.UDPFS-TEST. */
void flow_diagnostics(void);

#endif
