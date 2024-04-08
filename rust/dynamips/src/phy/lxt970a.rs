//! 10/100 Mbps PHY
//!
//! # References:
//! * [Intel LXT970A Dual-Speed Fast Ethernet Transceiver, January 2001](https://github.com/flaviojs/dynamips-datasheets/blob/master/ethernet/phy/LXT970A.pdf)
//! * IEEE Std 802.3-2008, Section 2

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

/// Table 45.  Control Register (Address 0)
pub const LX970A_CR: usize = 0x00;
/// Reset (RW=0,SC)
pub const LX970A_CR_RESET: u16 = 0x8000;
/// Loopback (RW=0)
pub const LX970A_CR_LOOP: u16 = 0x4000;
/// Speed Selection (RW=1(CFG0),IGN)
pub const LX970A_CR_SPEEDSELECT: u16 = 0x2000;
/// Auto-Negotiation Enable (RW=1(MF0))
pub const LX970A_CR_ANENABLE: u16 = 0x1000;
/// Power Down (RW=0?)
pub const LX970A_CR_POWERDOWN: u16 = 0x0800;
/// Isolate (RW=0(TRSTE))
pub const LX970A_CR_ISOLATE: u16 = 0x0400;
/// Restart Auto-Negotiation (RW=1(CFG0),SC)
pub const LX970A_CR_ANRESTART: u16 = 0x0200;
/// Duplex Mode (RW=1(FDE),IGN)
pub const LX970A_CR_DUPLEXMODE: u16 = 0x0100;
/// Collision Test (RW=0,IGN)
pub const LX970A_CR_COLLISIONTEST: u16 = 0x0080;
/// Transceiver Test Mode (RO=0)
pub const LX970A_CR_TTM: u16 = 0x0070;
/// Master-Slave Enable (RO=0)
pub const LX970A_CR_MSENABLE: u16 = 0x0008;
/// Master-Slave Value (RO=0)
pub const LX970A_CR_MSVALUE: u16 = 0x0004;
/// Reserved (RW=0)
pub const LX970A_CR_RESERVED: u16 = 0x0003;
pub const LX970A_CR_RO_MASK: u16 = 0x007C;
pub const LX970A_CR_RW_MASK: u16 = 0xFF83;
pub const LX970A_CR_DEFAULT: u16 = 0x3300;

/// Table 46.  Status Register (Address 1)
pub const LX970A_SR: usize = 0x01;
/// 100BASE-T4 (RO=0)
pub const LX970A_SR_100T4: u16 = 0x8000;
/// 100BASE-X full-duplex (RO=1)
pub const LX970A_SR_100TX_FD: u16 = 0x4000;
/// 100BASE-X hald-duplex (RO=1)
pub const LX970A_SR_100TX_HD: u16 = 0x2000;
/// 10 Mb/s full-duplex (RO=1)
pub const LX970A_SR_10T_FD: u16 = 0x1000;
/// 10 Mb/s half-duplex (RO=1)
pub const LX970A_SR_10T_HD: u16 = 0x0800;
/// 100BASE-T2 full-duplex (RO=0)
pub const LX970A_SR_100T2_FD: u16 = 0x0400;
/// 100BASE-T2 half-duplex (RO=0)
pub const LX970A_SR_100T2_HD: u16 = 0x0200;
/// Reserved (RO=0)
pub const LX970A_SR_RESERVED: u16 = 0x0100;
/// Master-Slave Configuration Fault (RO=0)
pub const LX970A_SR_MSCFGFAULT: u16 = 0x0080;
/// MF Preamble Suppression (RO=0)
pub const LX970A_SR_MFPS: u16 = 0x0040;
/// Auto-Neg. Complete (RO=0)
pub const LX970A_SR_ANCOMPLETE: u16 = 0x0020;
/// Remote Fault (RO=0,LH)
pub const LX970A_SR_REMOTEFAULT: u16 = 0x0010;
/// Auto-Neg. Ability (RO=1)
pub const LX970A_SR_ANABILITY: u16 = 0x0008;
/// Link Status (RO=0,LL)
pub const LX970A_SR_LINKSTATUS: u16 = 0x0004;
/// Jabber Detect (10BASE-T Only) (RO=0,LH)
pub const LX970A_SR_JABBERDETECT: u16 = 0x0002;
/// Extended Capability (RO=1)
pub const LX970A_SR_EXTCAPABILITY: u16 = 0x0001;
pub const LX970A_SR_RO_MASK: u16 = 0xFFFF;
pub const LX970A_SR_RW_MASK: u16 = 0x0000;
pub const LX970A_SR_DEFAULT: u16 = 0x7809;

