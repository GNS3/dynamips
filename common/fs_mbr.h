/** @file
 * @brief Master Boot Record
 *
 * Based on http://thestarman.pcministry.com/asm/mbr/PartTables.htm
 */

/*
 * Copyright (c) 2014 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#ifndef FS_MBR__
#define FS_MBR__

#include "dynamips_common.h"

#define MBR_CYLINDER_MIN 0
#define MBR_CYLINDER_MAX 1023
#define MBR_HEAD_MIN 0
#define MBR_HEAD_MAX 254
#define MBR_SECTOR_MIN 1
#define MBR_SECTOR_MAX 63

#define MBR_PARTITION_BOOTABLE   0x80

#define MBR_PARTITION_TYPE_FAT16 0x04

#define MBR_SIGNATURE_0  0x55
#define MBR_SIGNATURE_1  0xAA

#define MBR_OFFSET 512 - sizeof(struct mbr_data)

/* A partition of the MBR */
struct mbr_partition {
   m_uint8_t  bootable;
   m_uint8_t  first_chs[3];
   m_uint8_t  type;
   m_uint8_t  last_chs[3];
   m_uint32_t lba;
   m_uint32_t nr_sectors;
} __attribute__((__packed__));

/* The MBR data */
struct mbr_data {
   struct mbr_partition  partition[4];
   m_uint8_t             signature[2];
} __attribute__((__packed__));

void mbr_get_chs(m_uint8_t chs[3], m_uint16_t *cyl, m_uint8_t *head, m_uint8_t *sect);
void mbr_set_chs(m_uint8_t chs[3], m_uint16_t cyl, m_uint8_t head, m_uint8_t sect);
int  mbr_write_fd(int fd, struct mbr_data *mbr);
int  mbr_read_fd(int fd, struct mbr_data *mbr);

#endif
