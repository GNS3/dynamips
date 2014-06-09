/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * PCMCIA ATA Flash emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "fs_mbr.h"
#include "fs_fat.h"

#define DEBUG_ACCESS  0
#define DEBUG_ATA     0
#define DEBUG_READ    0
#define DEBUG_WRITE   0

/* Default disk parameters: 4 heads, 32 sectors per track */
#define DISK_NR_HEADS         4
#define DISK_SECTS_PER_TRACK  32

/* Size (in bytes) of a sector */
#define SECTOR_SIZE  512

/* ATA commands */
#define ATA_CMD_NOP           0x00
#define ATA_CMD_READ_SECTOR   0x20
#define ATA_CMD_WRITE_SECTOR  0x30
#define ATA_CMD_IDENT_DEVICE  0xEC

/* ATA status */
#define ATA_STATUS_BUSY       0x80   /* Controller busy */
#define ATA_STATUS_RDY        0x40   /* Device ready */
#define ATA_STATUS_DWF        0x20   /* Write fault */
#define ATA_STATUS_DSC        0x10   /* Device ready */
#define ATA_STATUS_DRQ        0x08   /* Data Request */
#define ATA_STATUS_CORR       0x04   /* Correctable error */
#define ATA_STATUS_IDX        0x02   /* Always 0 */
#define ATA_STATUS_ERR        0x01   /* Error */

/* ATA Drive/Head register */
#define ATA_DH_LBA            0x40   /* LBA Mode */

