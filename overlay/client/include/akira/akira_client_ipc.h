#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <switch.h>
#include <akira/ipc.h>

bool akiraIpcRunning(void);
Result akiraIpcInitialize(void);
void akiraIpcExit(void);
Result akiraIpcGetApiVersion(u32* out_ver);
Result akiraIpcIsStreamActive(u8* out_active);
Result akiraIpcGetStreamStats(AkiraStreamStats* out_stats);

#ifdef __cplusplus
}
#endif
