#pragma once
#define SIF_RPC_M_NOWAIT 1
typedef struct { void *server; int id; unsigned long long done_at; } SifRpcClientData_t;
int SifBindRpc(SifRpcClientData_t *cd, int sid, int mode);
int SifCallRpc(SifRpcClientData_t *cd, int func, int mode, void *send, int ssize, void *recv, int rsize, void *endf, void *arg);
int SifCheckStatRpc(SifRpcClientData_t *cd);
void SifWriteBackDCache(void *p, int size);
