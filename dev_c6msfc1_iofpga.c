/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C6k-MSFC1 I/O FPGA:
 *   - Simulates a NMC93C56 Serial EEPROM.
 *   - Simulates a DALLAS DS1620 for Temperature Sensors.
 *   - Simulates console and AUX ports (SCN2681).
 *
 * This is very similar to c7200 platform.
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
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"
#include "nmc93cX6.h"
#include "ds1620.h"
#include "dev_c6msfc1.h"

/* Debugging flags */
#define DEBUG_UNKNOWN  1
#define DEBUG_ACCESS   0
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

/* Pack the NVRAM */
#define NVRAM_PACKED   0x04

/* 2 temperature sensors in a MSFC1: chassis inlet and oulet */
#define C6MSFC1_TEMP_SENSORS  2
#define C6MSFC1_DEFAULT_TEMP  22    /* default temperature: 22°C */

/* IO FPGA structure */
struct iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c6msfc1_t *router;

   /* Lock test */
   pthread_mutex_t lock;

   /* Periodic task to trigger dummy DUART IRQ */
   ptask_id_t duart_irq_tid;

   /* DUART & Console Management */
   u_int duart_isr,duart_imr,duart_irq_seq;
   
   /* IO control register */
   u_int io_ctrl_reg;

   /* Temperature Control */
   u_int temp_cfg_reg[C6MSFC1_TEMP_SENSORS];
   u_int temp_deg_reg[C6MSFC1_TEMP_SENSORS];
   u_int temp_clk_low;

   u_int temp_cmd;
   u_int temp_cmd_pos;

   u_int temp_data;
   u_int temp_data_pos;

   /* Voltages */
   u_int mux;
};

#define IOFPGA_LOCK(d)   pthread_mutex_lock(&(d)->lock)
#define IOFPGA_UNLOCK(d) pthread_mutex_unlock(&(d)->lock)

/* CPU EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_cpu_def = {
   SK1_CLOCK_CPU, CS1_CHIP_SEL_CPU, 
   DI1_DATA_IN_CPU, DO1_DATA_OUT_CPU,
};

/* Midplane EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_midplane_def = {
   SK2_CLOCK_MIDPLANE, CS2_CHIP_SEL_MIDPLANE, 
   DI2_DATA_IN_MIDPLANE, DO2_DATA_OUT_MIDPLANE,
};


/* IOFPGA manages simultaneously CPU and Midplane EEPROM */
static const struct nmc93cX6_group eeprom_cpu_midplane = {
   EEPROM_TYPE_NMC93C56, 2, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "CPU and Midplane EEPROM", 
   { &eeprom_cpu_def, &eeprom_midplane_def }, 
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
         for(i=0;i<C6MSFC1_TEMP_SENSORS;i++)
            data |= ((d->temp_cfg_reg[i] >> d->temp_data_pos) & 1) << i;

         d->temp_data_pos++;

         if (d->temp_data_pos == DS1620_CONFIG_READ_SIZE)
            temp_reset(d);

         break;

      case DS1620_READ_TEMP:
         for(i=0;i<C6MSFC1_TEMP_SENSORS;i++)
            data |= ((d->temp_deg_reg[i] >> d->temp_data_pos) & 1) << i;

         d->temp_data_pos++;

         if (d->temp_data_pos == DS1620_DATA_READ_SIZE)
            temp_reset(d);

         break;

      default:
         vm_log(d->router->vm,"IO_FPGA","temp_sensors: CMD = 0x%x\n",
                d->temp_cmd);
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
               vm_log(d->router->vm,"IO_FPGA",
                      "temp_sensors: IOS sent command 0x%x.\n",
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

/* Console port input */
static void tty_con_input(vtty_t *vtty)
{
   struct iofpga_data *d = vtty->priv_data;

   IOFPGA_LOCK(d);
   if (d->duart_imr & DUART_RXRDYA) {
      d->duart_isr |= DUART_RXRDYA;
      vm_set_irq(d->router->vm,C6MSFC1_DUART_IRQ);
   }
   IOFPGA_UNLOCK(d);
}

/* AUX port input */
static void tty_aux_input(vtty_t *vtty)
{
   struct iofpga_data *d = vtty->priv_data;

   IOFPGA_LOCK(d);
   if (d->duart_imr & DUART_RXRDYB) {
      d->duart_isr |= DUART_RXRDYB;
      vm_set_irq(d->router->vm,C6MSFC1_DUART_IRQ);
   }
   IOFPGA_UNLOCK(d);
}

/* IRQ trickery for Console and AUX ports */
static int tty_trigger_dummy_irq(struct iofpga_data *d,void *arg)
{
   u_int mask;

   IOFPGA_LOCK(d);
   d->duart_irq_seq++;
   
   if (d->duart_irq_seq == 2) {
      mask = DUART_TXRDYA|DUART_TXRDYB;
      if (d->duart_imr & mask) {
         d->duart_isr |= DUART_TXRDYA|DUART_TXRDYB;
         vm_set_irq(d->router->vm,C6MSFC1_DUART_IRQ);
      }

      d->duart_irq_seq = 0;
   }
   
   IOFPGA_UNLOCK(d);
   return(0);
}

/*
 * dev_c6msfc1_iofpga_access()
 */
void *dev_c6msfc1_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   struct iofpga_data *d = dev->priv_data;
   vm_instance_t *vm = d->router->vm;
   u_char odata;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"IO_FPGA","reading reg 0x%x at pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"IO_FPGA","writing reg 0x%x at pc=0x%llx, data=0x%llx\n",
              offset,cpu_get_pc(cpu),*data);
   }
