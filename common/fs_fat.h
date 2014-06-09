/** @file
 * @brief FAT filesystem.
 *
 * Based on http://www.win.tue.nl/~aeb/linux/fs/fat/fat-1.html
 */

/*
 * Copyright (c) 2014 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#ifndef FS_FAT_H__
#define FS_FAT_H__

#include "dynamips_common.h"

#define FS_FAT_SECTOR_SIZE  512

int fs_fat_format16(int fd, m_uint32_t begin_lba, m_uint32_t nr_sectors, 
                    m_uint16_t sects_per_track, m_uint16_t heads,
                    const char *volume_name);

#endif
