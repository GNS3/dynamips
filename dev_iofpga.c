/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) I/O FPGA:
 *   - Simulates a NMC93C46 Serial EEPROM as CPU and Midplane EEPROM.
 *   - Simulates a DALLAS DS1620 for Temperature Sensors.
 *   - Simulates voltage sensors.
 *   - Simulates console and AUX ports.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "ptask.h"
#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"
#include "nmc93c46.h"
#include "ds1620.h"
#include "dev_c7200.h"

/* Debugging flags */
#define DEBUG_UNKNOWN  1
#define DEBUG_LED      0
#define DEBUG_IO_CTL   0
#define DEBUG_ENVM     0

/* DUART RX/TX status (SRA/SRB) */
#define DUART_RX_READY  0x01
#define DUART_TX_READY  0x04

/* DUART RX/TX Interrupt Status/Mask */
#define DUART_TXRDYA  0x01
#define DUART_RXRDYA  0x02
#define DUART_TXRDYB  0x10
#define DUART_RXRDYB  0x20

/* Definitions for CPU and Midplane Serial EEPROMs */
#define DO2_DATA_OUT_MIDPLANE	 7
#define DO1_DATA_OUT_CPU	 6
#define CS2_CHIP_SEL_MIDPLANE	 5
#define SK2_CLOCK_MIDPLANE 	 4
#define DI2_DATA_IN_MIDPLANE	 3
#define CS1_CHIP_SEL_CPU	 2
#define SK1_CLOCK_CPU	 	 1
#define DI1_DATA_IN_CPU		 0

/* Definitions for PEM (NPE-B) Serial EEPROM */
#define DO1_DATA_OUT_PEM   3
#define DI1_DATA_IN_PEM    2
#define CS1_CHIP_SEL_PEM   1
#define SK1_CLOCK_PEM      0

/* Pack the NVRAM */
#define NVRAM_PACKED   0x04

/* 4 temperature sensors in a C7200 */
#define C7200_TEMP_SENSORS  4
#define C7200_DEFAULT_TEMP  22    /* default temperature: 22°C */

/* Voltages */
#define C7200_A2D_SAMPLES   9

/*
 * A2D MUX Select definitions.
 */
#define C7200_MUX_PS0     0x00   /* Power Supply 0 */
#define C7200_MUX_PS1     0x02   /* Power Supply 1 */
#define C7200_MUX_P3V     0x04   /* +3V */
#define C7200_MUX_P12V    0x08   /* +12V */
#define C7200_MUX_P5V     0x0a   /* +5V */
#define C7200_MUX_N12V    0x0c   /* -12V */

/* Analog To Digital Converters samples */
#define C7200_A2D_PS0     1150
#define C7200_A2D_PS1     1150

/* Voltage Samples */
#define C7200_A2D_P3V     1150
#define C7200_A2D_P12V    1150
#define C7200_A2D_P5V     1150
#define C7200_A2D_N12V    1150

/* IO FPGA structure */
struct iofpga_data {
   u_int io_ctrl_reg;

   /* Managing CPU */
   cpu_mips_t *mgr_cpu;

   /* DUART & Console Management */
   u_int duart_interrupt;
   pthread_t duart_con_thread;
   pthread_t duart_aux_thread;

   /* Virtual TTY for Console and AUX ports */
   vtty_t *vtty_con,*vtty_aux;

   /* Temperature Control */
   u_int temp_cfg_reg[C7200_TEMP_SENSORS];
   u_int temp_deg_reg[C7200_TEMP_SENSORS];
   u_int temp_clk_low;

   u_int temp_cmd;
   u_int temp_cmd_pos;

   u_int temp_data;
   u_int temp_data_pos;

   /* Voltages */
   u_int mux;
};

/* Empty EEPROM */
static unsigned short eeprom_empty_data[16] = {
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
};

/* CPU EEPROM definition */
static struct nmc93c46_group_def eeprom_cpu_def = {
   SK1_CLOCK_CPU, CS1_CHIP_SEL_CPU, 
   DI1_DATA_IN_CPU, DO1_DATA_OUT_CPU,
   NULL, 0,
};

/* Midplane EEPROM definition */
static struct nmc93c46_group_def eeprom_midplane_def = {
   SK2_CLOCK_MIDPLANE, CS2_CHIP_SEL_MIDPLANE, 
   DI2_DATA_IN_MIDPLANE, DO2_DATA_OUT_MIDPLANE,
   NULL, 0,
};

