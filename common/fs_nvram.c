/** @file
 * @brief Cisco NVRAM filesystem.
 */

/*
 * Copyright (c) 2013 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h> // offsetof
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_nvram.h"

#define DEBUG_BACKUP 0



/** NVRAM filesystem. */
struct fs_nvram {
   u_char *base;
   size_t len;
   m_uint32_t addr; /// start address of the filesystem (for absolute addresses)
   u_int flags; /// filesystem flags
   u_int shift; /// scale byte offsets
   u_int padding; /// base padding value
   size_t backup; /// start offset of the backup filesystem

   m_uint8_t (*read_byte)(fs_nvram_t *fs, u_int offset);
   void (*write_byte)(fs_nvram_t *fs, u_int offset, m_uint8_t val);
};




//=========================================================
// Auxiliary



/** Convert a 16 bit value from big endian to native. */
static inline void be_to_native16(m_uint16_t *val)
{
   union {
      m_uint16_t val;
      m_uint8_t b[2];
   } u;
   u.val = *val;
   *val = (((m_uint16_t)u.b[0] << 8) | u.b[1]);
}


/** Convert a 32 bit value from big endian to native. */
static inline void be_to_native32(m_uint32_t *val)
{
   union {
      m_uint32_t val;
      m_uint8_t b[4];
   } u;
   u.val = *val;
   *val = (((m_uint32_t)u.b[0] << 24) | ((m_uint32_t)u.b[1] << 16) | ((m_uint32_t)u.b[2] << 8) | u.b[3]);
}


/** Convert startup-config header values from big endian to native. */
static void be_to_native_header_startup(struct fs_nvram_header_startup_config *head)
{
   be_to_native16(&head->magic);
   be_to_native16(&head->format);
   be_to_native16(&head->checksum);
   be_to_native16(&head->unk1);
   be_to_native32(&head->start);
   be_to_native32(&head->end);
   be_to_native32(&head->len);
   be_to_native32(&head->unk2);
   be_to_native32(&head->unk3);
   be_to_native16(&head->unk4);
   be_to_native16(&head->unk5);
   be_to_native32(&head->uncompressed_len);
}


/** Convert private-config header values from big endian to native. */
static void be_to_native_header_private(struct fs_nvram_header_private_config *head)
{
   be_to_native16(&head->magic);
   be_to_native16(&head->format);
   be_to_native32(&head->start);
   be_to_native32(&head->end);
   be_to_native32(&head->len);
}



/** Convert a 16 bit value from native to big endian. */
static inline void native_to_be16(m_uint16_t *val)
{
   union {
      m_uint16_t val;
      m_uint8_t b[2];
   } u;
   u.b[0] = (m_uint8_t)(*val >> 8);
   u.b[1] = (m_uint8_t)(*val & 0xFF);
   *val = u.val;
}


/** Convert a 32 bit value from native to big endian. */
static inline void native_to_be32(m_uint32_t *val)
{
   union {
      m_uint32_t val;
      m_uint8_t b[4];
   } u;
   u.b[0] = (m_uint8_t)(*val >> 24);
   u.b[1] = (m_uint8_t)(*val >> 16);
   u.b[2] = (m_uint8_t)(*val >> 8);
   u.b[3] = (m_uint8_t)(*val & 0xFF);
   *val = u.val;
}


/** Convert startup-config header values from native to big endian. */
static void native_to_be_header_startup(struct fs_nvram_header_startup_config *head)
{
   native_to_be16(&head->magic);
   native_to_be16(&head->format);
   native_to_be16(&head->checksum);
   native_to_be16(&head->unk1);
   native_to_be32(&head->start);
   native_to_be32(&head->end);
   native_to_be32(&head->len);
   native_to_be32(&head->unk2);
   native_to_be32(&head->unk3);
   native_to_be16(&head->unk4);
   native_to_be16(&head->unk5);
   native_to_be32(&head->uncompressed_len);
}


/** Convert private-config header values from native to big endian. */
static void native_to_be_header_private(struct fs_nvram_header_private_config *head)
{
   native_to_be16(&head->magic);
   native_to_be16(&head->format);
   native_to_be32(&head->start);
   native_to_be32(&head->end);
   native_to_be32(&head->len);
}