/* Card Information Structure */
static m_uint8_t cis_table[] = {
   /* CISTPL_DEVICE */
   0x01, 0x03,
      0xd9, /* DSPEED_250NS | WPS | DTYPE_FUNCSPEC */
      0x01, /* 2 KBytes(Units)/64 KBytes(Max Size) */
      0xff, /* 0xFF */
   /* CISTPL_DEVICE_OC */
   0x1c, 0x04,
      0x03, /* MWAIT | 3.3 volt VCC operation */
      0xd9, /* DSPEED_250NS | WPS | DTYPE_FUNCSPEC */
      0x01, /* 2 KBytes(Units)/64 KBytes(Max Size) */
      0xff, /* 0xFF */
   /* CISTPL_JEDEC_C */
   0x18, 0x02,
      0xdf, /* PCMCIA */
      0x01, /* 0x01 */
   /* CISTPL_MANFID */
   0x20, 0x04,
      0x34, 0x12, /* 0x1234 ??? */
      0x00, 0x02, /* 0x0200 */
   /* CISTPL_VERS_1 */
   0x15, 0x2b,
      0x04, 0x01, /* PCMCIA 2.0/2.1 / JEIDA 4.1/4.2 */
      0x44, 0x79, 0x6e, 0x61, 0x6d, 0x69, 0x70, 0x73,
      0x20, 0x41, 0x54, 0x41, 0x20, 0x46, 0x6c, 0x61,
      0x73, 0x68, 0x20, 0x43, 0x61, 0x72, 0x64, 0x20,
      0x20, 0x00, /* "Dynamips ATA Flash Card  " */
      0x44, 0x59, 0x4e, 0x41, 0x30, 0x20, 0x20, 0x00, /* "DYNA0  " */
      0x44, 0x59, 0x4e, 0x41, 0x30, 0x00, /* "DYNA0" */
      0xff, /* 0xFF */
   /* CISTPL_FUNCID */
   0x21, 0x02,
      0x04, /* Fixed Disk */
      0x01, /* Power-On Self Test */
   /* CISTPL_FUNCE */
   0x22, 0x02,
      0x01, /* Disk Device Interface tuple */
      0x01, /* PC Card-ATA Interface */
   /* CISTPL_FUNCE:  */
   0x22, 0x03,
      0x02, /* Basic PC Card ATA Interface tuple */
      0x04, /* S(Silicon Device) */
      0x5f, /* P0(Sleep) | P1(Standy) | P2(Idle) | P3(Auto) | N(3F7/377 Register Inhibit Available) | I(IOIS16# on Twin Card) */
   /* CISTPL_CONFIG */
   0x1a, 0x05,
      0x01, /* TPCC_RASZ=2, TPCC_RMSZ=1, TPCC_RFSZ=0 */
      0x03, /* TPCC_LAST=3 */
      0x00, 0x02, /* TPCC_RADR(Configuration Register Base Address)=0x0200 */
      0x0f, /* TPCC_RMSK(Configuration Register Presence Mask Field)=0x0F */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x0b,
      0xc0, /* Index=0 | Default | Interface */
      0x40, /* Memory | READY Active */
      0xa1, /* VCC power-description-structure only | Single 2-byte length specified | Misc */
      0x27, /* Nom V | Min V | Max V | Peak I */
      0x55, /* Nom V=5V */
      0x4d, /* Min V=4.5V */
      0x5d, /* Max V=5V */
      0x75, /* Peak I=80mA */
      0x08, 0x00, /* Card Address=0 | Host Address=0x0008 * 256 bytes */
      0x21, /* Max Twin Cards=1 | Power Down */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x06,
      0x00, /* Index=0 */
      0x01, /* VCC power-description-structure only */
      0x21, /* Nom V | Peak I */
      0xb5, 0x1e, /* Nom V=3.30V */
      0x4d, /* Peak I=45mA */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x0d,
      0xc1, /* Index=1 | Default | Interface */
      0x41, /* I/O and Memory | READY Active */
      0x99, /* VCC power-description-structure only | IO Space | IRQ | Misc */
      0x27, /* Nom V | Min V | Max V | Peak I */
      0x55, /* Nom V=5V */
      0x4d, /* Min V=4.5V */
      0x5d, /* Max V=5V */
      0x75, /* Peak I=80mA */
      0x64, /* IOAddrLines=4 | All registers are accessible by both 8-bit or 16-bit accesses */
      0xf0, 0xff, 0xff, /* Mask | Level | Pulse | Share | IRQ0..IRQ15 */
      0x21, /* Max Twin Cards=1 | Power Down */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x06,
      0x01, /* Index=1 */
      0x01, /* VCC power-description-structure only */
      0x21, /* Nom V | Peak I */
      0xb5, 0x1e, /* Nom V=3.30V */
      0x4d, /* Peak I=45mA */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x12,
      0xc2, /* Index=2 | Default | Interface */
      0x41, /* I/O and Memory | READY Active */
      0x99, /* VCC power-description-structure only | IO Space | IRQ | Misc */
      0x27, /* Nom V | Min V | Max V | Peak I */
      0x55, /* Nom V=5V */
      0x4d, /* Min V=4.5V */
      0x5d, /* Max V=5V */
      0x75, /* Peak I=80mA */
      0xea, /* IOAddrLines=10 | All registers are accessible by both 8-bit or 16-bit accesses | Range */
      0x61, /* Number of I/O Address Ranges=2 | Size of Address=2 | Size of Length=1 */
      0xf0, 0x01, 0x07, /* Address=0x1F0, Length=8 */
      0xf6, 0x03, 0x01, /* Address=0x3F6, Length=2 */
      0xee, /* IRQ14 | Level | Pulse | Share */
      0x21, /* Max Twin Cards=1 | Power Down */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x06,
      0x02, /* Index=2 */
      0x01, /* VCC power-description-structure only */
      0x21, /* Nom V | Peak I */
      0xb5, 0x1e, /* Nom V=3.30V */
      0x4d, /* Peak I=45mA */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x12,
      0xc3, /* Index=3 | Default | Interface */
      0x41, /* I/O and Memory | READY Active */
      0x99, /* VCC power-description-structure only | IO Space | IRQ | Misc */
      0x27, /* Nom V | Min V | Max V | Peak I */
      0x55, /* Nom V=5V */
      0x4d, /* Min V=4.5V */
      0x5d, /* Max V=5V */
      0x75, /* Peak I=80mA */
      0xea, /* IOAddrLines=10 | All registers are accessible by both 8-bit or 16-bit accesses | Range */
      0x61, /* Number of I/O Address Ranges=2 | Size of Address=2 | Size of Length=1 */
      0x70, 0x01, 0x07, /* Address=0x170, Length=8 */
      0x76, 0x03, 0x01, /* Address=0x376, Length=2 */
      0xee, /* IRQ14 | Level | Pulse | Share */
      0x21, /* Max Twin Cards=1 | Power Down */
   /* CISTPL_CFTABLE_ENTRY */
   0x1b, 0x06,
      0x03, /* Index=3 */
      0x01, /* VCC power-description-structure only */
      0x21, /* Nom V | Peak I */
      0xb5, 0x1e, /* Nom V=3.30V */
      0x4d, /* Peak I=45mA */
   /* CISTPL_NO_LINK */
   0x14, 0x00,
   /* CISTPL_END */
   0xff,
};

