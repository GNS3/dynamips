/** @file
 * @brief Cisco NVRAM filesystem.
 *
 * Format was inferred by analysing the NVRAM data after changing/erasing stuff.
 * All data is big endian.
 *
 * Based on the platforms c1700/c2600/c2692/c3600/c3725/c3745/c7200/c6msfc1.
 */

/*
 * Copyright (c) 2013 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#ifndef FS_NVRAM_H__
#define FS_NVRAM_H__

#include "utils.h"


///////////////////////////////////////////////////////////
// Filesystem


/** Size of a sector. */
#define FS_NVRAM_SECTOR_SIZE   0x400

/** Sector contains the start of the file .*/
#define FS_NVRAM_FLAG_FILE_START   0x01

/** Sector contains the end of the file. */
#define FS_NVRAM_FLAG_FILE_END     0x02

/** File does not have read or write permission. */
#define FS_NVRAM_FLAG_FILE_NO_RW

#define FS_NVRAM_MAGIC_FILESYSTEM       0xF0A5
#define FS_NVRAM_MAGIC_STARTUP_CONFIG   0xABCD
#define FS_NVRAM_MAGIC_PRIVATE_CONFIG   0xFEDC
#define FS_NVRAM_MAGIC_FILE_SECTOR      0xDCBA

/** Data is not compressed. */
#define FS_NVRAM_FORMAT_RAW   1

/** Data is compressed in .Z file format. */
#define FS_NVRAM_FORMAT_LZC   2

/** Magic not found - custom errno code. */
#define FS_NVRAM_ERR_NO_MAGIC   ( - FS_NVRAM_MAGIC_FILESYSTEM )

/** Backup data doesn't match. */
#define FS_NVRAM_ERR_BACKUP_MISSMATCH   ( FS_NVRAM_ERR_NO_MAGIC - 1 )

/** Invalid address found in filesystem. */
#define FS_NVRAM_ERR_INVALID_ADDRESS   ( FS_NVRAM_ERR_NO_MAGIC - 2 )

/* Size of blocks in a NVRAM filesystem with backup (total size is 0x4C000 in c3745) */
#define FS_NVRAM_NORMAL_FILESYSTEM_BLOCK1   0x20000
#define FS_NVRAM_BACKUP_FILESYSTEM_BLOCK1   0x1C000


///////////////////////////////////////////////////////////
// Optional flags for open


/** Create NVRAM filesystem if no magic. */
#define FS_NVRAM_FLAG_OPEN_CREATE       0x0001

/** Don't scale byte offsets. (default, ignored) */
#define FS_NVRAM_FLAG_NO_SCALE          0x0010

/** Scale byte offsets by 4. */
#define FS_NVRAM_FLAG_SCALE_4           0x0020

/** Align the private-config header to 4 bytes with a padding of 7/6/5/0 bytes. (default, ignored) */ 
#define FS_NVRAM_FLAG_ALIGN_4_PAD_8     0x0040

/** Align the private-config header to 4 bytes with a padding of 3/2/1/0 bytes. */ 
#define FS_NVRAM_FLAG_ALIGN_4_PAD_4     0x0080

/** Has a backup filesystem.
 * Data is not continuous:
 *   up to 0x20000 bytes of the normal filesystem;
 *   up to 0x1C000 bytes of the backup filesystem;
 *   rest of normal filesystem;
 *   rest of backup filesystem.
 */
#define FS_NVRAM_FLAG_WITH_BACKUP       0x0100

/** Use addresses relative to the the end of the filesystem magic. (default, ignored)
 * Add 8 to get the raw offset.
 */
#define FS_NVRAM_FLAG_ADDR_RELATIVE     0x0200

/** Use absolute addresses.
 * The base address of the filesystem is the addr argument.
 */
#define FS_NVRAM_FLAG_ADDR_ABSOLUTE     0x0400

/** Value of unk1 is set to 0x0C04. (default, ignored) */
#define FS_NVRAM_FLAGS_UNK1_0C04        0x0800

/** Value of unk1 is set to 0x0C03. */
#define FS_NVRAM_FLAGS_UNK1_0C03        0x1000

/** Value of unk1 is set to 0x0C01. */
#define FS_NVRAM_FLAGS_UNK1_0C01        0x2000

#define FS_NVRAM_FORMAT_MASK            0x3FF0


/** Default filesystem format. (default, ignored) */
#define FS_NVRAM_FORMAT_DEFAULT       ( FS_NVRAM_FLAG_NO_SCALE | FS_NVRAM_FLAG_ALIGN_4_PAD_8 | FS_NVRAM_FLAG_ADDR_RELATIVE | FS_NVRAM_FLAGS_UNK1_0C04 )

/** Filesystem format for the c2600 platform. */
#define FS_NVRAM_FORMAT_SCALE_4       ( FS_NVRAM_FLAG_SCALE_4  | FS_NVRAM_FLAG_ALIGN_4_PAD_8 | FS_NVRAM_FLAG_ADDR_RELATIVE | FS_NVRAM_FLAGS_UNK1_0C03 )

/** Filesystem format for the c3725/c3745 platforms. */
#define FS_NVRAM_FORMAT_WITH_BACKUP   ( FS_NVRAM_FLAG_NO_SCALE | FS_NVRAM_FLAG_ALIGN_4_PAD_4 | FS_NVRAM_FLAG_ADDR_RELATIVE | FS_NVRAM_FLAGS_UNK1_0C04 | FS_NVRAM_FLAG_WITH_BACKUP )