/** Uncompress data in .Z file format.
 * Adapted from 7zip's ZDecoder.cpp, which is licensed under LGPL 2.1.
 */
static int uncompress_LZC(const u_char *in_data, u_int in_len, u_char *out_data, u_int out_len)
{
#define LZC_MAGIC_1           0x1F
#define LZC_MAGIC_2           0x9D
#define LZC_NUM_BITS_MASK     0x1F
#define LZC_BLOCK_MODE_MASK   0x80
#define LZC_NUM_BITS_MIN      9
#define LZC_NUM_BITS_MAX      16
   m_uint16_t *parents = NULL;
   m_uint8_t *suffixes = NULL;
   m_uint8_t *stack = NULL;
   int err;

   if (in_len < 3 || (in_data == NULL && in_len > 0) || (out_data == NULL && out_len > 0))
      return EINVAL; // invalid argument

   if (in_data[0] != LZC_MAGIC_1 || in_data[1] != LZC_MAGIC_2)
      return ENOTSUP; // no magic

   int maxbits = in_data[2] & LZC_NUM_BITS_MASK;
   if (maxbits < LZC_NUM_BITS_MIN || maxbits > LZC_NUM_BITS_MAX)
      return ENOTSUP; // maxbits not supported

   m_uint32_t numItems = 1 << maxbits;
   m_uint8_t blockMode = ((in_data[2] & LZC_BLOCK_MODE_MASK) != 0);

   parents = (m_uint16_t *)malloc(numItems * sizeof(*parents)); if (parents == NULL) goto err_memory;
   suffixes = (m_uint8_t *)malloc(numItems * sizeof(*suffixes)); if (suffixes == NULL) goto err_memory;
   stack = (m_uint8_t *)malloc(numItems * sizeof(*stack)); if (stack == NULL) goto err_memory;

   u_int in_pos = 3;
   u_int out_pos = 0;
   int numBits = LZC_NUM_BITS_MIN;
   m_uint32_t head = blockMode ? 257 : 256;

   m_uint8_t needPrev = 0;

   u_int bitPos = 0;
   u_int numBufBits = 0;

   u_char buf[LZC_NUM_BITS_MAX + 4];

   parents[256] = 0;
   suffixes[256] = 0;

   for (;;)
   {
      if (numBufBits == bitPos)
      {
          u_int len = m_min(in_len - in_pos, numBits);
          memcpy(buf, in_data + in_pos, len);
          numBufBits = len << 3;
          bitPos = 0;
          in_pos += len;
      }
      u_int bytePos = bitPos >> 3;
      m_uint32_t symbol = buf[bytePos] | ((m_uint32_t)buf[bytePos + 1] << 8) | ((m_uint32_t)buf[bytePos + 2] << 16);
      symbol >>= (bitPos & 7);
      symbol &= (1 << numBits) - 1;
      bitPos += numBits;
      if (bitPos > numBufBits)
         break;
      if (symbol >= head)
         goto err_data;
      if (blockMode && symbol == 256)
      {
         numBufBits = bitPos = 0;
         numBits = LZC_NUM_BITS_MIN;
         head = 257;
         needPrev = 0;
         continue;
      }
      m_uint32_t cur = symbol;
      int i = 0;
      while (cur >= 256)
      {
         stack[i++] = suffixes[cur];
         cur = parents[cur];
      }
      stack[i++] = (u_char)cur;
      if (needPrev)
      {
         suffixes[head - 1] = (u_char)cur;
         if (symbol == head - 1)
            stack[0] = (u_char)cur;
      }
      do {
         if (out_pos < out_len)
            out_data[out_pos++] = stack[--i];
         else
            i = 0;
      } while (i > 0);
      if (head < numItems)
      {
         needPrev = 1;
         parents[head++] = (m_uint16_t)symbol;
         if (head > ((m_uint32_t)1 << numBits))
         {
            if (numBits < maxbits)
            {
               numBufBits = bitPos = 0;
               numBits++;
            }
         }
      }
      else {
         needPrev = 0;
      }
   }

   free(parents);
   free(suffixes);
   free(stack);
   return 0;
err_memory:
   err = ENOMEM; // out of memory
   goto err_end;
err_data:
   err = -1;//EIO; // invalid data
   goto err_end;
err_end:
   if (parents)
      free(parents);

   if (suffixes)
      free(suffixes);

   if (stack)
      free(stack);

   return err;
}