/* PCMCIA private data */
struct pcmcia_disk_data {
   vm_instance_t *vm;
   vm_obj_t vm_obj;
   struct vdevice dev;
   char *filename;
   int fd;

   /* Disk parameters (C/H/S) */
   u_int nr_heads;
   u_int nr_cylinders;
   u_int sects_per_track;

   /* Current ATA command and CHS info */
   m_uint8_t ata_cmd,ata_cmd_in_progress;
   m_uint8_t ata_status;

   m_uint8_t cyl_low,cyl_high;
   m_uint8_t head,sect_no;
   m_uint8_t sect_count;
   
   /* Current sector */
   m_uint32_t sect_pos;

   /* Remaining sectors to read or write */
   u_int sect_remaining;

   /* Callback function when data buffer is validated */
   void (*ata_cmd_callback)(struct pcmcia_disk_data *);

   /* Data buffer */
   m_uint32_t data_offset;
   u_int data_pos;
   m_uint8_t data_buffer[SECTOR_SIZE];
};

/* Convert a CHS reference to an LBA reference */
static inline m_uint32_t chs_to_lba(struct pcmcia_disk_data *d,
                                    u_int cyl,u_int head,u_int sect)
{
   return((((cyl * d->nr_heads) + head) * d->sects_per_track) + sect - 1);
}

/* Convert a LBA reference to a CHS reference */
static inline void lba_to_chs(struct pcmcia_disk_data *d,m_uint32_t lba,
                              u_int *cyl,u_int *head,u_int *sect)
{
   *cyl = lba / (d->sects_per_track * d->nr_heads);
   *head = (lba / d->sects_per_track) % d->nr_heads;
   *sect = (lba % d->sects_per_track) + 1;
}

/* Format disk with a single FAT16 partition */
static int disk_format(struct pcmcia_disk_data *d)
{
   struct mbr_data mbr;
   struct mbr_partition *part;
   u_int cyl=0, head=1, sect=1;

   /* Master Boot Record */
   memset(&mbr,0,sizeof(mbr));
   mbr.signature[0] = MBR_SIGNATURE_0;
   mbr.signature[1] = MBR_SIGNATURE_1;
   part = &mbr.partition[0];
   part->bootable = 0;
   part->type = MBR_PARTITION_TYPE_FAT16;
   part->lba = chs_to_lba(d, 0, 1, 1);
   part->nr_sectors = d->nr_heads * d->nr_cylinders * d->sects_per_track - part->lba;
   lba_to_chs(d, part->lba + part->nr_sectors - 1, &cyl, &head, &sect);
   mbr_set_chs(part->first_chs, 0, 1, 1);
   mbr_set_chs(part->last_chs, cyl, head, sect);

   if (mbr_write_fd(d->fd, &mbr)<0) {
      return(-1);
   }

   /* FAT16 partition */
   if (fs_fat_format16(d->fd, part->lba, part->nr_sectors, d->sects_per_track,
                       d->nr_heads, d->vm_obj.name)) {
      return(-1);
   }

   return(0);
}