/*
PHY ID number:
 Intel has OUI=00207Bh and ROUI=DE0400h (OUI with bits reversed)
 (ROUI << 10) & FFFFFFFFh = 78100000h
*/

/// Table 47.  PHY Identification Register 1 (Address 2)
pub const LX970A_PIR1: usize = 0x02;
/// PHY ID Number (RO=7810h)
pub const LX970A_PIR1_PIN: u16 = 0xFFFF;
pub const LX970A_PIR1_RO_MASK: u16 = 0xFFFF;
pub const LX970A_PIR1_RW_MASK: u16 = 0x0000;
pub const LX970A_PIR1_DEFAULT: u16 = 0x7810;

/// Table 48.  PHY Identification Register 2 (Address 3)
pub const LX970A_PIR2: usize = 0x03;
/// PHY ID number (RO=000000b)
pub const LX970A_PIR2_PIN: u16 = 0xFC00;
/// Manufacturer’s model number (RO=000000b)
pub const LX970A_PIR2_MANMODELNUM: u16 = 0x03F0;
/// Manufacturer’s revision number (RO=0011b)
pub const LX970A_PIR2_MANREVNUM: u16 = 0x000F;
pub const LX970A_PIR2_RO_MASK: u16 = 0xFFFF;
pub const LX970A_PIR2_RW_MASK: u16 = 0x0000;
pub const LX970A_PIR2_DEFAULT: u16 = 0x0003;

/// Table 49.  Auto Negotiation Advertisement Register (Address 4)
pub const LX970A_ANAR: usize = 0x04;
/// Next Page (RO=0)
pub const LX970A_ANAR_NEXTPAGE: u16 = 0x8000;
/// Reserved (RO=0)
pub const LX970A_ANAR_RESERVED_1: u16 = 0x4000;
/// Remote Fault (RW=0)
pub const LX970A_ANAR_REMOTEFAULT: u16 = 0x2000;
/// Reserved (RW=0)
pub const LX970A_ANAR_RESERVED_2: u16 = 0x1800;
/// Pause (RW=0)
pub const LX970A_ANAR_PAUSE: u16 = 0x0400;
/// 100BASE-T4 (RW=0)
pub const LX970A_ANAR_100T4: u16 = 0x0200;
/// 100BASE-TX (RW=1(FDE,MF4))
pub const LX970A_ANAR_100TX_FD: u16 = 0x0100;
/// 100BASE-TX (RW=1(MF4))
pub const LX970A_ANAR_100TX_HD: u16 = 0x0080;
/// 10BASE-T full-duplex (RW=1(FDE,CFG1))
pub const LX970A_ANAR_10T_FD: u16 = 0x0040;
/// 10BASE-T (RW=1(CFG1))
pub const LX970A_ANAR_10T_HD: u16 = 0x0020;
/// Selector Field, S<4:0> (RW=00001b)
pub const LX970A_ANAR_SF: u16 = 0x001F;
pub const LX970A_ANAR_RO_MASK: u16 = 0xC000;
pub const LX970A_ANAR_RW_MASK: u16 = 0x3FFF;
pub const LX970A_ANAR_DEFAULT: u16 = 0x01E1;
/* auxiliary */
/// <00001> = IEEE 802.3
pub const LX970A_ANAR_SF_IEEE8023: u16 = 0x0001;