/* PEM (NPE-B) EEPROM definition */
static struct nmc93c46_group_def eeprom_pem_def = {
   SK1_CLOCK_PEM, CS1_CHIP_SEL_PEM, DI1_DATA_IN_PEM, DO1_DATA_OUT_PEM,
   eeprom_empty_data, (sizeof(eeprom_empty_data) / 2),
};

/* IOFPGA manages simultaneously CPU and Midplane EEPROM */
static struct nmc93c46_eeprom eeprom_cpu_midplane = {
   2, 0, "CPU and Midplane EEPROM", 0, 
   { &eeprom_cpu_def, &eeprom_midplane_def },
   { { 0, 0, 0, 0, 0}, { 0, 0, 0, 0, 0} },
};

/* 
 * IOFPGA manages also PEM EEPROM (for NPE-B)
 * PEM stands for "Power Entry Module":
 * http://www.cisco.com/en/US/products/hw/routers/ps341/products_field_notice09186a00801cb26d.shtml
 */
static struct nmc93c46_eeprom eeprom_pem_npeb = {
   1, 0, "PEM (NPE-B) EEPROM", 0, { &eeprom_pem_def }, { { 0, 0, 0, 0, 0} },
};

/* Reset DS1620 */
static void temp_reset(struct iofpga_data *d)
{
   d->temp_cmd_pos = 0;
   d->temp_cmd = 0;

   d->temp_data_pos = 0;
   d->temp_data = 0;
}

/* Write the temperature control data */
static void temp_write_ctrl(struct iofpga_data *d,u_char val)
{
   switch(val) {
      case DS1620_RESET_ON:
         temp_reset(d);
         break;

      case DS1620_CLK_LOW:
         d->temp_clk_low = 1;
         break;

      case DS1620_CLK_HIGH:
         d->temp_clk_low = 0;
         break;
   }
}

/* Read a temperature control data */
static u_int temp_read_data(struct iofpga_data *d)
{
   u_int i,data = 0;

   switch(d->temp_cmd) {
      case DS1620_READ_CONFIG:
         for(i=0;i<C7200_TEMP_SENSORS;i++)
            data |= ((d->temp_cfg_reg[i] >> d->temp_data_pos) & 1) << i;

         d->temp_data_pos++;

         if (d->temp_data_pos == DS1620_CONFIG_READ_SIZE)
            temp_reset(d);

         break;

      case DS1620_READ_TEMP:
         for(i=0;i<C7200_TEMP_SENSORS;i++)
            data |= ((d->temp_deg_reg[i] >> d->temp_data_pos) & 1) << i;

         d->temp_data_pos++;

         if (d->temp_data_pos == DS1620_DATA_READ_SIZE)
            temp_reset(d);

         break;

      default:
         m_log("IO_FPGA","temp_sensors: CMD = 0x%x\n",d->temp_cmd);
   }

   return(data);
}

/* Write the temperature data write register */
static void temp_write_data(struct iofpga_data *d,u_char val)
{
   if (val == DS1620_ENABLE_READ) {
      d->temp_data_pos = 0;
      return;
   }

   if (!d->temp_clk_low)
      return;

   /* Write a command */
   if (d->temp_cmd_pos < DS1620_WRITE_SIZE)
   {
      if (val == DS1620_DATA_HIGH)
         d->temp_cmd |= 1 << d->temp_cmd_pos;

      d->temp_cmd_pos++;
   
      if (d->temp_cmd_pos == DS1620_WRITE_SIZE) {
         switch(d->temp_cmd) {
            case DS1620_START_CONVT:
               //printf("temp_sensors: IOS enabled continuous monitoring.\n");
               temp_reset(d);
               break;
            case DS1620_READ_CONFIG:
            case DS1620_READ_TEMP:
               break;
            default:
               m_log("IO_FPGA","temp_sensors: IOS sent command 0x%x.\n",
                     d->temp_cmd);
         }
      }
   }
   else
   {
      if (val == DS1620_DATA_HIGH)
         d->temp_data |= 1 << d->temp_data_pos;

      d->temp_data_pos++;
   }
}

/* Console port input thread */
static void *tty_con_input(struct iofpga_data *d)
{
   while(1) {
      if (!vtty_read_and_store(d->vtty_con)) {
         if (d->duart_interrupt & DUART_RXRDYA)
            mips64_set_irq(d->mgr_cpu,C7200_DUART_IRQ);
      }
   }

   return NULL;
}

/* AUX port input thread */
static void *tty_aux_input(struct iofpga_data *d)
{
   while(1) {
      if (!vtty_read_and_store(d->vtty_aux)) {
         if (d->duart_interrupt & DUART_RXRDYB)
            mips64_set_irq(d->mgr_cpu,C7200_DUART_IRQ);
      }
   }

   return NULL;
}