/* Create the virtual disk */
static int disk_create(struct pcmcia_disk_data *d)
{
   off_t disk_len;

   if ((d->fd = open(d->filename,O_CREAT|O_EXCL|O_RDWR,0600)) < 0) {
      /* already exists? */
      if ((d->fd = open(d->filename,O_CREAT|O_RDWR,0600)) < 0) {
         perror("disk_create: open");
         return(-1);
      }
   }
   else {
      /* new disk */
      if (disk_format(d)) {
         return(-1);
      }
   }

   disk_len = d->nr_heads * d->nr_cylinders * d->sects_per_track * SECTOR_SIZE;
   ftruncate(d->fd,disk_len);
   return(0);
}

/* Read a sector from disk file */
static int disk_read_sector(struct pcmcia_disk_data *d,m_uint32_t sect,
                            m_uint8_t *buffer)
{
   off_t disk_offset = (off_t)sect * SECTOR_SIZE;

#if DEBUG_READ
   vm_log(d->vm,d->dev.name,"reading sector 0x%8.8x\n",sect);
#endif

   if (lseek(d->fd,disk_offset,SEEK_SET) == -1) {
      perror("read_sector: lseek");
      return(-1);
   }
   
   if (read(d->fd,buffer,SECTOR_SIZE) != SECTOR_SIZE) {
      perror("read_sector: read");
      return(-1);
   }

   return(0);
}

/* Write a sector to disk file */
static int disk_write_sector(struct pcmcia_disk_data *d,m_uint32_t sect,
                             m_uint8_t *buffer)
{  
   off_t disk_offset = (off_t)sect * SECTOR_SIZE;

#if DEBUG_WRITE
   vm_log(d->vm,d->dev.name,"writing sector 0x%8.8x\n",sect);
#endif

   if (lseek(d->fd,disk_offset,SEEK_SET) == -1) {
      perror("write_sector: lseek");
      return(-1);
   }
   
   if (write(d->fd,buffer,SECTOR_SIZE) != SECTOR_SIZE) {
      perror("write_sector: write");
      return(-1);
   }

   return(0);
}

/* Identify PCMCIA device (ATA command 0xEC) */
static void ata_identify_device(struct pcmcia_disk_data *d)
{
   m_uint8_t *p = d->data_buffer;
   m_uint32_t sect_count;

   sect_count = d->nr_heads * d->nr_cylinders * d->sects_per_track;

   /* Clear all fields (for safety) */
   memset(p,0x00,SECTOR_SIZE);

   /* Word 0: General Configuration */
   p[0] = 0x8a; /* Not MFM encoded | Hard sectored | Removable cartridge drive */
   p[1] = 0x84; /* Disk transfer rate !<= 10Mbs | Non-rotating disk drive */

   /* Word 1: Default number of cylinders */
   p[2] = d->nr_cylinders & 0xFF;
   p[3] = (d->nr_cylinders >> 8) & 0xFF;

   /* Word 3: Default number of heads */
   p[6] = d->nr_heads;

   /* Word 6: Default number of sectors per track */
   p[12] = d->sects_per_track;

   /* Word 7: Number of sectors per card (MSW) */
   p[14] = (sect_count >> 16) & 0xFF;
   p[15] = (sect_count >> 24);

   /* Word 8: Number of sectors per card (LSW) */
   p[16] = sect_count & 0xFF;
   p[17] = (sect_count >> 8) & 0xFF;

   /* Word 22: ECC count */
   p[44] = 0x04;

   /* Word 53: Translation parameters valid */
   p[106] = 0x3;

   /* Word 54: Current number of cylinders */
   p[108] = d->nr_cylinders & 0xFF;
   p[109] = (d->nr_cylinders >> 8) & 0xFF;

   /* Word 55: Current number of heads */
   p[110] = d->nr_heads;

   /* Word 56: Current number of sectors per track */
   p[112] = d->sects_per_track;

   /* Word 57/58: Current of sectors per card (LSW/MSW) */
   p[114] = sect_count & 0xFF;
   p[115] = (sect_count >> 8) & 0xFF;

   p[116] = (sect_count >> 16) & 0xFF;
   p[117] = (sect_count >> 24);

#if 0
   /* Word 60/61: Total sectors addressable in LBA mode (MSW/LSW) */
   p[120] = (sect_count >> 16) & 0xFF;
   p[121] = (sect_count >> 24);
   p[122] = sect_count & 0xFF;
   p[123] = (sect_count >> 8) & 0xFF;
#endif
}