//=========================================================
// Private



/** Retuns address of the specified filesystem offset */
static inline m_uint32_t fs_nvram_address_of(fs_nvram_t *fs, m_uint32_t offset)
{
   if ((fs->flags & FS_NVRAM_FLAG_ADDR_ABSOLUTE))
      return(fs->addr + offset);
   else
      return(offset - 8);
}


/** Retuns filesystem offset of the specified address */
static inline m_uint32_t fs_nvram_offset_of(fs_nvram_t *fs, m_uint32_t address)
{
   if ((fs->flags & FS_NVRAM_FLAG_ADDR_ABSOLUTE))
      return(address - fs->addr);
   else
      return(address + 8);
}


/** Retuns padding at the specified offset */
static inline u_int fs_nvram_padding_at(fs_nvram_t *fs, u_int offset)
{
   u_int padding = 0;

   if (offset % 4 != 0)
      padding = fs->padding - offset % 4;

   return padding;
}


/** Read a 16-bit value from NVRAM. */
static inline m_uint16_t fs_nvram_read16(fs_nvram_t *fs,u_int offset)
{
   m_uint16_t val;
   val =  fs->read_byte(fs,offset) << 8;
   val |= fs->read_byte(fs,offset+1);
   return(val);
}

/** Write a 16-bit value to NVRAM. */
static void fs_nvram_write16(fs_nvram_t *fs,u_int offset,m_uint16_t val)
{
   fs->write_byte(fs,offset,val >> 8);
   fs->write_byte(fs,offset+1,val & 0xFF);
}


/** Read a 32-bit value from NVRAM. */
static m_uint32_t fs_nvram_read32(fs_nvram_t *fs,u_int offset)
{
   m_uint32_t val;
   val =  fs->read_byte(fs,offset)   << 24;
   val |= fs->read_byte(fs,offset+1) << 16;
   val |= fs->read_byte(fs,offset+2) << 8;
   val |= fs->read_byte(fs,offset+3);
   return(val);
}


/** Write a 32-bit value to NVRAM. */
_unused static void fs_nvram_write32(fs_nvram_t *fs,u_int offset,m_uint32_t val)
{
   fs->write_byte(fs,offset,val >> 24);
   fs->write_byte(fs,offset+1,val >> 16);
   fs->write_byte(fs,offset+2,val >> 8);
   fs->write_byte(fs,offset+3,val & 0xFF);
}


/** Read a buffer from NVRAM. */
static void fs_nvram_memcpy_from(fs_nvram_t *fs, u_int offset, u_char *data, u_int len)
{
   u_int i;

   for (i = 0; i < len; i++) {
      *data = fs->read_byte(fs, offset + i);
      data++;
   }
}


/** Write a buffer to NVRAM. */
static void fs_nvram_memcpy_to(fs_nvram_t *fs, u_int offset, const u_char *data, u_int len)
{
   u_int i;

   for (i = 0; i < len; i++) {
      fs->write_byte(fs, offset + i, *data);
      data++;
   }
}


/** Clear section of NVRAM. */
static void fs_nvram_clear(fs_nvram_t *fs, u_int offset, u_int len)
{
   u_int i;

   for (i = 0; i < len; i++) {
      fs->write_byte(fs, offset + i, 0);
   }
}


/** Update the filesystem checksum. */
static void fs_nvram_update_checksum(fs_nvram_t *fs)
{
   u_int offset, count;
   m_uint32_t sum = 0;

   fs_nvram_write16(fs, sizeof(struct fs_nvram_header) + offsetof(struct fs_nvram_header_startup_config, checksum), 0x0000);

   offset = sizeof(struct fs_nvram_header);
   count = fs->len - offset;
   while (count > 1) {
      sum = sum + fs_nvram_read16(fs, offset);
      offset += 2;
      count -= sizeof(m_uint16_t);
   }

   if (count > 0) 
      sum = sum + ((fs->read_byte(fs, offset)) << 8);

   while (sum >> 16)
      sum = (sum & 0xffff) + (sum >> 16);

   sum = ~sum;

   fs_nvram_write16(fs, sizeof(struct fs_nvram_header) + offsetof(struct fs_nvram_header_startup_config, checksum), sum);
}


