/*
 * 10/100 Mbps Ethernet PHY.
 *
 * Based on:
 *  Intel® LXT970A
 *  Dual-Speed Fast Ethernet Transceiver
 *  Order Number: 249099-001
 *  January 2001
 * Based on:
 *  IEEE Std 802.3-2008, Section 2
 */

#ifndef __DEV_LXT970A_H__
#define __DEV_LXT970A_H__

/*
Abreviations:
 RW=x - Read/Write with default value x
 RO=x - Read Only with default value x
 [xx] - default value determined by pins xx
 SC   - Self Clearing
 LH   - Latching High (remains High until read, and then returns to Low)
 LL   - Latching Low (remains Low until read, and then returns to High)
 IGN  - IGNored on certain conditions
 RTOR - Related To Other Register

Pin states used for the default values:
 MF0 = VMF2 or VMF3 (Enabled)
 MF1 = VMF1 or VMF4 (Disabled)
 MF2 = VMF2 or VMF3 (Enabled)
 MF3 = VMF2 or VMF3 (Enabled)
 MF4 = VMF2 or VMF3 (Enabled)
 CFG0 = High
 CFG1 = High
 TRSTE = Low
 FDE = High
 MDDIS = Low, RESET = High, PWERDWN = Low (2.4.1.2, MDIO Control) 
*/

/*
Standard MII Registers
*/

#define LX970A_CR                   0x00    /* Table 45.  Control Register (Address 0) */
#define LX970A_CR_RESET             0x8000  /* Reset (RW=0,SC) */
#define LX970A_CR_LOOP              0x4000  /* Loopback (RW=0) */
#define LX970A_CR_SPEEDSELECT       0x2000  /* Speed Selection (RW=1[CFG0],IGN) */
#define LX970A_CR_ANENABLE          0x1000  /* Auto-Negotiation Enable (RW=1[MF0]) */
#define LX970A_CR_POWERDOWN         0x0800  /* Power Down (RW=0?) */
#define LX970A_CR_ISOLATE           0x0400  /* Isolate (RW=0[TRSTE]) */
#define LX970A_CR_ANRESTART         0x0200  /* Restart Auto-Negotiation (RW=1[CFG0],SC) */
#define LX970A_CR_DUPLEXMODE        0x0100  /* Duplex Mode (RW=1[FDE],IGN) */
#define LX970A_CR_COLLISIONTEST     0x0080  /* Collision Test (RW=0,IGN) */
#define LX970A_CR_TTM               0x0070  /* Transceiver Test Mode (RO=0) */
#define LX970A_CR_MSENABLE          0x0008  /* Master-Slave Enable (RO=0) */
#define LX970A_CR_MSVALUE           0x0004  /* Master-Slave Value (RO=0) */
#define LX970A_CR_RESERVED          0x0003  /* Reserved (RW=0) */
#define LX970A_CR_RO_MASK           0x007C
#define LX970A_CR_RW_MASK           0xFF83
#define LX970A_CR_DEFAULT           0x3300

#define LX970A_SR                   0x01    /* Table 46.  Status Register (Address 1) */
#define LX970A_SR_100T4             0x8000  /* 100BASE-T4 (RO=0) */
#define LX970A_SR_100TX_FD          0x4000  /* 100BASE-X full-duplex (RO=1) */
#define LX970A_SR_100TX_HD          0x2000  /* 100BASE-X hald-duplex (RO=1) */
#define LX970A_SR_10T_FD            0x1000  /* 10 Mb/s full-duplex (RO=1) */
#define LX970A_SR_10T_HD            0x0800  /* 10 Mb/s half-duplex (RO=1) */
#define LX970A_SR_100T2_FD          0x0400  /* 100BASE-T2 full-duplex (RO=0) */
#define LX970A_SR_100T2_HD          0x0200  /* 100BASE-T2 half-duplex (RO=0) */
#define LX970A_SR_RESERVED          0x0100  /* Reserved (RO=0) */
#define LX970A_SR_MSCFGFAULT        0x0080  /* Master-Slave Configuration Fault (RO=0) */
#define LX970A_SR_MFPS              0x0040  /* MF Preamble Suppression (RO=0) */
#define LX970A_SR_ANCOMPLETE        0x0020  /* Auto-Neg. Complete (RO=0) */
#define LX970A_SR_REMOTEFAULT       0x0010  /* Remote Fault (RO=0,LH) */
#define LX970A_SR_ANABILITY         0x0008  /* Auto-Neg. Ability (RO=1) */
#define LX970A_SR_LINKSTATUS        0x0004  /* Link Status (RO=0,LL) */
#define LX970A_SR_JABBERDETECT      0x0002  /* Jabber Detect (10BASE-T Only) (RO=0,LH) */
#define LX970A_SR_EXTCAPABILITY     0x0001  /* Extended Capability (RO=1) */
#define LX970A_SR_RO_MASK           0xFFFF
#define LX970A_SR_RW_MASK           0x0000
#define LX970A_SR_DEFAULT           0x7809

