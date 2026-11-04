/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LILAC_A540_CONTEXT_H
#define LILAC_A540_CONTEXT_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct lilac_gpu_context;
struct lilac_gpu_context *lilac_gpu_context_create(void);
bool lilac_gpu_context_retire(struct lilac_gpu_context *);
uint32_t lilac_gpu_context_last(struct lilac_gpu_context *);
bool lilac_gpu_context_failed(struct lilac_gpu_context *);
bool lilac_gpu_poll_fault(void);
bool lilac_gpu_operational(void);
bool lilac_gpu_fence_failed(uint32_t);
void lilac_gpu_context_destroy(struct lilac_gpu_context *);
uint64_t lilac_gpu_context_address(struct lilac_gpu_context *, uint64_t);
void lilac_gpu_context_address_free(struct lilac_gpu_context *, uint64_t, uint64_t);
bool lilac_gpu_context_map(struct lilac_gpu_context *, uint64_t, uint64_t, bool);
void lilac_gpu_context_unmap(struct lilac_gpu_context *, uint64_t, uint64_t);
uint32_t lilac_gpu_context_submit(struct lilac_gpu_context *, const uint64_t *, const uint32_t *, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