/* Set sector position */
static void ata_set_sect_pos(struct pcmcia_disk_data *d)
{
   u_int cyl;

   if (d->head & ATA_DH_LBA) {
      d->sect_pos  = (u_int)(d->head & 0x0F) << 24;
      d->sect_pos |= (u_int)d->cyl_high << 16;
      d->sect_pos |= (u_int)d->cyl_low  << 8;
      d->sect_pos |= (u_int)d->sect_no;

#if DEBUG_ATA
      vm_log(d->vm,d->dev.name,"ata_set_sect_pos: LBA sect=0x%x\n",
             d->sect_pos);
#endif
   } else {
      cyl = (((u_int)d->cyl_high) << 8) + d->cyl_low;
      d->sect_pos = chs_to_lba(d,cyl,d->head & 0x0F,d->sect_no);
     
#if DEBUG_ATA
      vm_log(d->vm,d->dev.name,
             "ata_set_sect_pos: cyl=0x%x,head=0x%x,sect=0x%x => "
             "sect_pos=0x%x\n",
             cyl,d->head & 0x0F,d->sect_no,d->sect_pos);
#endif
   }
}

/* ATA device identifier callback */
static void ata_cmd_ident_device_callback(struct pcmcia_disk_data *d)
{
   d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC;
}

/* ATA read sector callback */
static void ata_cmd_read_callback(struct pcmcia_disk_data *d)
{
   d->sect_remaining--;

   if (!d->sect_remaining) {
      d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC;
      return;
   }

   /* Read the next sector */
   d->sect_pos++;
   disk_read_sector(d,d->sect_pos,d->data_buffer);
   d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC|ATA_STATUS_DRQ;
}

/* ATA write sector callback */
static void ata_cmd_write_callback(struct pcmcia_disk_data *d)
{
   /* Write the sector */
   disk_write_sector(d,d->sect_pos,d->data_buffer);
   d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC|ATA_STATUS_DRQ;
   d->sect_pos++;

   d->sect_remaining--;

   if (!d->sect_remaining) {
      d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC;
   }
}

/* Handle an ATA command */
static void ata_handle_cmd(struct pcmcia_disk_data *d)
{
#if DEBUG_ATA
   vm_log(d->vm,d->dev.name,"ATA command 0x%2.2x\n",(u_int)d->ata_cmd);
#endif

   d->data_pos = 0;

   switch(d->ata_cmd) {
      case ATA_CMD_IDENT_DEVICE:
         ata_identify_device(d);
         d->ata_cmd_callback = ata_cmd_ident_device_callback;
         d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC|ATA_STATUS_DRQ;
         break;

      case ATA_CMD_READ_SECTOR:
         d->sect_remaining = d->sect_count;

         if (!d->sect_remaining)
            d->sect_remaining = 256;

         ata_set_sect_pos(d);
         disk_read_sector(d,d->sect_pos,d->data_buffer);
         d->ata_cmd_callback = ata_cmd_read_callback;
         d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC|ATA_STATUS_DRQ;
         break;

      case ATA_CMD_WRITE_SECTOR:
         d->sect_remaining = d->sect_count;

         if (!d->sect_remaining)
            d->sect_remaining = 256;

         ata_set_sect_pos(d);
         d->ata_cmd_callback = ata_cmd_write_callback;
         d->ata_status = ATA_STATUS_RDY|ATA_STATUS_DSC|ATA_STATUS_DRQ;
         break;

      default:
         vm_log(d->vm,d->dev.name,"unhandled ATA command 0x%2.2x\n",
                (u_int)d->ata_cmd);
   }
}