/// Table 50.  Auto Negotiation Link Partner Ability Register (Address 5)
pub const LX970A_ANLPAR: usize = 0x05;
/// Next Page (RO)
pub const LX970A_ANLPAR_NEXTPAGE: u16 = 0x8000;
/// Acknowledge (RO)
pub const LX970A_ANLPAR_ACKNOWLEDGE: u16 = 0x4000;
/// Remote Fault (RO)
pub const LX970A_ANLPAR_REMOTEFAULT: u16 = 0x2000;
/// Reserved (RO)
pub const LX970A_ANLPAR_RESERVED: u16 = 0x1800;
/// Pause (RO)
pub const LX970A_ANLPAR_PAUSE: u16 = 0x0400;
/// 100BASE-T4 (RO)
pub const LX970A_ANLPAR_100T4: u16 = 0x0200;
/// 100BASE-TX full-duplex (RO)
pub const LX970A_ANLPAR_100TX_FD: u16 = 0x0100;
/// 100BASE-TX (RO)
pub const LX970A_ANLPAR_100TX_HD: u16 = 0x0080;
/// 10BASE-T full-duplex (RO)
pub const LX970A_ANLPAR_10T_FD: u16 = 0x0040;
/// 10BASE-T (RO)
pub const LX970A_ANLPAR_10T_HD: u16 = 0x0020;
/// Selector Field S(4:0) (RO)
pub const LX970A_ANLPAR_SF: u16 = 0x001F;
pub const LX970A_ANLPAR_RO_MASK: u16 = 0xFFFF;
pub const LX970A_ANLPAR_RW_MASK: u16 = 0x0000;
pub const LX970A_ANLPAR_DEFAULT: u16 = 0x0000;
/* auxiliary */
/// <00001> = IEEE 802.3
pub const LX970A_ANLPAR_SF_IEEE8023: u16 = 0x0001;

/// Table 51.  Auto Negotiation Expansion (Address 6)
pub const LX970A_ANE: usize = 0x06;
/// Reserved (RO=0)
pub const LX970A_ANE_RESERVED: u16 = 0xFFE0;
/// Parallel Detection Fault (RO=0,LH)
pub const LX970A_ANE_PDETECTFAULT: u16 = 0x0010;
/// Link Partner Next Page Able (RO=0)
pub const LX970A_ANE_LPNPA: u16 = 0x0008;
/// Next Page Able (RO=0)
pub const LX970A_ANE_NPA: u16 = 0x0004;
/// Page Received (RO=0,LH)
pub const LX970A_ANE_PR: u16 = 0x0002;
/// Link Partner Auto Neg Able (RO=0)
pub const LX970A_ANE_LPANA: u16 = 0x0001;
pub const LX970A_ANE_RO_MASK: u16 = 0xFFFF;
pub const LX970A_ANE_RW_MASK: u16 = 0x0000;
pub const LX970A_ANE_DEFAULT: u16 = 0x0000;

/*
Vendor Specific MII Registers
*/

/// Table 52.  Mirror Register (Address 16, Hex 10)
pub const LX970A_MR: usize = 0x10;
/// User Defined (RW=0)
pub const LX970A_MR_USERDEFINED: u16 = 0xFFFF;
pub const LX970A_MR_RO_MASK: u16 = 0x0000;
pub const LX970A_MR_RW_MASK: u16 = 0xFFFF;
pub const LX970A_MR_DEFAULT: u16 = 0x0000;

/// Table 53.  Interrupt Enable Register (Address 17, Hex 11)
pub const LX970A_IER: usize = 0x11;
/// Reserved (RO=0)
pub const LX970A_IER_RESERVED: u16 = 0xFFF0;
/// MIIDRVLVL (RW=0)
pub const LX970A_IER_MIIDRVLVL: u16 = 0x0008;
/// LNK CRITERIA (RW=0)
pub const LX970A_IER_LNK_CRITERIA: u16 = 0x0004;
/// INTEN (RW=0)
pub const LX970A_IER_INTEN: u16 = 0x0002;
/// TINT (RW=0,IGN)
pub const LX970A_IER_TINT: u16 = 0x0001;
pub const LX970A_IER_RO_MASK: u16 = 0xFFF0;
pub const LX970A_IER_RW_MASK: u16 = 0x000F;
pub const LX970A_IER_DEFAULT: u16 = 0x0000;

