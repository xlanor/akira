#include "core/thread_affinity.h"
#include <switch.h>
#include <chiaki/thread.h>
#include <wg_thread.h>
#include <borealis.hpp>

static uint64_t g_core_mask = 0;
static int g_num_cores = 0;
static int g_available_cores[4] = {-1, -1, -1, -1};
static bool g_affinity_init = false;
static bool g_affinity_enabled = false;

typedef enum {
	THREAD_PURPOSE_NETWORK = 0,
	THREAD_PURPOSE_AUDIO = 1,
	THREAD_PURPOSE_SESSION = 2,
	THREAD_PURPOSE_DEFAULT = 3
} ThreadPurpose;

static void affinity_init(void)
{
	if(g_affinity_init)
		return;
	Result rc = svcGetInfo(&g_core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
	if(R_FAILED(rc))
		g_core_mask = 0xF;
	g_num_cores = __builtin_popcountll(g_core_mask);
	int idx = 0;
	for(int i = 0; i < 4 && idx < g_num_cores; i++)
	{
		if(g_core_mask & (1ULL << i))
			g_available_cores[idx++] = i;
	}
	g_affinity_init = true;
}

static ThreadPurpose get_purpose_for_chiaki_thread(ChiakiThreadName name)
{
	switch(name)
	{
		case CHIAKI_THREAD_NAME_CTRL:
		case CHIAKI_THREAD_NAME_CONGESTION:
		case CHIAKI_THREAD_NAME_DISCOVERY:
		case CHIAKI_THREAD_NAME_DISCOVERY_SVC:
		case CHIAKI_THREAD_NAME_TAKION:
		case CHIAKI_THREAD_NAME_TAKION_SEND:
		case CHIAKI_THREAD_NAME_RUDP_SEND:
		case CHIAKI_THREAD_NAME_HOLEPUNCH:
			return THREAD_PURPOSE_NETWORK;
		case CHIAKI_THREAD_NAME_FEEDBACK:
			return THREAD_PURPOSE_AUDIO;
		case CHIAKI_THREAD_NAME_SESSION:
		case CHIAKI_THREAD_NAME_REGIST:
		case CHIAKI_THREAD_NAME_GKCRYPT:
			return THREAD_PURPOSE_SESSION;
		default:
			return THREAD_PURPOSE_DEFAULT;
	}
}

static ThreadPurpose get_purpose_for_wg_thread(WgThreadName name)
{
	switch(name)
	{
		case WG_THREAD_NAME_RELAY:
		case WG_THREAD_NAME_RECV:
		case WG_THREAD_NAME_SEND:
			return THREAD_PURPOSE_NETWORK;
		default:
			return THREAD_PURPOSE_DEFAULT;
	}
}

static ThreadPurpose get_purpose_for_akira_thread(AkiraThreadName name)
{
	switch(name)
	{
		case AKIRA_THREAD_NAME_LWIP_LOOP:
			return THREAD_PURPOSE_NETWORK;
		case AKIRA_THREAD_NAME_MAIN:
		case AKIRA_THREAD_NAME_BENCHMARK:
		case AKIRA_THREAD_NAME_CONNECTION:
			return THREAD_PURPOSE_SESSION;
		default:
			return THREAD_PURPOSE_DEFAULT;
	}
}

static int get_core_for_purpose(ThreadPurpose purpose)
{
	if(!g_affinity_init)
		affinity_init();
	if(g_num_cores == 1)
		return g_available_cores[0];
	if(g_num_cores == 2)
	{
		int mapping[4] = {0, 1, 0, 1};
		return g_available_cores[mapping[purpose]];
	}
	if(g_num_cores == 3)
	{
		int mapping[4] = {0, 1, 2, 2};
		return g_available_cores[mapping[purpose]];
	}
	return g_available_cores[purpose];
}

static void apply_affinity(int core, const char *debug_name)
{
	if(core < 0)
		return;
	Handle handle = threadGetCurHandle();
	uint32_t mask = (1U << core);
	svcSetThreadCoreMask(handle, core, mask);
	brls::Logger::info("Thread '{}' pinned to core {}", debug_name, core);
}

static void chiaki_thread_affinity_cb(ChiakiThreadName name, void *user)
{
	(void)user;
	static const char *names[] = {
		"ctrl", "congestion", "discovery", "discovery_svc",
		"takion", "takion_send", "rudp_send", "holepunch",
		"feedback", "session", "regist", "gkcrypt"
	};
	ThreadPurpose purpose = get_purpose_for_chiaki_thread(name);
	int core = get_core_for_purpose(purpose);
	apply_affinity(core, names[name]);
}

static void wg_thread_affinity_cb(WgThreadName name, void *user)
{
	(void)user;
	ThreadPurpose purpose = get_purpose_for_wg_thread(name);
	int core = get_core_for_purpose(purpose);
	if(core < 0)
		return;
	Handle handle = threadGetCurHandle();
	uint32_t mask = (1U << core);
	svcSetThreadCoreMask(handle, core, mask);
}

void akira_thread_set_affinity(AkiraThreadName name)
{
	if(!g_affinity_enabled)
		return;
	static const char *names[] = {"main", "lwip_loop", "benchmark", "connection"};
	ThreadPurpose purpose = get_purpose_for_akira_thread(name);
	int core = get_core_for_purpose(purpose);
	apply_affinity(core, names[name]);
}

void chiaki_thread_affinity_init(void)
{
	affinity_init();
	g_affinity_enabled = true;
	chiaki_thread_set_affinity_cb(chiaki_thread_affinity_cb, NULL);
	wg_thread_set_affinity_cb(wg_thread_affinity_cb, NULL);
	brls::Logger::info("Thread affinity initialized: {} cores available, mask=0x{:X}",
		g_num_cores, g_core_mask);
}

uint64_t chiaki_thread_affinity_get_mask(void)
{
	if(!g_affinity_init)
		affinity_init();
	return g_core_mask;
}

int chiaki_thread_affinity_get_num_cores(void)
{
	if(!g_affinity_init)
		affinity_init();
	return g_num_cores;
}
