/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LILAC_RPM_API_H
#define LILAC_RPM_API_H
#include <stdbool.h>
#include <stdint.h>
/* Caller serializes all calls and verifies the MSM8998 RPM device-tree
 * resources and version-0 RPM message protocol before connecting. A failed
 * connection or vote is terminal for this boot; no retry or implicit release
 * is safe when the remote response was lost. Level zero requests release. */
bool lilac_rpm_connect(bool verified);
bool lilac_rpm_gpu_vote(uint32_t level);
#endif