/** Read data from NVRAM.*/
static inline u_char *fs_nvram_read_data(fs_nvram_t *fs, u_int offset, u_int len)
{
   u_char *data;

   data = (u_char *)malloc(len + 1);
   if (data == NULL)
      return NULL; // out of memory
 
   fs_nvram_memcpy_from(fs, offset, data, len);
   data[len] = 0;

   return data;
}


/** Create a NVRAM filesystem. */
static void fs_nvram_create(fs_nvram_t *fs)
{
   fs_nvram_clear(fs, 0, fs->len);
   fs_nvram_write16(fs, offsetof(struct fs_nvram_header, magic), FS_NVRAM_MAGIC_FILESYSTEM);
   fs_nvram_write16(fs, sizeof(struct fs_nvram_header) + offsetof(struct fs_nvram_header_startup_config, checksum), 0xFFFF);
}


/** Read a byte from the NVRAM filesystem. */
static m_uint8_t fs_nvram_read_byte(fs_nvram_t *fs, u_int offset)
{
   m_uint8_t *ptr;

   ptr = (m_uint8_t *)fs->base + (offset << fs->shift);
   return(*ptr);
}


/** Write a byte to the NVRAM filesystem. */
static void fs_nvram_write_byte(fs_nvram_t *fs, u_int offset, m_uint8_t val)
{
   m_uint8_t *ptr;

   ptr = (m_uint8_t *)fs->base + (offset << fs->shift);
   *ptr = val;
}


/** Returns the normal offset of the NVRAM filesystem with backup. */
static inline u_int fs_nvram_offset1_with_backup(fs_nvram_t *fs, u_int offset)
{
   if (offset < FS_NVRAM_NORMAL_FILESYSTEM_BLOCK1)
      return(offset << fs->shift);
   else
      return((FS_NVRAM_BACKUP_FILESYSTEM_BLOCK1 + offset) << fs->shift);
}


/** Returns the backup offset of the NVRAM filesystem with backup. */
static inline u_int fs_nvram_offset2_with_backup(fs_nvram_t *fs, u_int offset)
{
   if (offset < FS_NVRAM_BACKUP_FILESYSTEM_BLOCK1)
      return((fs->backup + offset) << fs->shift);
   else
      return((fs->len + offset) << fs->shift);
}


/** Read a byte from the NVRAM filesystem with backup. */
static m_uint8_t fs_nvram_read_byte_with_backup(fs_nvram_t *fs, u_int offset)
{
   m_uint8_t *ptr1;

   ptr1 = (m_uint8_t *)fs->base + fs_nvram_offset1_with_backup(fs, offset);
#if DEBUG_BACKUP
   {
      m_uint8_t *ptr2 = (m_uint8_t *)fs->base + fs_nvram_offset2_with_backup(fs, offset);
      if (*ptr1 != *ptr2)
         fprintf(stderr, "fs_nvram_read_byte_with_backup: data in backup filesystem doesn't match (offset=%u, offset1=%u, offset2=%u, normal=0x%02X, backup=0x%02X)\n",
            offset, fs_nvram_offset1_with_backup(fs, offset), fs_nvram_offset2_with_backup(fs, offset), *ptr1, *ptr2);
   }
#endif

   return(*ptr1);
}


/** Write a byte to the NVRAM filesystem with backup. */
static void fs_nvram_write_byte_with_backup(fs_nvram_t *fs, u_int offset, m_uint8_t val)
{
   m_uint8_t *ptr1;
   m_uint8_t *ptr2;

   ptr1 = (m_uint8_t *)fs->base + fs_nvram_offset1_with_backup(fs, offset);
   ptr2 = (m_uint8_t *)fs->base + fs_nvram_offset2_with_backup(fs, offset);

   *ptr1 = val;
   *ptr2 = val;
}



//=========================================================
// Public