/*
 * dev_pcmcia_disk_access_0()
 */
void *dev_pcmcia_disk_access_0(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct pcmcia_disk_data *d = dev->priv_data;

   /* Compute the good internal offset */
   offset = (offset >> 1) ^ 1;
   
#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->dev.name,
              "reading offset 0x%5.5x at pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->dev.name,
              "writing offset 0x%5.5x, data=0x%llx at pc=0x%llx (size=%u)\n",
              offset,*data,cpu_get_pc(cpu),op_size);
   }
#endif

   /* Card Information Structure */
   if (offset < sizeof(cis_table)) {
      if (op_type == MTS_READ)
         *data = cis_table[offset];

      return NULL;
   }
      
   switch(offset) {
      case 0x102:     /* Pin Replacement Register */
         if (op_type == MTS_READ)
            *data = 0x22;
         break;

      case 0x80001:   /* Sector Count + Sector no */
         if (op_type == MTS_READ) {
            *data = (d->sect_no << 8) + d->sect_count;
         } else {
            d->sect_no    = *data >> 8;
            d->sect_count = *data & 0xFF;
         }
         break;

      case 0x80002:   /* Cylinder Low + Cylinder High */
         if (op_type == MTS_READ) {
            *data = (d->cyl_high << 8) + d->cyl_low;
         } else {
            d->cyl_high = *data >> 8;
            d->cyl_low  = *data & 0xFF;
         }
         break;

      case 0x80003:   /* Select Card/Head + Status/Command register */
         if (op_type == MTS_READ)
            *data = (d->ata_status << 8) + d->head;
         else {
            d->ata_cmd = *data >> 8;
            d->head = *data;
            ata_handle_cmd(d);
         }            
         break;

      default:
         /* Data buffer access ? */
         if ((offset >= d->data_offset) && 
             (offset < d->data_offset + (SECTOR_SIZE/2)))
         {
            if (op_type == MTS_READ) {
               *data =  d->data_buffer[(d->data_pos << 1)];
               *data += d->data_buffer[(d->data_pos << 1)+1] << 8;
            } else {
               d->data_buffer[(d->data_pos << 1)]   = *data & 0xFF;
               d->data_buffer[(d->data_pos << 1)+1] = *data >> 8;
            }
            
            d->data_pos++;

            /* Buffer complete: call the callback function */
            if (d->data_pos == (SECTOR_SIZE/2)) {
               d->data_pos = 0;
               
               if (d->ata_cmd_callback)
                  d->ata_cmd_callback(d);
            }
         }
   }

   return NULL;
}

/*
 * dev_pcmcia_disk_access_1()
 */
void *dev_pcmcia_disk_access_1(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct pcmcia_disk_data *d = dev->priv_data;

   /* Compute the good internal offset */
   offset = (offset >> 1) ^ 1;
   
#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->dev.name,
              "reading offset 0x%5.5x at pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->dev.name,
              "writing offset 0x%5.5x, data=0x%llx at pc=0x%llx (size=%u)\n",
              offset,*data,cpu_get_pc(cpu),op_size);
   }
