/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_DISPLAY_API_H
#define LILAC_DISPLAY_API_H
#define LILAC_DISPLAY_STATUS_SELECTOR 127U
#define LILAC_DISPLAY_API_VERSION 1U
#define LILAC_DISPLAY_STATUS_COUNT 16U
/* Scalars: version, shown, panel width/height/depth, packed logical size
 * (width low32, height high32), packed settled count/filter mode (low/high32),
 * nine RGB samples at the centres of a 3x3 grid. Fits IOKit's 16-scalar limit.
 * No address or caller-selected framebuffer offset is exposed.
 */
#endif