/** Open NVRAM filesystem. Sets errno. */
fs_nvram_t *fs_nvram_open(u_char *base, size_t len, m_uint32_t addr, u_int flags)
{
   struct fs_nvram *fs;
   size_t len_div = 1;

   if ((flags & FS_NVRAM_FLAG_SCALE_4))
      len_div *= 4; // a quarter of the size

   if ((flags & FS_NVRAM_FLAG_WITH_BACKUP))
      len_div *= 2; // half the size is for the backup

   if (base == NULL || len < sizeof(struct fs_nvram_header) * len_div || len % (FS_NVRAM_SECTOR_SIZE * len_div) != 0) {
      errno = EINVAL;
      return NULL; // invalid argument
   }

   fs = (struct fs_nvram*)malloc(sizeof(*fs));
   if (fs == NULL) {
      errno = ENOMEM;
      return NULL; // out of memory
   }

   fs->base       = base;
   fs->len        = len / len_div;
   fs->addr       = addr;
   fs->flags      = flags;
   fs->shift      = (flags & FS_NVRAM_FLAG_SCALE_4) ? 2 : 0;
   fs->padding    = (flags & FS_NVRAM_FLAG_ALIGN_4_PAD_4) ? 4 : 8;
   fs->backup     = (flags & FS_NVRAM_FLAG_WITH_BACKUP) ? m_min(fs->len, FS_NVRAM_NORMAL_FILESYSTEM_BLOCK1) : 0;
   fs->read_byte  = (flags & FS_NVRAM_FLAG_WITH_BACKUP) ? fs_nvram_read_byte_with_backup : fs_nvram_read_byte;
   fs->write_byte = (flags & FS_NVRAM_FLAG_WITH_BACKUP) ? fs_nvram_write_byte_with_backup : fs_nvram_write_byte;

   if (FS_NVRAM_MAGIC_FILESYSTEM != fs_nvram_read16(fs, offsetof(struct fs_nvram_header, magic))) {
      if (!(flags & FS_NVRAM_FLAG_OPEN_CREATE)) {
         fs_nvram_close(fs);
         errno = FS_NVRAM_ERR_NO_MAGIC;
         return NULL; // no magic
      }

      fs_nvram_create(fs);
   }

   errno = 0;
   return fs;
}


/** Close NVRAM filesystem. */
void fs_nvram_close(fs_nvram_t *fs)
{
   if (fs)
      free(fs);
}


/** Read startup-config and/or private-config from NVRAM.
 * Returns 0 on success.
 */
int fs_nvram_read_config(fs_nvram_t *fs, u_char **startup_config, size_t *startup_len, u_char **private_config, size_t *private_len)
{
   int err;
   size_t off;
   u_char *buf;
   struct fs_nvram_header_startup_config startup_head;
   struct fs_nvram_header_private_config private_head;

   if (fs == NULL)
      return(EINVAL); // invalid argument

   // initial values
   if (startup_config)
      *startup_config = NULL;

   if (startup_len)
      *startup_len = 0;

   if (private_config)
      *private_config = NULL;

   if (private_len)
      *private_len = 0;

   // read headers
   off = sizeof(struct fs_nvram_header);
   fs_nvram_memcpy_from(fs, off, (u_char *)&startup_head, sizeof(startup_head));
   be_to_native_header_startup(&startup_head);
   if (FS_NVRAM_MAGIC_STARTUP_CONFIG != startup_head.magic)
      return(0); // done, no startup-config and no private-config

   off = fs_nvram_offset_of(fs, startup_head.end);
   off += fs_nvram_padding_at(fs, off);
   fs_nvram_memcpy_from(fs, off, (u_char *)&private_head, sizeof(private_head));
   be_to_native_header_private(&private_head);

   // read startup-config
   if (FS_NVRAM_FORMAT_RAW == startup_head.format) {
      if (startup_config) {
         off = fs_nvram_offset_of(fs, startup_head.start);
         *startup_config = fs_nvram_read_data(fs, off, startup_head.len);
         if (*startup_config == NULL)
            goto err_memory;
      }

      if (startup_len)
         *startup_len = startup_head.len;
   }
   else if (FS_NVRAM_FORMAT_LZC == startup_head.format) {
      if (startup_config) {
         off = fs_nvram_offset_of(fs, startup_head.start);
         *startup_config = (u_char *)malloc(startup_head.uncompressed_len + 1);
         if (*startup_config == NULL)
            goto err_memory;

         buf = fs_nvram_read_data(fs, off, startup_head.len);
         if (buf == NULL)
            goto err_memory;

         err = uncompress_LZC(buf, startup_head.len, *startup_config, startup_head.uncompressed_len);
         if (err)
            goto err_uncompress;

         (*startup_config)[startup_head.uncompressed_len] = 0;
         free(buf);
      }

      if (startup_len)
         *startup_len = startup_head.uncompressed_len;
   }
   else {
      goto err_format;
   }

   // read private-config
   if (FS_NVRAM_MAGIC_PRIVATE_CONFIG != private_head.magic)
      return(0); // done, no private-config

   if (FS_NVRAM_FORMAT_RAW == private_head.format) {
      if (private_config) {
         off = fs_nvram_offset_of(fs, private_head.start);
         *private_config = fs_nvram_read_data(fs, off, private_head.len);
         if (*private_config == NULL)
            goto err_memory;
      }

      if (private_len)
         *private_len = private_head.len;
   }
   else {
      goto err_format;
   }

   return(0); // done
err_memory:
   err = ENOMEM; // out of memory
   goto err_end;
err_format:
   err = ENOTSUP; // unsupported format
   goto err_end;
err_uncompress:
   free(buf);
   goto err_end;
err_end:
   if (startup_config && *startup_config) {
      free(*startup_config);
      *startup_config = NULL;
   }

   if (startup_len)
      *startup_len = 0;

   if (private_config && *private_config) {
      free(*private_config);
      *private_config = NULL;
   }

   if (private_len)
      *private_len = 0;

   return(err);
}


