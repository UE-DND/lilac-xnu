/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include "../../kernel/lilac-gpu-va.h"
int main(void)
{
    struct lilac_gpu_va v = {0};
    uint64_t a = lilac_gpu_va_alloc(&v,1), b = lilac_gpu_va_alloc(&v,65537);
    assert(a == LILAC_GPU_VA_START && b == a+65536);
    assert(lilac_gpu_va_contains(&v,b,65537));
    assert(!lilac_gpu_va_free(&v,b+65536,65536));
    assert(!lilac_gpu_va_free(&v,b,65536));
    assert(!lilac_gpu_va_free(&v,UINT64_MAX,65536));
    assert(!lilac_gpu_va_contains(&v,a,UINT64_MAX));
    assert(lilac_gpu_va_free(&v,b,65537));
    assert(!lilac_gpu_va_free(&v,b,65537));
    assert(!lilac_gpu_va_contains(&v,b,1));
    assert(lilac_gpu_va_alloc(&v,65537) == b);
    assert(lilac_gpu_va_free(&v,a,1));
    assert(lilac_gpu_va_free(&v,b,65537));
    uint64_t size = LILAC_GPU_VA_END-LILAC_GPU_VA_START;
    assert(lilac_gpu_va_alloc(&v,size) == a);
    assert(!lilac_gpu_va_alloc(&v,1));
    assert(lilac_gpu_va_contains(&v,LILAC_GPU_VA_END-1,1));
    assert(!lilac_gpu_va_contains(&v,LILAC_GPU_VA_END-1,2));
    assert(lilac_gpu_va_free(&v,a,size));
    assert(!lilac_gpu_va_alloc(&v,UINT64_MAX));
    puts("PASS: GPU VA fragmentation, exhaustion, bounds, interior and double frees");
}
