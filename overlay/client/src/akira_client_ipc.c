#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <switch.h>
#include <string.h>
#include <stdatomic.h>
#include <akira/akira_client_ipc.h>

static Service g_akiraSrv;
static atomic_size_t g_refCnt;

bool akiraIpcRunning(void)
{
    if (serviceIsActive(&g_akiraSrv))
        return true;

    Handle handle;
    bool running = R_FAILED(smRegisterService(&handle, smEncodeName(AKIRA_IPC_SERVICE_NAME), false, 1));
    if (!running)
        smUnregisterService(smEncodeName(AKIRA_IPC_SERVICE_NAME));
    return running;
}

Result akiraIpcInitialize(void)
{
    g_refCnt++;

    if (serviceIsActive(&g_akiraSrv))
        return 0;

    Result rc = smGetService(&g_akiraSrv, AKIRA_IPC_SERVICE_NAME);

    if (R_FAILED(rc))
        akiraIpcExit();

    return rc;
}

void akiraIpcExit(void)
{
    if (--g_refCnt == 0)
    {
        serviceClose(&g_akiraSrv);
    }
}

Result akiraIpcGetApiVersion(u32* out_ver)
{
    return serviceDispatchOut(&g_akiraSrv, AkiraIpcCmd_GetApiVersion, *out_ver);
}

Result akiraIpcIsStreamActive(u8* out_active)
{
    return serviceDispatchOut(&g_akiraSrv, AkiraIpcCmd_IsStreamActive, *out_active);
}

Result akiraIpcGetStreamStats(AkiraStreamStats* out_stats)
{
    return serviceDispatch(&g_akiraSrv, AkiraIpcCmd_GetStreamStats,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = { { out_stats, sizeof(AkiraStreamStats) } },
    );
}