#endif

   switch(offset) {
      case 0x02:   /* Sector Count + Sector no */
         if (op_type == MTS_READ) {
            *data = (d->sect_no << 8) + d->sect_count;
         } else {
            d->sect_no    = *data >> 8;
            d->sect_count = *data & 0xFF;
         }
         break;

      case 0x04:   /* Cylinder Low + Cylinder High */
         if (op_type == MTS_READ) {
            *data = (d->cyl_high << 8) + d->cyl_low;
         } else {
            d->cyl_high = *data >> 8;
            d->cyl_low  = *data & 0xFF;
         }
         break;

      case 0x06:   /* Select Card/Head + Status/Command register */
         if (op_type == MTS_READ)
            *data = (d->ata_status << 8) + d->head;
         else {
            d->ata_cmd = *data >> 8;
            d->head = *data & 0xFF;
            ata_handle_cmd(d);
         }
         break;

      case 0x08:   /* Data */
         if (op_type == MTS_READ) {
            *data =  d->data_buffer[(d->data_pos << 1)];
            *data += d->data_buffer[(d->data_pos << 1)+1] << 8;
         } else {
            d->data_buffer[(d->data_pos << 1)]   = *data & 0xFF;
            d->data_buffer[(d->data_pos << 1)+1] = *data >> 8;
         }

         d->data_pos++;

         /* Buffer complete: call the callback function */
         if (d->data_pos == (SECTOR_SIZE/2)) {
            d->data_pos = 0;

            if (d->ata_cmd_callback)
               d->ata_cmd_callback(d);
         }
         break;

      case 0x0E:   /* Status/Drive Control + Drive Address */
         break;
   }

   return NULL;
}

/* Shutdown a PCMCIA disk device */
void dev_pcmcia_disk_shutdown(vm_instance_t *vm,struct pcmcia_disk_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Close disk file */
      if (d->fd != -1) close(d->fd);

      /* Free filename */
      free(d->filename);
      
      /* Free the structure itself */
      free(d);
   }
}

/* Initialize a PCMCIA disk */
vm_obj_t *dev_pcmcia_disk_init(vm_instance_t *vm,char *name,
                               m_uint64_t paddr,m_uint32_t len,
                               u_int disk_size,int mode)
{
   struct pcmcia_disk_data *d;
   m_uint32_t tot_sect;

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"PCMCIA: unable to create disk device '%s'.\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));
   vm_object_init(&d->vm_obj);
   d->vm = vm;
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_pcmcia_disk_shutdown;
   d->fd = -1;

   if (!(d->filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"PCMCIA: unable to create filename.\n");
      goto err_filename;
   }

   /* Data buffer offset in mapped memory */
   d->data_offset = 0x80200;
   d->ata_status  = ATA_STATUS_RDY|ATA_STATUS_DSC;

   /* Compute the number of cylinders given a disk size in Mb */
   tot_sect = ((m_uint64_t)disk_size * 1048576) / SECTOR_SIZE;

   d->nr_heads = DISK_NR_HEADS;
   d->sects_per_track = DISK_SECTS_PER_TRACK;
   d->nr_cylinders = tot_sect / (d->nr_heads * d->sects_per_track);

   vm_log(vm,name,"C/H/S settings = %u/%u/%u\n",
          d->nr_cylinders,d->nr_heads,d->sects_per_track);

   /* Create the disk file */
   if (disk_create(d) == -1)
      goto err_disk_create;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.flags     = VDEVICE_FLAG_CACHING;

   if (mode == 0)
      d->dev.handler = dev_pcmcia_disk_access_0;
   else
      d->dev.handler = dev_pcmcia_disk_access_1;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(&d->vm_obj);

 err_disk_create:
   free(d->filename);
 err_filename:
   free(d);
   return NULL;
}

/* Get the device associated with a PCMCIA disk object */
struct vdevice *dev_pcmcia_disk_get_device(vm_obj_t *obj)
{
   struct pcmcia_disk_data *d;

   if (!obj || !(d = obj->data))
      return NULL;

   return(&d->dev);
}