/*
PHY ID number:
 Intel has OUI=00207Bh and ROUI=DE0400h (OUI with bits reversed)
 (ROUI << 10) & FFFFFFFFh = 78100000h
*/

#define LX970A_PIR1                 0x02    /* Table 47.  PHY Identification Register 1 (Address 2) */
#define LX970A_PIR1_PIN             0xFFFF  /* PHY ID Number (RO=7810h) */
#define LX970A_PIR1_RO_MASK         0xFFFF
#define LX970A_PIR1_RW_MASK         0x0000
#define LX970A_PIR1_DEFAULT         0x7810

#define LX970A_PIR2                 0x03    /* Table 48.  PHY Identification Register 2 (Address 3) */
#define LX970A_PIR2_PIN             0xFC00  /* PHY ID number (RO=000000b) */
#define LX970A_PIR2_MANMODELNUM     0x03F0  /* Manufacturer’s model number (RO=000000b) */
#define LX970A_PIR2_MANREVNUM       0x000F  /* Manufacturer’s revision number (RO=0011b) */
#define LX970A_PIR2_RO_MASK         0xFFFF
#define LX970A_PIR2_RW_MASK         0x0000
#define LX970A_PIR2_DEFAULT         0x0003

#define LX970A_ANAR                 0x04    /* Table 49.  Auto Negotiation Advertisement Register (Address 4) */
#define LX970A_ANAR_NEXTPAGE        0x8000  /* Next Page (RO=0) */
#define LX970A_ANAR_RESERVED_1      0x4000  /* Reserved (RO=0) */
#define LX970A_ANAR_REMOTEFAULT     0x2000  /* Remote Fault (RW=0) */
#define LX970A_ANAR_RESERVED_2      0x1800  /* Reserved (RW=0) */
#define LX970A_ANAR_PAUSE           0x0400  /* Pause (RW=0) */
#define LX970A_ANAR_100T4           0x0200  /* 100BASE-T4 (RW=0) */
#define LX970A_ANAR_100TX_FD        0x0100  /* 100BASE-TX (RW=1[FDE,MF4]) */
#define LX970A_ANAR_100TX_HD        0x0080  /* 100BASE-TX (RW=1[MF4]) */
#define LX970A_ANAR_10T_FD          0x0040  /* 10BASE-T full-duplex (RW=1[FDE,CFG1]) */
#define LX970A_ANAR_10T_HD          0x0020  /* 10BASE-T (RW=1[CFG1]) */
#define LX970A_ANAR_SF              0x001F  /* Selector Field, S<4:0> (RW=00001b) */
#define LX970A_ANAR_RO_MASK         0xC000
#define LX970A_ANAR_RW_MASK         0x3FFF
#define LX970A_ANAR_DEFAULT         0x01E1
/* auxiliary */
#define LX970A_ANAR_SF_IEEE8023     0x0001  /* <00001> = IEEE 802.3 */

