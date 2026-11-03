/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_IMAGE_H
#define LILAC_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#define LILAC_IMAGE_MAGIC 0x31474d49434c494cULL /* LILCIMG1 */
#define LILAC_IMAGE_VERSION 1u
#define LILAC_IMAGE_MAX_EXTENTS 4096u
#define LILAC_IMAGE_MINOR 129u
struct lilac_image_extent { uint64_t logical, physical, blocks; };
struct lilac_image_map {
    uint64_t magic, image_bytes, userdata_first, userdata_last;
    uint32_t version, count, gpt_crc, first_crc, last_crc, reserved;
    struct lilac_image_extent extent[];
};
int lilac_image_validate(const struct lilac_image_map *map,size_t bytes,
    uint64_t first,uint64_t last,uint32_t gpt_crc);
int lilac_image_translate(const struct lilac_image_map *map,uint64_t block,uint64_t *physical);
#endif