/** Write startup-config and private-config to NVRAM.
 * Returns 0 on success.
 */
int fs_nvram_write_config(fs_nvram_t *fs, const u_char *startup_config, size_t startup_len, const u_char *private_config, size_t private_len)
{
   size_t len;
   size_t off;
   size_t padding;
   struct fs_nvram_header_startup_config startup_head;
   struct fs_nvram_header_private_config private_head;

   if (fs == NULL || (startup_config == NULL && startup_len > 0) || (private_config == NULL && private_len > 0))
      return(EINVAL); // invalid argument

   // check space and padding
   // XXX ignores normal files in NVRAM
   len = sizeof(struct fs_nvram_header) + sizeof(struct fs_nvram_header_startup_config) + startup_len;
   padding = fs_nvram_padding_at(fs, len);
   len += padding + sizeof(struct fs_nvram_header_private_config) + private_len;
   if (fs->len < len)
      return(ENOSPC); // not enough space

   // old length
   len = sizeof(struct fs_nvram_header);
   if (FS_NVRAM_MAGIC_STARTUP_CONFIG == fs_nvram_read16(fs, len + offsetof(typeof(startup_head), magic))) {
      len += fs_nvram_read32(fs, len + offsetof(typeof(startup_head), len));
      if (len % 4 != 0)
         len += 8 - len % 4;

      if (FS_NVRAM_MAGIC_PRIVATE_CONFIG == fs_nvram_read16(fs, len + offsetof(typeof(private_head), magic)))
         len += fs_nvram_read32(fs, len + offsetof(typeof(private_head), len));
   }

   if (len % FS_NVRAM_SECTOR_SIZE != 0)
      len += FS_NVRAM_SECTOR_SIZE - len % FS_NVRAM_SECTOR_SIZE; // whole sector

   if (len > fs->len)
      len = fs->len; // should never happen

   // prepare headers
   memset(&startup_head, 0, sizeof(startup_head));
   startup_head.magic = FS_NVRAM_MAGIC_STARTUP_CONFIG;
   startup_head.format = FS_NVRAM_FORMAT_RAW;
   startup_head.unk1 = (fs->flags & FS_NVRAM_FLAGS_UNK1_0C01) ? 0x0C01 : (fs->flags & FS_NVRAM_FLAGS_UNK1_0C03) ? 0x0C03 : 0x0C04;
   startup_head.start = fs_nvram_address_of(fs, sizeof(struct fs_nvram_header) + sizeof(struct fs_nvram_header_startup_config));
   startup_head.end = startup_head.start + startup_len;
   startup_head.len = startup_len;

   memset(&private_head, 0, sizeof(private_head));
   private_head.magic = FS_NVRAM_MAGIC_PRIVATE_CONFIG;
   private_head.format = FS_NVRAM_FORMAT_RAW;
   private_head.start = startup_head.end + padding + sizeof(struct fs_nvram_header_private_config);
   private_head.end = private_head.start + private_len;
   private_head.len = private_len;

   native_to_be_header_startup(&startup_head);
   native_to_be_header_private(&private_head);

   // write data
   off = sizeof(struct fs_nvram_header);
   
   fs_nvram_memcpy_to(fs, off, (const u_char *)&startup_head, sizeof(struct fs_nvram_header_startup_config));
   off += sizeof(struct fs_nvram_header_startup_config);
   fs_nvram_memcpy_to(fs, off, startup_config, startup_len);
   off += startup_len;

   fs_nvram_clear(fs, off, padding);
   off += padding;

   fs_nvram_memcpy_to(fs, off, (const u_char *)&private_head, sizeof(struct fs_nvram_header_private_config));
   off += sizeof(struct fs_nvram_header_private_config);
   fs_nvram_memcpy_to(fs, off, private_config, private_len);
   off += private_len;

   if (off < len)
      fs_nvram_clear(fs, off, len - off);

   fs_nvram_update_checksum(fs);

   return(0);
}