/* IRQ trickery for Console and AUX ports */
static int tty_trigger_dummy_irq(struct iofpga_data *d,void *arg)
{
   if (d->duart_interrupt & (DUART_TXRDYA|DUART_TXRDYB))
      mips64_set_irq(d->mgr_cpu,C7200_DUART_IRQ);
   return(0);
}

/*
 * dev_iofpga_access()
 */
void *dev_iofpga_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                        u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct iofpga_data *d = dev->priv_data;
   u_char odata;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      /* I/O control register */
      case 0x204:
         if (op_type == MTS_WRITE) {
#if DEBUG_IO_CTL
            m_log("IO_FPGA: setting value 0x%llx in IO control register\n",
                  *data);
#endif
            d->io_ctrl_reg = *data;
         }
         else {
            *data = d->io_ctrl_reg;
            *data |= NVRAM_PACKED;              /* Packed NVRAM */
         }
         break;

      /* CPU/Midplane EEPROMs */
      case 0x21c:
         if (op_type == MTS_WRITE)
            nmc93c46_write(&eeprom_cpu_midplane,(u_int)(*data));
         else
            *data = nmc93c46_read(&eeprom_cpu_midplane);
         break;

      /* PEM (NPE-B) EEPROM */
      case 0x388:
          if (op_type == MTS_WRITE)
            nmc93c46_write(&eeprom_pem_npeb,(u_int)(*data));
         else
            *data = nmc93c46_read(&eeprom_pem_npeb);
         break;

      /* Watchdog */
      case 0x234:
         break;

      /* 
       * FPGA release/presence ? Flash SIMM size:
       *   0x0001: 2048K Flash (2 banks)
       *   0x0504: 8192K Flash (2 banks)
       *   0x0704: 16384K Flash (2 banks)
       *   0x2001: 1024K Flash (1 bank)
       *   0x2504: 4096K Flash (1 bank)
       *   0x2704: 8192K Flash (1 bank)
       *
       *   Number of Flash SIMM banks + size.
       *   Touching some lower bits causes problems with environmental monitor.
       *
       * It is displayed by command "sh bootflash: chips"
       */
      case 0x23c:
         if (op_type == MTS_READ)
            *data = 0x00002704;
         break;

      /* LEDs */
      case 0x244:
#if DEBUG_LED
         m_log("IO_FPGA","LED register is now 0x%x (0x%x)\n",
               *data,(~*data) & 0x0F);
#endif
         break;

      /* ==== DUART SCN2681 (console/aux) ==== */
      case 0x40c:   /* Status Register A (SRA) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(d->vtty_con))
               odata |= DUART_RX_READY;

            odata |= DUART_TX_READY;
         
            mips64_clear_irq(d->mgr_cpu,C7200_DUART_IRQ);
            *data = odata;
         }
         break;

      case 0x414:   /* Command Register A (CRA) */
         break;

      case 0x41c:   /* RX/TX Holding Register A (RHRA/THRA) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(d->vtty_con,(char)*data);
         } else {
            *data = vtty_get_char(d->vtty_con);
         }
         break;

      case 0x42c:   /* Interrupt Status/Mask Register (ISR/IMR) */
         if (op_type == MTS_WRITE) {
            d->duart_interrupt = *data;
         } else
            *data = d->duart_interrupt;
         break;         

      case 0x44c:   /* Status Register B (SRB) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(d->vtty_aux))
               odata |= DUART_RX_READY;

            odata |= DUART_TX_READY;
         
            //mips64_clear_irq(d->mgr_cpu,C7200_DUART_IRQ);
            *data = odata;
         }
         break;

      case 0x454:   /* Command Register B (CRB) */
         break;

      case 0x45c:   /* RX/TX Holding Register B (RHRB/THRB) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(d->vtty_aux,(char)*data);
         } else {
            *data = vtty_get_char(d->vtty_aux);
         }
         break;

      /* ==== DS 1620 (temp sensors) ==== */
      case 0x20c:   /* Temperature Control */
         if (op_type == MTS_WRITE)
            temp_write_ctrl(d,*data);
         break;

      case 0x214:   /* Temperature data write */
         if (op_type == MTS_WRITE) {
            temp_write_data(d,*data);
            d->mux = *data;
         }
         break;

      case 0x22c:   /* Temperature data read */
         *data = temp_read_data(d);
         break;

      case 0x257:   /* ENVM A/D Converter */