#endif

   IOFPGA_LOCK(d);

   switch(offset) {
      case 0x294:
         /* 
          * Unknown, seen in 12.4(6)T, and seems to be read at each 
          * network interrupt.
          */
         if (op_type == MTS_READ)
            *data = 0x0;
         break;

      /* I/O control register */
      case 0x204:
         if (op_type == MTS_WRITE) {
#if DEBUG_IO_CTL
            vm_log(vm,"IO_FPGA","setting value 0x%llx in io_ctrl_reg\n",*data);
#endif
            d->io_ctrl_reg = *data;
         } else {
            *data = d->io_ctrl_reg;
            *data |= NVRAM_PACKED;              /* Packed NVRAM */
         }
         break;

      /* CPU/Midplane EEPROMs */
      case 0x21c:
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->sys_eeprom_g1,(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->sys_eeprom_g1);
         break;
         
      /* Watchdog */
      case 0x234:
         break;

      /* 
       * FPGA release/presence ? Flash SIMM size:
       *   0x0001: 2048K  Flash (2 banks)
       *   0x0504: 8192K  Flash (2 banks)
       *   0x0704: 16384K Flash (2 banks)
       *   0x0904: 32768K Flash (2 banks)
       *   0x0B04: 65536K Flash (2 banks)
       *   0x2001: 1024K  Flash (1 bank)
       *   0x2504: 4096K  Flash (1 bank)
       *   0x2704: 8192K  Flash (1 bank)
       *   0x2904: 16384K Flash (1 bank)
       *   0x2B04: 32768K Flash (1 bank)
       *
       *   Number of Flash SIMM banks + size.
       *   Touching some lower bits causes problems with environmental monitor.
       *
       * It is displayed by command "sh bootflash: chips"
       */
      case 0x23c:
         if (op_type == MTS_READ)
            *data = 0x2704;
         break;

      /* LEDs */
      case 0x244:
#if DEBUG_LED
         vm_log(vm,"IO_FPGA","LED register is now 0x%x (0x%x)\n",
                *data,(~*data) & 0x0F);
#endif
         break;

      /* ==== DUART SCN2681 (console/aux) ==== */
      case 0x404:   /* Mode Register A (MRA) */
         break;

      case 0x40c:   /* Status Register A (SRA) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(vm->vtty_con))
               odata |= DUART_RX_READY;

            odata |= DUART_TX_READY;
         
            vm_clear_irq(vm,C6MSFC1_DUART_IRQ);
            *data = odata;
         }
         break;

      case 0x414:   /* Command Register A (CRA) */
         /* Disable TX = High */
         if ((op_type == MTS_WRITE) && (*data & 0x8)) {
            vm->vtty_con->managed_flush = TRUE;          
            vtty_flush(vm->vtty_con);
         }
         break;

      case 0x41c:   /* RX/TX Holding Register A (RHRA/THRA) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(vm->vtty_con,(char)*data);
            d->duart_isr &= ~DUART_TXRDYA;
         } else {
            *data = vtty_get_char(vm->vtty_con);
            d->duart_isr &= ~DUART_RXRDYA;
         }
         break;

      case 0x424:   /* WRITE: Aux Control Register (ACR) */
         break;

      case 0x42c:   /* Interrupt Status/Mask Register (ISR/IMR) */
         if (op_type == MTS_WRITE) {
            d->duart_imr = *data;
         } else
            *data = d->duart_isr;
         break;

      case 0x434:   /* Counter/Timer Upper Value (CTU) */
      case 0x43c:   /* Counter/Timer Lower Value (CTL) */
      case 0x444:   /* Mode Register B (MRB) */
         break;

      case 0x44c:   /* Status Register B (SRB) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(vm->vtty_aux))
               odata |= DUART_RX_READY;

            odata |= DUART_TX_READY;
         
            //vm_clear_irq(vm,C6MSFC1_DUART_IRQ);
            *data = odata;
         }
         break;

      case 0x454:   /* Command Register B (CRB) */
         /* Disable TX = High */
         if ((op_type == MTS_WRITE) && (*data & 0x8)) {
            vm->vtty_aux->managed_flush = TRUE;
            vtty_flush(vm->vtty_aux);
         }
         break;

      case 0x45c:   /* RX/TX Holding Register B (RHRB/THRB) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(vm->vtty_aux,(char)*data);
            d->duart_isr &= ~DUART_TXRDYA;
         } else {
            *data = vtty_get_char(vm->vtty_aux);
            d->duart_isr &= ~DUART_RXRDYB;
         }
         break;

      case 0x46c:   /* WRITE: Output Port Configuration Register (OPCR) */
      case 0x474:   /* READ: Start Counter Command; */
                    /* WRITE: Set Output Port Bits Command */
      case 0x47c:   /* WRITE: Reset Output Port Bits Command */
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
         if (op_type == MTS_READ)
            *data = temp_read_data(d);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"IO_FPGA","read from addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,"IO_FPGA","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   IOFPGA_UNLOCK(d);
   return NULL;
}

