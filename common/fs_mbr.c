/** @file
 * @brief Master Boot Record
 *
 * Based on http://thestarman.pcministry.com/asm/mbr/PartTables.htm
 */

/*
 * Copyright (c) 2014 Flávio J. Saraiva <flaviojs2005@gmail.com>
 */

#include "fs_mbr.h"

/** Decode a CHS reference */
void mbr_get_chs(m_uint8_t chs[3], m_uint16_t *cyl, m_uint8_t *head, m_uint8_t *sect)
{
   if (head) *head = chs[0];
   if (sect) *sect = (chs[1]&0x3F);
   if (cyl) *cyl = ((m_uint16_t)(chs[1]&0xC0)<<2) | (chs[2]);
}

/** Encode a CHS reference */
void mbr_set_chs(m_uint8_t chs[3], m_uint16_t cyl, m_uint8_t head, m_uint8_t sect)
{
   if (cyl > MBR_CYLINDER_MAX) {
      /* c=1023, h=254, s=63 */
      chs[0] = 0xFE;
      chs[1] = 0xFF;
      chs[2] = 0xFF;
   }
   else {
      chs[0] = head;
      chs[1] = ((m_uint8_t)(cyl>>2)&0xC0) | (sect&0x3F);
      chs[2] = (m_uint8_t)(cyl&0xFF);
   }
}

/** Write MBR data */
int mbr_write_fd(int fd, struct mbr_data *mbr)
{
   if (!mbr) {
      fprintf(stderr,"mbr_write_fd: null");
      return(-1);
   }

   if (lseek(fd, MBR_OFFSET, SEEK_SET) != MBR_OFFSET) {
      perror("mbr_write_fd: lseek");
      return(-1);
   }

   if (write(fd, mbr, sizeof(*mbr)) != sizeof(*mbr)) {
      perror("mbr_write_fd: write");
      return(-1);
   }

   return(0);
}

/** Read MBR data */
int mbr_read_fd(int fd, struct mbr_data *mbr)
{
   if (!mbr) {
      fprintf(stderr,"mbr_read_fd: null");
      return(-1);
   }

   if (lseek(fd, MBR_OFFSET, SEEK_SET) != MBR_OFFSET) {
      perror("mbr_read_fd: lseek");
      return(-1);
   }

   if (read(fd, mbr, sizeof(*mbr)) != sizeof(*mbr)) {
      perror("mbr_read_fd: read");
      return(-1);
   }

   return(0);
}
