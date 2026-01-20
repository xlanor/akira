#ifndef CHIAKI_NX_THREAD_AFFINITY_H
#define CHIAKI_NX_THREAD_AFFINITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	AKIRA_THREAD_NAME_MAIN,
	AKIRA_THREAD_NAME_LWIP_LOOP,
	AKIRA_THREAD_NAME_BENCHMARK,
	AKIRA_THREAD_NAME_CONNECTION
} AkiraThreadName;

void chiaki_thread_affinity_init(void);
void akira_thread_set_affinity(AkiraThreadName name);
uint64_t chiaki_thread_affinity_get_mask(void);
int chiaki_thread_affinity_get_num_cores(void);

#ifdef __cplusplus
}
#endif

#endif