/* Initialize EEPROM groups */
void c6msfc1_init_eeprom_groups(c6msfc1_t *router)
{
   router->sys_eeprom_g1 = eeprom_cpu_midplane;
   router->sys_eeprom_g1.eeprom[0] = &router->cpu_eeprom;
   router->sys_eeprom_g1.eeprom[1] = &router->mp_eeprom;
}

/* Shutdown the IO FPGA device */
void dev_c6msfc1_iofpga_shutdown(vm_instance_t *vm,struct iofpga_data *d)
{
   if (d != NULL) {
      IOFPGA_LOCK(d);
      vm->vtty_con->read_notifier = NULL;
      vm->vtty_aux->read_notifier = NULL;
      IOFPGA_UNLOCK(d);

      /* Remove the dummy IRQ periodic task */
      ptask_remove(d->duart_irq_tid);

      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_c6msfc1_iofpga_init()
 */
int dev_c6msfc1_iofpga_init(c6msfc1_t *router,m_uint64_t paddr,m_uint32_t len)
{
   vm_instance_t *vm = router->vm;
   struct iofpga_data *d;
   u_int i;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"IO_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   pthread_mutex_init(&d->lock,NULL);
   d->router = router;

   for(i=0;i<C6MSFC1_TEMP_SENSORS;i++) {
      d->temp_cfg_reg[i] = DS1620_CONFIG_STATUS_CPU;
      d->temp_deg_reg[i] = C6MSFC1_DEFAULT_TEMP * 2;
   }

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "io_fpga";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c6msfc1_iofpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "io_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_c6msfc1_iofpga_access;
   d->dev.priv_data = d;

   /* Set console and AUX port notifying functions */
   vm->vtty_con->priv_data = d;
   vm->vtty_aux->priv_data = d;
   vm->vtty_con->read_notifier = tty_con_input;
   vm->vtty_aux->read_notifier = tty_aux_input;

   /* Trigger periodically a dummy IRQ to flush buffers */
   d->duart_irq_tid = ptask_add((ptask_callback)tty_trigger_dummy_irq,
                                d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