#define LX970A_ANLPAR               0x05    /* Table 50.  Auto Negotiation Link Partner Ability Register (Address 5) */
#define LX970A_ANLPAR_NEXTPAGE      0x8000  /* Next Page (RO) */
#define LX970A_ANLPAR_ACKNOWLEDGE   0x4000  /* Acknowledge (RO) */
#define LX970A_ANLPAR_REMOTEFAULT   0x2000  /* Remote Fault (RO) */
#define LX970A_ANLPAR_RESERVED      0x1800  /* Reserved (RO) */
#define LX970A_ANLPAR_PAUSE         0x0400  /* Pause (RO) */
#define LX970A_ANLPAR_100T4         0x0200  /* 100BASE-T4 (RO) */
#define LX970A_ANLPAR_100TX_FD      0x0100  /* 100BASE-TX full-duplex (RO) */
#define LX970A_ANLPAR_100TX_HD      0x0080  /* 100BASE-TX (RO) */
#define LX970A_ANLPAR_10T_FD        0x0040  /* 10BASE-T full-duplex (RO) */
#define LX970A_ANLPAR_10T_HD        0x0020  /* 10BASE-T (RO) */
#define LX970A_ANLPAR_SF            0x001F  /* Selector Field S[4:0] (RO) */
#define LX970A_ANLPAR_RO_MASK       0xFFFF
#define LX970A_ANLPAR_RW_MASK       0x0000
#define LX970A_ANLPAR_DEFAULT       0x0000
/* auxiliary */
#define LX970A_ANLPAR_SF_IEEE8023   0x0001  /* <00001> = IEEE 802.3 */

#define LX970A_ANE                  0x06    /* Table 51.  Auto Negotiation Expansion (Address 6) */
#define LX970A_ANE_RESERVED         0xFFE0  /* Reserved (RO=0) */
#define LX970A_ANE_PDETECTFAULT     0x0010  /* Parallel Detection Fault (RO=0,LH) */
#define LX970A_ANE_LPNPA            0x0008  /* Link Partner Next Page Able (RO=0) */
#define LX970A_ANE_NPA              0x0004  /* Next Page Able (RO=0) */
#define LX970A_ANE_PR               0x0002  /* Page Received (RO=0,LH) */
#define LX970A_ANE_LPANA            0x0001  /* Link Partner Auto Neg Able (RO=0) */
#define LX970A_ANE_RO_MASK          0xFFFF
#define LX970A_ANE_RW_MASK          0x0000
#define LX970A_ANE_DEFAULT          0x0000

/*
Vendor Specific MII Registers
*/

#define LX970A_MR                   0x10    /* Table 52.  Mirror Register (Address 16, Hex 10) */
#define LX970A_MR_USERDEFINED       0xFFFF  /* User Defined (RW=0) */
#define LX970A_MR_RO_MASK           0x0000
#define LX970A_MR_RW_MASK           0xFFFF
#define LX970A_MR_DEFAULT           0x0000

#define LX970A_IER                  0x11    /* Table 53.  Interrupt Enable Register (Address 17, Hex 11) */
#define LX970A_IER_RESERVED         0xFFF0  /* Reserved (RO=0) */
#define LX970A_IER_MIIDRVLVL        0x0008  /* MIIDRVLVL (RW=0) */
#define LX970A_IER_LNK_CRITERIA     0x0004  /* LNK CRITERIA (RW=0) */
#define LX970A_IER_INTEN            0x0002  /* INTEN (RW=0) */
#define LX970A_IER_TINT             0x0001  /* TINT (RW=0,IGN) */
#define LX970A_IER_RO_MASK          0xFFF0
#define LX970A_IER_RW_MASK          0x000F
#define LX970A_IER_DEFAULT          0x0000

#define LX970A_ISR                  0x12    /* Table 54.  Interrupt Status Register (Address 18, Hex 12) */
#define LX970A_ISR_MINT             0x8000  /* MINT (RO=0?) */
#define LX970A_ISR_XTALOK           0x4000  /* XTALOK (RO=0) */
#define LX970A_ISR_RESERVED         0x3FFF  /* Reserved (RO=0) */
#define LX970A_ISR_RO_MASK          0xFFFF
#define LX970A_ISR_RW_MASK          0x0000
#define LX970A_ISR_DEFAULT          0x0000