/// Table 54.  Interrupt Status Register (Address 18, Hex 12)
pub const LX970A_ISR: usize = 0x12;
/// MINT (RO=0?)
pub const LX970A_ISR_MINT: u16 = 0x8000;
/// XTALOK (RO=0)
pub const LX970A_ISR_XTALOK: u16 = 0x4000;
/// Reserved (RO=0)
pub const LX970A_ISR_RESERVED: u16 = 0x3FFF;
pub const LX970A_ISR_RO_MASK: u16 = 0xFFFF;
pub const LX970A_ISR_RW_MASK: u16 = 0x0000;
pub const LX970A_ISR_DEFAULT: u16 = 0x0000;

/// Table 55.  Configuration Register (Address 19, Hex 13)
pub const LX970A_CFGR: usize = 0x13;
/// Reserved (RO=0)
pub const LX970A_CFGR_RESERVED_1: u16 = 0x8000;
/// Txmit Test (100BASE-TX) (RW=0,RTOR)
pub const LX970A_CFGR_TXMITTEST: u16 = 0x4000;
/// Repeater Mode (RW=0(MF1))
pub const LX970A_CFGR_REPEATERMODE: u16 = 0x2000;
/// MDIO_INT (RW=0,IGN)
pub const LX970A_CFGR_MDIOINT: u16 = 0x1000;
/// TP Loopback (10BASE-T) (RW=0)
pub const LX970A_CFGR_TPLOOPBACK: u16 = 0x0800;
/// SQE (10BASE-T) (RW=0)
pub const LX970A_CFGR_SQE: u16 = 0x0400;
/// Jabber (10BASE-T) (RW=0)
pub const LX970A_CFGR_JABBER: u16 = 0x0200;
/// Link Test (10BASE-T) (RW=0(CFG1,MF0))
pub const LX970A_CFGR_LINKTEST: u16 = 0x0100;
/// LEDC Programming bits (RW=0)
pub const LX970A_CFGR_LEDC: u16 = 0x00C0;
/// Advance TX Clock (RW=0)
pub const LX970A_CFGR_ATXC: u16 = 0x0020;
/// 5B Symbol/(100BASE-X only) 4B Nibble (RW=1(MF2))
pub const LX970A_CFGR_5BS_4BN: u16 = 0x0010;
/// Scrambler (100BASE-X only) (RW=1(MF3))
pub const LX970A_CFGR_SCRAMBLER: u16 = 0x0008;
/// 100BASE-FX (RW=1(MF4,MF0))
pub const LX970A_CFGR_100FX: u16 = 0x0004;
/// Reserved (RO=0)
pub const LX970A_CFGR_RESERVED_2: u16 = 0x0002;
/// Transmit Disconnect (RW=0)
pub const LX970A_CFGR_TD: u16 = 0x0001;
pub const LX970A_CFGR_RO_MASK: u16 = 0x8002;
pub const LX970A_CFGR_RW_MASK: u16 = 0x7FFD;
pub const LX970A_CFGR_DEFAULT: u16 = 0x0014;
/* auxiliary */
/// 0 0 LEDC indicates collision
pub const LX970A_CFGR_LEDC_COLLISION: u16 = 0x0000;
/// 0 1 LEDC is off
pub const LX970A_CFGR_LEDC_OFF: u16 = 0x0040;
/// 1 0 LEDC indicates activity
pub const LX970A_CFGR_LEDC_ACTIVITY: u16 = 0x0080;
/// 1 1 LEDC is continuously on (for diagnostic use)
pub const LX970A_CFGR_LEDC_ALWAYSON: u16 = 0x00C0;