#if DEBUG_ENVM
         m_log("ENVM","access to envm a/d converter - mux = %u\n",d->mux);
#endif
         if (op_type == MTS_READ) {
            switch(d->mux) {
               case C7200_MUX_PS0:
                  *data = C7200_A2D_PS0;
                  break;

               case C7200_MUX_PS1:
                  *data = C7200_A2D_PS1;
                  break;

               case C7200_MUX_P3V:
                  *data = C7200_A2D_P3V;
                  break;

               case C7200_MUX_P12V:
                  *data = C7200_A2D_P12V;
                  break;

               case C7200_MUX_P5V:
                  *data = C7200_A2D_P5V;
                  break;

               case C7200_MUX_N12V:
                  *data = C7200_A2D_N12V;
                  break;
                  
               default:
                  *data = 0;
            }

            *data = *data / C7200_A2D_SAMPLES;
         }
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_WRITE)
            m_log("IO_FPGA","read from addr 0x%x\n",offset);
         else
            m_log("IO_FPGA","write to addr 0x%x, value=0x%llx\n",offset,*data);
#endif
   }

   return NULL;
}

/*
 * Set the base MAC address of the system.
 */
static int dev_iofpga_set_mac_addr(struct c7200_eeprom *mp_eeprom,
                                   char *mac_addr)
{
   m_eth_addr_t addr;

   if (parse_mac_addr(&addr,mac_addr) == -1) {
      fprintf(stderr,"IO_FPGA: unable to parse MAC address '%s'\n",mac_addr);
      return(-1);
   }
   
   c7200_set_mac_addr(mp_eeprom,&addr);
   return(0);
}

/*
 * dev_iofpga_init()
 */
int dev_iofpga_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len,
                    char *npe,char *midplane,char *mac_addr)
{  
   struct c7200_eeprom *npe_eeprom,*mp_eeprom,*pem_eeprom;
   struct iofpga_data *d;
   struct vdevice *dev;
   cpu_mips_t *cpu0;
   u_int i;

   /* Device is managed by CPU0 */
   cpu0 = cpu_group_find_id(cpu_group,0);

   /* Set the NPE EEPROM */
   if (!(npe_eeprom = c7200_get_cpu_eeprom(npe))) {
      fprintf(stderr,"C7200: unknown NPE \"%s\"!\n",npe);
      return(-1);
   }
   
   eeprom_cpu_def.data = npe_eeprom->data;
   eeprom_cpu_def.data_len = npe_eeprom->len;

   /* Set the Midplane EEPROM */
   if (!(mp_eeprom = c7200_get_midplane_eeprom(midplane))) {
      fprintf(stderr,"C7200: unknown Midplane \"%s\"!\n",midplane);
      return(-1);
   }
   
   eeprom_midplane_def.data = mp_eeprom->data;
   eeprom_midplane_def.data_len = mp_eeprom->len;

   /* Set the PEM EEPROM for NPE-175/NPE-225 */
   if ((pem_eeprom = c7200_get_pem_eeprom(npe)) != NULL) {
      eeprom_pem_def.data = pem_eeprom->data;
      eeprom_pem_def.data_len = pem_eeprom->len;
   }

   /* Set the base MAC address */
   if (mac_addr != NULL) {
      dev_iofpga_set_mac_addr(mp_eeprom,mac_addr);
   } else {
      printf("C7200: Warning, no MAC address set.\n");
   }

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"IO_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->mgr_cpu  = cpu0;
   d->vtty_con = vtty_create("Console port",vtty_con_type,vtty_con_tcp_port);
   d->vtty_aux = vtty_create("AUX port",vtty_aux_type,vtty_aux_tcp_port);

   for(i=0;i<C7200_TEMP_SENSORS;i++) {
      d->temp_cfg_reg[i] = DS1620_CONFIG_STATUS_CPU;
      d->temp_deg_reg[i] = C7200_DEFAULT_TEMP * 2;
   }

   /* Create the device itself */
   if (!(dev = dev_create("io_fpga"))) {
      fprintf(stderr,"IO_FPGA: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_iofpga_access;
   dev->priv_data = d;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);

   /* Create console threads */
   if (vtty_con_type != VTTY_TYPE_NONE)
      pthread_create(&d->duart_con_thread,NULL,(void *)tty_con_input,d);
   
   if (vtty_aux_type != VTTY_TYPE_NONE)
      pthread_create(&d->duart_aux_thread,NULL,(void *)tty_aux_input,d);

   ptask_add((ptask_callback)tty_trigger_dummy_irq,d,NULL);
   return(0);
}