#define LX970A_CFGR                 0x13    /* Table 55.  Configuration Register (Address 19, Hex 13) */
#define LX970A_CFGR_RESERVED_1      0x8000  /* Reserved (RO=0) */
#define LX970A_CFGR_TXMITTEST       0x4000  /* Txmit Test (100BASE-TX) (RW=0,RTOR) */
#define LX970A_CFGR_REPEATERMODE    0x2000  /* Repeater Mode (RW=0[MF1]) */
#define LX970A_CFGR_MDIOINT         0x1000  /* MDIO_INT (RW=0,IGN) */
#define LX970A_CFGR_TPLOOPBACK      0x0800  /* TP Loopback (10BASE-T) (RW=0) */
#define LX970A_CFGR_SQE             0x0400  /* SQE (10BASE-T) (RW=0) */
#define LX970A_CFGR_JABBER          0x0200  /* Jabber (10BASE-T) (RW=0) */
#define LX970A_CFGR_LINKTEST        0x0100  /* Link Test (10BASE-T) (RW=0[CFG1,MF0]) */
#define LX970A_CFGR_LEDC            0x00C0  /* LEDC Programming bits (RW=0) */
#define LX970A_CFGR_ATXC            0x0020  /* Advance TX Clock (RW=0) */
#define LX970A_CFGR_5BS_4BN         0x0010  /* 5B Symbol/(100BASE-X only) 4B Nibble (RW=1[MF2]) */
#define LX970A_CFGR_SCRAMBLER       0x0008  /* Scrambler (100BASE-X only) (RW=1[MF3]) */
#define LX970A_CFGR_100FX           0x0004  /* 100BASE-FX (RW=1[MF4,MF0]) */
#define LX970A_CFGR_RESERVED_2      0x0002  /* Reserved (RO=0) */
#define LX970A_CFGR_TD              0x0001  /* Transmit Disconnect (RW=0) */
#define LX970A_CFGR_RO_MASK         0x8002
#define LX970A_CFGR_RW_MASK         0x7FFD
#define LX970A_CFGR_DEFAULT         0x0014
/* auxiliary */
#define LX970A_CFGR_LEDC_COLLISION  0x0000  /* 0 0 LEDC indicates collision */
#define LX970A_CFGR_LEDC_OFF        0x0040  /* 0 1 LEDC is off */
#define LX970A_CFGR_LEDC_ACTIVITY   0x0080  /* 1 0 LEDC indicates activity */
#define LX970A_CFGR_LEDC_ALWAYSON   0x00C0  /* 1 1 LEDC is continuously on (for diagnostic use) */

#define LX970A_CSR                  0x14    /* Table 56.  Chip Status Register (Address 20, Hex 14) */
#define LX970A_CSR_RESERVED_1       0xC000  /* Reserved (RO=0?) */
#define LX970A_CSR_LINK             0x2000  /* Link (RO=0,RTOR) */
#define LX970A_CSR_DUPLEXMODE       0x1000  /* Duplex Mode (RO=1[FDE],RTOR) */
#define LX970A_CSR_SPEED            0x0800  /* Speed (RO=1[CFG0],RTOR) */
#define LX970A_CSR_RESERVED_2       0x0400  /* Reserved (RO=0?) */
#define LX970A_CSR_ANCOMPLETE       0x0200  /* Auto-Negotiation Complete (RO=0,LH,RTOR) */
#define LX970A_CSR_PAGERECEIVED     0x0100  /* Page Received (RO=0,LH,RTOR) */
#define LX970A_CSR_RESERVED_3       0x00C0  /* Reserved (RO=0) */
#define LX970A_CSR_RESERVED_4       0x0038  /* Reserved (RO=0?) */
#define LX970A_CSR_LOWVOLTAGE       0x0004  /* Low-Voltage (RO=0?) */
#define LX970A_CSR_RESERVED_5       0x0003  /* Reserved (RO=0?) */
#define LX970A_CSR_RO_MASK          0xFFFF
#define LX970A_CSR_RW_MASK          0x0000
#define LX970A_CSR_DEFAULT          0x1800

#endif