/** Filesystem format for the c7000 platform. */
#define FS_NVRAM_FORMAT_ABSOLUTE      ( FS_NVRAM_FLAG_NO_SCALE | FS_NVRAM_FLAG_ALIGN_4_PAD_4 | FS_NVRAM_FLAG_ADDR_ABSOLUTE | FS_NVRAM_FLAGS_UNK1_0C04 )

/** Filesystem format for the c6msfc1 platform. */
#define FS_NVRAM_FORMAT_ABSOLUTE_C6   ( FS_NVRAM_FLAG_NO_SCALE | FS_NVRAM_FLAG_ALIGN_4_PAD_4 | FS_NVRAM_FLAG_ADDR_ABSOLUTE | FS_NVRAM_FLAGS_UNK1_0C01 )


///////////////////////////////////////////////////////////
// Flags for verify

/** Verify backup data. */
#define FS_NVRAM_VERIFY_BACKUP   0x01

/** Verify config data. */
#define FS_NVRAM_VERIFY_CONFIG   0x02

// TODO Verify file data.
//#define FS_NVRAM_VERIFY_FILES    0x04

/** Verify everything. */
#define FS_NVRAM_VERIFY_ALL      0x07


///////////////////////////////////////////////////////////


/** Header of the NVRAM filesystem.
 * When empty, only this magic and the checksum are filled.
 * @see nvram_header_startup_config
 * @see nvram_header_private_config
 */
struct fs_nvram_header {
   /** Padding. */
   u_char   padding[6];

   /** Magic value 0xF0A5. */
   m_uint16_t   magic;
   // Following data:
   //  - nvram_header_startup_config
   //  - startup-config data
   //  - padding to align the next header to a multiple of 4
   //  - nvram_header_private_config
   //  - private-config data
   //  - padding till end of sector
   //  - the next 2 sectors are reserved for expansion of config files
   //  - the rest of sectors are for normal files
} __attribute__((__packed__));


/** Header of special file startup-config.
 * @see nvram_header
 */
struct fs_nvram_header_startup_config {
   /** Magic value 0xABCD. */
   m_uint16_t   magic;

   /** Format of the data.
    * 0x0001 - raw data;
    * 0x0002 - .Z compressed (12 bits);
    */
   m_uint16_t   format;

   /** Checksum of filesystem data. (all data after the filesystem magic) */
   m_uint16_t   checksum;

   /** 0x0C04 - maybe maximum amount of free space that will be reserved? */
   m_uint16_t   unk1;

   /** Address of the data. */
   m_uint32_t   start;

   /** Address right after the data. */
   m_uint32_t   end;

   /** Length of block.  */
   m_uint32_t   len;

   /** 0x00000000 */
   m_uint32_t   unk2;

   /** 0x00000000 if raw data, 0x00000001 if compressed */
   m_uint32_t   unk3;

   /** 0x0000 if raw data, 0x0001 if compressed */
   m_uint16_t   unk4;

   /** 0x0000 */
   m_uint16_t   unk5;

   /** Length of uncompressed data, 0 if raw data. */
   m_uint32_t   uncompressed_len;

   // startup-config data comes after this header
} __attribute__((__packed__));


/** Header of special file private-config.
 * @see nvram_header
 */
struct fs_nvram_header_private_config {
   /** Magic value 0xFEDC. */
   m_uint16_t   magic;

   /** Format of the file.
    * 0x0001 - raw data;
    */
   m_uint16_t   format;

   /** Address of the data. */
   m_uint32_t   start;

   /** Address right after the data. */
   m_uint32_t   end;

   /** Length of block.  */
   m_uint32_t   len;

   // private-config data comes after this header
} __attribute__((__packed__));


/** Sector containing file data. */
struct fs_nvram_file_sector {
   /** Magic value 0xDCBA */
   m_uint16_t   magic;

   /** Next sector with data, 0 by default */
   m_uint16_t   next_sector;

   /** Flags.
    * @see FS_NVRAM_FLAG_FILE_START
    * @see FS_NVRAM_FLAG_FILE_END
    * @see FS_NVRAM_FLAG_FILE_NO_RW
    */
   m_uint16_t   flags;

   /** Amount of data in this sector. */
   m_uint16_t   length;

   /** File name, always NUL-terminated. */
   char         filename[24];

   /** File data. */
   u_char       data[992];
} __attribute__((__packed__));


typedef struct fs_nvram fs_nvram_t;


/* Functions */
fs_nvram_t *fs_nvram_open(u_char *base, size_t len, m_uint32_t addr, u_int flags);
void fs_nvram_close(fs_nvram_t *fs);
int fs_nvram_read_config(fs_nvram_t *fs, u_char **startup_config, size_t *startup_len, u_char **private_config, size_t *private_len);
int fs_nvram_write_config(fs_nvram_t *fs, const u_char *startup_config, size_t startup_len, const u_char *private_config, size_t private_len);
size_t fs_nvram_num_sectors(fs_nvram_t *fs);
// TODO read/write file sectors
int fs_nvram_verify(fs_nvram_t *fs, u_int what);

#endif