/// Table 56.  Chip Status Register (Address 20, Hex 14)
pub const LX970A_CSR: usize = 0x14;
/// Reserved (RO=0?)
pub const LX970A_CSR_RESERVED_1: u16 = 0xC000;
/// Link (RO=0,RTOR)
pub const LX970A_CSR_LINK: u16 = 0x2000;
/// Duplex Mode (RO=1(FDE),RTOR)
pub const LX970A_CSR_DUPLEXMODE: u16 = 0x1000;
/// Speed (RO=1(CFG0),RTOR)
pub const LX970A_CSR_SPEED: u16 = 0x0800;
/// Reserved (RO=0?)
pub const LX970A_CSR_RESERVED_2: u16 = 0x0400;
/// Auto-Negotiation Complete (RO=0,LH,RTOR)
pub const LX970A_CSR_ANCOMPLETE: u16 = 0x0200;
/// Page Received (RO=0,LH,RTOR)
pub const LX970A_CSR_PAGERECEIVED: u16 = 0x0100;
/// Reserved (RO=0)
pub const LX970A_CSR_RESERVED_3: u16 = 0x00C0;
/// Reserved (RO=0?)
pub const LX970A_CSR_RESERVED_4: u16 = 0x0038;
/// Low-Voltage (RO=0?)
pub const LX970A_CSR_LOWVOLTAGE: u16 = 0x0004;
/// Reserved (RO=0?)
pub const LX970A_CSR_RESERVED_5: u16 = 0x0003;
pub const LX970A_CSR_RO_MASK: u16 = 0xFFFF;
pub const LX970A_CSR_RW_MASK: u16 = 0x0000;
pub const LX970A_CSR_DEFAULT: u16 = 0x1800;

/// 10/100 Mbps PHY
pub struct Lxt970A {
    pub regs: [u16; 32],
    pub last_read_was_sr: bool,
    /// temporary variable to preserve behavior
    pub has_nio: bool,
}
impl Default for Lxt970A {
    fn default() -> Self {
        let mut x = Self { regs: [0; 32], last_read_was_sr: false, has_nio: false };
        x.set_defaults();
        x
    }
}
impl Lxt970A {
    pub fn new() -> Self {
        Self::default()
    }
    /// Link status
    pub fn link_is_up(&self) -> bool {
        self.has_nio && (self.regs[LX970A_CR] & LX970A_CR_POWERDOWN) == 0
    }
    /// MII update registers
    pub fn update_regs(&mut self) {
        if self.link_is_up() {
            // link up, LX970A_SR_LINKSTATUS is latch down
            self.regs[LX970A_CSR] |= LX970A_CSR_LINK;
            if (self.regs[LX970A_CR] & LX970A_CR_ANENABLE) == 0 {
                // manual selection
                if (self.regs[LX970A_CR] & LX970A_CR_SPEEDSELECT) != 0 {
                    // 100Mbps
                    self.regs[LX970A_CSR] |= LX970A_CSR_SPEED;
                } else {
                    // 10 Mbps
                    self.regs[LX970A_CSR] &= !LX970A_CSR_SPEED;
                }
                if (self.regs[LX970A_CR] & LX970A_CR_DUPLEXMODE) != 0 {
                    // full duplex
                    self.regs[LX970A_CSR] |= LX970A_CSR_DUPLEXMODE;
                } else {
                    // half duplex
                    self.regs[LX970A_CSR] &= !LX970A_CSR_DUPLEXMODE;
                }
            } else {
                // auto-negotiation, assume partner is standard 10/100 eth
                self.regs[LX970A_ANLPAR] = LX970A_ANLPAR_ACKNOWLEDGE
                    | LX970A_ANLPAR_100TX_FD
                    | LX970A_ANLPAR_100TX_HD
                    | LX970A_ANLPAR_10T_FD
                    | LX970A_ANLPAR_10T_HD;
                if (self.regs[LX970A_ANAR] & LX970A_ANAR_100TX_FD) != 0 {
                    // 100Mbps full duplex
                    self.regs[LX970A_CSR] |= LX970A_CSR_DUPLEXMODE | LX970A_CSR_SPEED;
                } else if (self.regs[LX970A_ANAR] & LX970A_ANAR_100TX_HD) != 0 {
                    // 100Mbps half duplex
                    self.regs[LX970A_CSR] =
                        (self.regs[LX970A_CSR] & !LX970A_CSR_DUPLEXMODE) | LX970A_CSR_SPEED;
                } else if (self.regs[LX970A_ANAR] & LX970A_ANAR_10T_FD) != 0 {
                    // 10Mbps full duplex
                    self.regs[LX970A_CSR] =
                        LX970A_CSR_DUPLEXMODE | (self.regs[LX970A_CSR] & !LX970A_CSR_SPEED);
                } else {
                    // 10Mbps half duplex
                    self.regs[LX970A_ANAR] |= LX970A_ANAR_10T_HD;
                    self.regs[LX970A_CSR] &= !(LX970A_CSR_DUPLEXMODE | LX970A_CSR_SPEED);
                }
                self.regs[LX970A_CR] &= !(LX970A_CR_ANRESTART);
                self.regs[LX970A_SR] |= LX970A_SR_ANCOMPLETE;
                self.regs[LX970A_CSR] |= LX970A_CSR_ANCOMPLETE;
            }
        } else {
            // link down or administratively down
            self.regs[LX970A_SR] &= !(LX970A_SR_ANCOMPLETE | LX970A_SR_LINKSTATUS);
            self.regs[LX970A_CSR] &= !(LX970A_CSR_LINK
                | LX970A_CSR_DUPLEXMODE
                | LX970A_CSR_SPEED
                | LX970A_CSR_ANCOMPLETE);
        }
    }
    /// MII register defaults
    pub fn set_defaults(&mut self) {
        // default is 100Mb/s full duplex and auto-negotiation
        self.regs = [0; 32];
        self.regs[LX970A_CR] = LX970A_CR_DEFAULT;
        self.regs[LX970A_SR] = LX970A_SR_DEFAULT;
        self.regs[LX970A_PIR1] = LX970A_PIR1_DEFAULT;
        self.regs[LX970A_PIR2] = LX970A_PIR2_DEFAULT;
        self.regs[LX970A_ANAR] = LX970A_ANAR_DEFAULT;
        self.regs[LX970A_ANE] = LX970A_ANE_DEFAULT;
        self.regs[LX970A_MR] = LX970A_MR_DEFAULT;
        self.regs[LX970A_IER] = LX970A_IER_DEFAULT;
        self.regs[LX970A_ISR] = LX970A_ISR_DEFAULT;
        self.regs[LX970A_CFGR] = LX970A_CFGR_DEFAULT;
        self.regs[LX970A_CSR] = LX970A_CSR_DEFAULT;

        // chip is powered up and stable
        self.regs[LX970A_ISR] = LX970A_ISR_XTALOK;

        self.update_regs();
    }

