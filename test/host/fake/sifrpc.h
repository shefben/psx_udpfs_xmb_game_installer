#ifndef FAKE_SIFRPC_H
#define FAKE_SIFRPC_H
typedef struct {
  void *server;
} SifRpcClientData_t;
int SifBindRpc(SifRpcClientData_t *cd, int rpc, int mode);
int SifCallRpc(SifRpcClientData_t *cd, int fno, int mode, void *send, int ssize, void *recv,
               int rsize, void *end, void *end_arg);
void nopdelay(void);
#endif
