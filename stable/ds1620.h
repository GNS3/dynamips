#ifndef __DS1620_H__
#define __DS1620_H__

/*
 * These defines come from the DS1620 documentation.
 */
#define DS1620_READ_TEMP     0xAA
#define DS1620_WRITE_TH      0x01
#define DS1620_WRITE_TL      0x02
#define DS1620_READ_TH       0xA1
#define DS1620_READ_TL       0xA2
#define DS1620_START_CONVT   0xEE
#define DS1620_STOP_CONVT    0x22
#define DS1620_WRITE_CONFIG  0x0C
#define DS1620_READ_CONFIG   0xAC

#define DS1620_CONFIG_STATUS_DONE   0x80
#define DS1620_CONFIG_STATUS_THF    0x40
#define DS1620_CONFIG_STATUS_TLF    0x20
#define DS1620_CONFIG_STATUS_CPU    0x02
#define DS1620_CONFIG_STATUS_1SHOT  0x01

#define DS1620_WRITE_SIZE           8
#define DS1620_CONFIG_READ_SIZE     8
#define DS1620_DATA_READ_SIZE       9

#define DS1620_RESET_OFF    0x0f
#define DS1620_RESET_ON     0x00
#define DS1620_CLK_LOW      0x0f
#define DS1620_CLK_HIGH     0x1f

#define DS1620_DATA_HIGH    0x11
#define DS1620_DATA_LOW     0x10
#define DS1620_ENABLE_READ  0x00

#endif