    /// MII register read access
    pub fn mii_read_access(&mut self, reg: usize) -> u16 {
        let value = self.regs[reg];

        // update bits
        match reg {
            LX970A_SR => {
                // Latch Low
                if self.link_is_up() {
                    self.regs[reg] &= LX970A_SR_LINKSTATUS;
                }
            }
            LX970A_ISR => {
                if self.last_read_was_sr {
                    self.regs[reg] &= !LX970A_ISR_MINT;
                }
            }
            _ => {
                /* XXX Latch High:
                LX970A_SR_REMOTEFAULT, LX970A_SR_JABBERDETECT
                LX970A_ANE_PDETECTFAULT, LX970A_ANE_PR,
                LX970A_CSR_ANC, LX970A_CSR_PAGERECEIVED */
            }
        }

        self.last_read_was_sr = reg == LX970A_SR;
        value
    }

    /// MII register write access
    pub fn mii_write_access(&mut self, reg: usize, value: u16) {
        let mut update_regs = false;
        let (ro_mask, rw_mask) = match reg {
            LX970A_CR => {
                // reset, self clearing
                if (value & LX970A_CR_RESET) != 0 {
                    self.set_defaults();
                    return;
                }
                update_regs = true;
                (LX970A_CR_RO_MASK, LX970A_CR_RW_MASK)
            }
            LX970A_ANAR => (LX970A_ANAR_RO_MASK, LX970A_ANAR_RW_MASK),
            LX970A_MR => (LX970A_MR_RO_MASK, LX970A_MR_RW_MASK),
            LX970A_IER => (LX970A_IER_RO_MASK, LX970A_IER_RW_MASK),
            LX970A_CFGR => (LX970A_CFGR_RO_MASK, LX970A_CFGR_RW_MASK),
            _ => {
                // read-only register
                (0xFFFF, 0x0000)
            }
        };

        self.regs[reg] = (self.regs[reg] & ro_mask) | (value & rw_mask);
        if update_regs {
            self.update_regs();
        }
    }
}