/** Returns the number of sectors in the NVRAM filesystem. */
size_t fs_nvram_num_sectors(fs_nvram_t *fs)
{
   if (fs == NULL)
      return(0);

   return( fs->len / FS_NVRAM_SECTOR_SIZE );
}


/** Verify the contents of the filesystem.
 * Returns 0 on success.
 */
int fs_nvram_verify(fs_nvram_t *fs, u_int what)
{
   size_t offset;

   if (fs == NULL)
      return(EINVAL); // invalid argument

   if ((what & FS_NVRAM_VERIFY_BACKUP)) {
      if ((fs->flags & FS_NVRAM_FLAG_WITH_BACKUP)) {
         for (offset = 0; offset < fs->len; offset++) {
            m_uint8_t b1 = fs->base[fs_nvram_offset1_with_backup(fs, offset)];
            m_uint8_t b2 = fs->base[fs_nvram_offset2_with_backup(fs, offset)];
            if (b1 != b2)
               return(FS_NVRAM_ERR_BACKUP_MISSMATCH); // data is corrupted? length is wrong?
         }
      }
   }

   if ((what & FS_NVRAM_VERIFY_CONFIG)) {
      struct fs_nvram_header_startup_config startup_head;
      struct fs_nvram_header_private_config private_head;

      offset = sizeof(struct fs_nvram_header);
      fs_nvram_memcpy_from(fs, offset, (u_char *)&startup_head, sizeof(startup_head));
      be_to_native_header_startup(&startup_head);
      if (FS_NVRAM_MAGIC_STARTUP_CONFIG == startup_head.magic) {
         if (startup_head.end != startup_head.start + startup_head.len || startup_head.len > fs->len)
            return(FS_NVRAM_ERR_INVALID_ADDRESS); // data is corrupted?
         if (startup_head.start < fs->addr || startup_head.end > fs->addr + fs->len)
            return(FS_NVRAM_ERR_INVALID_ADDRESS); // fs->addr has the wrong value?

         offset = fs_nvram_offset_of(fs, startup_head.end);
         offset += fs_nvram_padding_at(fs, offset);
         if (fs->len < offset + sizeof(private_head))
            return(FS_NVRAM_ERR_INVALID_ADDRESS); // data is corrupted?

         fs_nvram_memcpy_from(fs, offset, (u_char *)&private_head, sizeof(private_head));
         be_to_native_header_private(&private_head);
         if (FS_NVRAM_MAGIC_PRIVATE_CONFIG == private_head.magic) {
            if (private_head.end != private_head.start + private_head.len || private_head.len > fs->len)
               return(FS_NVRAM_ERR_INVALID_ADDRESS); // data is corrupted?
            if (private_head.start < fs->addr || private_head.end > fs->addr + fs->len)
               return(FS_NVRAM_ERR_INVALID_ADDRESS); // fs->addr has the wrong value?
            if (private_head.end != private_head.start + private_head.len)
               return(FS_NVRAM_ERR_INVALID_ADDRESS); // data is corrupted?
         }
      }
   }

   return(0);
}
