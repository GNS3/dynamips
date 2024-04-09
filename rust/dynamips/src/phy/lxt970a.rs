//! 10/100 Mbps PHY
//!
//! # References
//! * [Intel LXT970A Dual-Speed Fast Ethernet Transceiver, January 2001](https://github.com/flaviojs/dynamips-datasheets/blob/master/ethernet/phy/LXT970A.pdf)
//! * IEEE Std 802.3-2008, Section 2
//!
//! # MISSING
//! * tests
//! * timings
//! * errors (jabber, parallel detection, manual selection)
//! * some register fields

use std::fmt::Debug;
use tock_registers::interfaces::ReadWriteable;
use tock_registers::interfaces::Readable;
use tock_registers::interfaces::Writeable;
use tock_registers::register_bitfields;
use tock_registers::registers::InMemoryRegister;
use tock_registers::LocalRegisterCopy;
use tock_registers::RegisterLongName;

/// Assume the link partner is a similar 10/100 Mbps PHY and ack.
pub const LXT907A_AN_LP_ABILITY_ASSUMED: u16 = 0b0100_0001_1110_0001;

/// 10/100 Mbps PHY
#[derive(Debug)]
pub struct Lxt970A {
    pub regs: Regs,
    pub pins: Pins,
    pub last_read_was_status: bool,
    pub link_partner: Option<u16>,
}
impl Default for Lxt970A {
    fn default() -> Self {
        Self::new_with_pins(Pins::default())
    }
}
impl Lxt970A {
    /// Creates a new PHY with default values.
    pub fn new() -> Self {
        Self::default()
    }
    /// Creates a new PHY with values based on the pins.
    pub fn new_with_pins(pins: Pins) -> Self {
        Self { regs: Regs::new_with_pins(pins), pins, last_read_was_status: false, link_partner: None }
    }
    /// Set Link Partner (with an_lp_ability)
    pub fn set_link_partner(&mut self, link_partner: Option<u16>) {
        self.link_partner = link_partner;
        self.update_regs();
    }
    /// Link status
    pub fn link_is_up(&self) -> bool {
        self.regs.control.matches_all(Control::POWER_DOWN::No) && self.regs.chip_status.matches_all(ChipStatus::LINK::Up)
    }
    /// Update registers
    pub fn update_regs(&mut self) {
        if self.regs.control.matches_all(Control::RESET::Yes) {
            // reset
            self.regs = Regs::new_with_pins(self.pins); // SC
            debug_assert!(self.regs.control.matches_all(Control::RESET::No));
        }
        if self.regs.control.matches_all(Control::AN_ENABLE::Yes) {
            if self.regs.control.matches_all(Control::AN_RESTART::Yes) {
                // restart auto negotiation
                self.regs.status.modify(Status::AN_COMPLETE::No);
                self.regs.chip_status.modify(ChipStatus::LINK::Down);
                self.regs.control.modify(Control::AN_RESTART::No); // SC
            }
            if self.link_partner.is_some() && self.regs.chip_status.matches_all(ChipStatus::LINK::Down) {
                // do auto negotiation
                self.regs.an_lp_ability.set(self.link_partner.unwrap());
                //TODO self.regs.an_expansion.modify(AnExpansion::PD_FAULT::Yes); // LH
                self.regs.an_expansion.modify(AnExpansion::PAGE_RECEIVED::Yes); // LH
                self.regs.chip_status.modify(ChipStatus::PAGE_RECEIVED::Yes); // LH
                let mut remote_fault = false;
                if self.regs.an_lp_ability.matches_all(AnLpAbility::REMOTE_FAULT::Yes) || !self.regs.an_lp_ability.matches_all(AnLpAbility::SF::Ieee802Dot3) {
                    remote_fault = true;
                } else {
                    let mut overlap = self.regs.an_advertise.extract();
                    overlap.set(overlap.get() & self.regs.an_lp_ability.get());
                    if overlap.matches_all(AnAdvertise::ALLOW_100TX_FD::Yes) {
                        self.regs.chip_status.modify(ChipStatus::SPEED::Speed100Mbps);
                        self.regs.chip_status.modify(ChipStatus::DUPLEX::FullDuplex);
                    } else if overlap.matches_all(AnAdvertise::ALLOW_100TX_HD::Yes) {
                        self.regs.chip_status.modify(ChipStatus::SPEED::Speed100Mbps);
                        self.regs.chip_status.modify(ChipStatus::DUPLEX::HalfDuplex);
                    } else if overlap.matches_all(AnAdvertise::ALLOW_10T_FD::Yes) {
                        self.regs.chip_status.modify(ChipStatus::SPEED::Speed10Mbps);
                        self.regs.chip_status.modify(ChipStatus::DUPLEX::FullDuplex);
                    } else if overlap.matches_all(AnAdvertise::ALLOW_10T_HD::Yes) {
                        self.regs.chip_status.modify(ChipStatus::SPEED::Speed10Mbps);
                        self.regs.chip_status.modify(ChipStatus::DUPLEX::HalfDuplex);
                    } else {
                        remote_fault = true;
                    }
                }
                if remote_fault {
                    self.regs.status.modify(Status::REMOTE_FAULT::Yes); // LH
                } else {
                    // auto negotiation complete
                    self.regs.status.modify(Status::AN_COMPLETE::Yes);
                    self.regs.chip_status.modify(ChipStatus::LINK::Up);
                }
            }
        } else if self.link_partner.is_some() {
            // do manual selection
            let link_partner = LocalRegisterCopy::<u16, AnLpAbility::Register>::new(self.link_partner.unwrap());
            let mut fault = false;
            let allow_100tx_fd = link_partner.matches_all(AnLpAbility::ALLOW_100TX_FD::Yes);
            let allow_100tx_hd = link_partner.matches_all(AnLpAbility::ALLOW_100TX_HD::Yes);
            if (allow_100tx_fd || allow_100tx_hd) && self.regs.control.matches_all(Control::SPEED::Speed100Mbps) {
                self.regs.chip_status.modify(ChipStatus::SPEED::Speed100Mbps);
                if allow_100tx_fd && self.regs.control.matches_all(Control::DUPLEX::FullDuplex) {
                    self.regs.chip_status.modify(ChipStatus::DUPLEX::FullDuplex);
                } else {
                    self.regs.chip_status.modify(ChipStatus::DUPLEX::HalfDuplex);
                }
            } else {
                self.regs.chip_status.modify(ChipStatus::SPEED::Speed10Mbps);
                let allow_10t_fd = link_partner.matches_all(AnLpAbility::ALLOW_10T_FD::Yes);
                let allow_10t_hd = link_partner.matches_all(AnLpAbility::ALLOW_10T_HD::Yes);
                if allow_10t_fd && self.regs.control.matches_all(Control::DUPLEX::FullDuplex) {
                    self.regs.chip_status.modify(ChipStatus::DUPLEX::FullDuplex);
                } else if allow_10t_hd {
                    self.regs.chip_status.modify(ChipStatus::DUPLEX::HalfDuplex);
                } else {
                    fault = true;
                }
            }
            if fault {
                //TODO how do I signal an error?
                self.regs.chip_status.matches_all(ChipStatus::LINK::Down);
            } else {
                self.regs.chip_status.matches_all(ChipStatus::LINK::Up);
            }
        } else {
            self.regs.chip_status.matches_all(ChipStatus::LINK::Down);
        }
        // latches
        if self.regs.chip_status.matches_all(ChipStatus::LINK::Down) {
            self.regs.status.modify(Status::LINK::Down); // LL
        }
        //TODO self.regs.status.modify(Status::JABBER_DETECTED::Yes); // LH
        if self.regs.status.matches_all(Status::AN_COMPLETE::Yes) {
            self.regs.chip_status.modify(ChipStatus::AN_COMPLETE::Yes); // LH
        }
        // chip is powered up and stable
        self.regs.int_status.modify(IntStatus::XTAL::Stable);
    }
    /// Set register defaults.
    pub fn set_defaults(&mut self) {
        self.regs = Regs::new_with_pins(self.pins);
        self.update_regs();
    }
    /// MII register read access
    pub fn mii_read_access(&mut self, idx: usize) -> Option<u16> {
        if self.regs.control.matches_all(Control::ISOLATE::Yes) {
            return None;
        }
        let value = match idx {
            Regs::IDX_CONTROL => self.regs.control.get(),
            Regs::IDX_STATUS => self.regs.status.get(),
            Regs::IDX_ID1 => self.regs.id1.get(),
            Regs::IDX_ID2 => self.regs.id2.get(),
            Regs::IDX_AN_ADVERTISE => self.regs.an_advertise.get(),
            Regs::IDX_AN_LP_ABILITY => self.regs.an_lp_ability.get(),
            Regs::IDX_AN_EXPANSION => self.regs.an_expansion.get(),
            Regs::IDX_MIRROR => self.regs.mirror.get(),
            Regs::IDX_INT_ENABLE => self.regs.int_enable.get(),
            Regs::IDX_INT_STATUS => self.regs.int_status.get(),
            Regs::IDX_CONFIG => self.regs.config.get(),
            Regs::IDX_CHIP_STATUS => self.regs.chip_status.get(),
            _ => 0,
        };
        match idx {
            Regs::IDX_STATUS => {
                let mut reg = self.regs.status.extract();
                reg.modify(Status::REMOTE_FAULT::No); // LH
                reg.modify(Status::LINK::Up); // LL
                reg.modify(Status::JABBER_DETECTED::No); // LH
                self.regs.status.set(reg.get());
            }
            Regs::IDX_AN_EXPANSION => {
                let mut reg = self.regs.an_expansion.extract();
                reg.modify(AnExpansion::PD_FAULT::No); // LH
                reg.modify(AnExpansion::PAGE_RECEIVED::No); // LH
                self.regs.an_expansion.set(reg.get());
            }
            Regs::IDX_INT_STATUS => {
                if self.last_read_was_status {
                    self.regs.int_status.modify(IntStatus::MII_INT::No); // LH+1
                }
            }
            Regs::IDX_CHIP_STATUS => {
                let mut reg = self.regs.chip_status.extract();
                reg.modify(ChipStatus::AN_COMPLETE::No); // LH
                reg.modify(ChipStatus::PAGE_RECEIVED::No); // LH
                self.regs.chip_status.set(reg.get());
            }
            _ => {}
        }
        self.last_read_was_status = idx == Regs::IDX_STATUS;
        self.update_regs();
        Some(value)
    }

    /// MII register write access
    pub fn mii_write_access(&mut self, idx: usize, value: u16) -> Option<()> {
        if self.regs.control.matches_all(Control::ISOLATE::Yes) {
            // TODO how are you supposed to clear this bit if you can't write?
            return None;
        }
        fn update<R: RegisterLongName>(reg: &mut InMemoryRegister<u16, R>, rw_mask: u16, value: u16) {
            reg.set((reg.get() & !rw_mask) | (value & rw_mask));
        }
        match idx {
            Regs::IDX_CONTROL => update(&mut self.regs.control, Regs::RW_CONTROL, value),
            Regs::IDX_AN_ADVERTISE => update(&mut self.regs.an_advertise, Regs::RW_AN_ADVERTISE, value),
            Regs::IDX_MIRROR => update(&mut self.regs.mirror, Regs::RW_MIRROR, value),
            Regs::IDX_INT_ENABLE => update(&mut self.regs.int_enable, Regs::RW_INT_ENABLE, value),
            Regs::IDX_CONFIG => update(&mut self.regs.config, Regs::RW_CONFIG, value),
            _ => {}
        };
        self.update_regs();
        Some(())
    }
}

/// Pins that affect the chip.
#[derive(Debug, Copy, Clone)]
pub struct Pins {
    pub mf0: bool,
    pub mf1: bool,
    pub mf2: bool,
    pub mf3: bool,
    pub mf4: bool,
    pub cfg0: bool,
    pub cfg1: bool,
    pub fde: bool,
    pub trste: bool,
}
impl Default for Pins {
    fn default() -> Self {
        Self {
            mf0: true,    // Auto Negotiate
            mf1: false,   // DTE Mode
            mf2: true,    // 5B Symbol
            mf3: true,    // Bypass Scrambler
            mf4: true,    // 100 Mbps Capability
            cfg0: true,   // Auto Negotiate Restart
            cfg1: true,   // 10 Mbps Capability
            fde: true,    // Full Duplex
            trste: false, // Do Not Isolate
        }
    }
}
impl Pins {
    pub fn new() -> Self {
        Self::default()
    }
}

/// In-memory registers.
pub struct Regs {
    pub control: InMemoryRegister<u16, Control::Register>,
    pub status: InMemoryRegister<u16, Status::Register>,
    pub id1: InMemoryRegister<u16, Id1::Register>,
    pub id2: InMemoryRegister<u16, Id2::Register>,
    pub an_advertise: InMemoryRegister<u16, AnAdvertise::Register>,
    pub an_lp_ability: InMemoryRegister<u16, AnLpAbility::Register>,
    pub an_expansion: InMemoryRegister<u16, AnExpansion::Register>,
    pub mirror: InMemoryRegister<u16, Mirror::Register>,
    pub int_enable: InMemoryRegister<u16, IntEnable::Register>,
    pub int_status: InMemoryRegister<u16, IntStatus::Register>,
    pub config: InMemoryRegister<u16, Config::Register>,
    pub chip_status: InMemoryRegister<u16, ChipStatus::Register>,
}
impl Regs {
    pub const IDX_CONTROL: usize = 0;
    pub const IDX_STATUS: usize = 1;
    pub const IDX_ID1: usize = 2;
    pub const IDX_ID2: usize = 3;
    pub const IDX_AN_ADVERTISE: usize = 4;
    pub const IDX_AN_LP_ABILITY: usize = 5;
    pub const IDX_AN_EXPANSION: usize = 6;
    pub const IDX_MIRROR: usize = 16;
    pub const IDX_INT_ENABLE: usize = 17;
    pub const IDX_INT_STATUS: usize = 18;
    pub const IDX_CONFIG: usize = 19;
    pub const IDX_CHIP_STATUS: usize = 20;
    pub const RW_CONTROL: u16 = 0b1111_1111_1000_0011;
    pub const RW_AN_ADVERTISE: u16 = 0b0011_1111_1111_1111;
    pub const RW_MIRROR: u16 = 0b1111_1111_1111_1111;
    pub const RW_INT_ENABLE: u16 = 0b0000_0000_0000_1111;
    pub const RW_CONFIG: u16 = 0b1111_1111_1111_1111;
    /// Create registers with default values.
    pub fn new() -> Regs {
        Regs::default()
    }
    /// Create registers with values based on the pins.
    pub fn new_with_pins(pins: Pins) -> Regs {
        let regs = Regs::default();
        regs.control.set({
            let mut reg = regs.control.extract();
            if pins.mf0 {
                reg.modify(Control::AN_ENABLE::Yes);
                if pins.cfg0 {
                    reg.modify(Control::AN_RESTART::Yes);
                }
            } else {
                if pins.cfg0 {
                    reg.modify(Control::SPEED::Speed100Mbps);
                }
                if pins.fde {
                    reg.modify(Control::DUPLEX::FullDuplex);
                }
            }
            if pins.trste {
                reg.modify(Control::ISOLATE::Yes);
            }
            reg.into()
        });
        regs.an_advertise.set({
            let mut reg = regs.an_advertise.extract();
            if pins.mf4 {
                reg.modify(AnAdvertise::ALLOW_100TX_HD::Yes);
                if pins.fde {
                    reg.modify(AnAdvertise::ALLOW_100TX_FD::Yes);
                }
            }
            if pins.cfg1 {
                reg.modify(AnAdvertise::ALLOW_10T_HD::Yes);
                if pins.fde {
                    reg.modify(AnAdvertise::ALLOW_10T_FD::Yes);
                }
            }
            reg.into()
        });
        regs.config.set({
            let mut reg = regs.config.extract();
            if !pins.mf0 {
                if pins.cfg1 {
                    reg.modify(Config::LINK_TEST::No);
                }
                if pins.mf4 {
                    reg.modify(Config::CABLE_100FX::Fiber);
                }
            }
            if pins.mf1 {
                reg.modify(Config::MODE::Repeater);
            }
            if pins.mf2 {
                reg.modify(Config::CODER::Symbol5b);
            }
            if pins.mf3 {
                reg.modify(Config::SCRAMBLER::Bypass);
            }
            reg.into()
        });
        regs
    }
}
impl Default for Regs {
    fn default() -> Regs {
        Regs {
            control: InMemoryRegister::new(0),
            status: InMemoryRegister::new(0b0111_1000_0000_1001),
            id1: InMemoryRegister::new(0x7810),
            id2: InMemoryRegister::new(0b000000_000000_0001),
            an_advertise: InMemoryRegister::new(0b00_0_00_000000_00001),
            an_lp_ability: InMemoryRegister::new(0),
            an_expansion: InMemoryRegister::new(0),
            mirror: InMemoryRegister::new(0),
            int_enable: InMemoryRegister::new(0),
            int_status: InMemoryRegister::new(0),
            config: InMemoryRegister::new(0),
            chip_status: InMemoryRegister::new(0),
        }
    }
}
impl Debug for Regs {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        fmt.debug_struct("Regs")
            .field("control", &format_args!("0x{:04x}", self.control.get()))
            .field("status", &format_args!("0x{:04x}", self.status.get()))
            .field("id1", &format_args!("0x{:04x}", self.id1.get()))
            .field("id2", &format_args!("0x{:04x}", self.id2.get()))
            .field("an_advertise", &format_args!("0x{:04x}", self.an_advertise.get()))
            .field("an_lp_ability", &format_args!("0x{:04x}", self.an_lp_ability.get()))
            .field("an_expansion", &format_args!("0x{:04x}", self.an_expansion.get()))
            .field("mirror", &format_args!("0x{:04x}", self.mirror.get()))
            .field("ier", &format_args!("0x{:04x}", self.int_enable.get()))
            .field("isr", &format_args!("0x{:04x}", self.int_status.get()))
            .field("config", &format_args!("0x{:04x}", self.config.get()))
            .field("chip_status", &format_args!("0x{:04x}", self.chip_status.get()))
            .finish()
    }
}

/*
Abreviations:
 RW=x - Read/Write with default value x
 RO=x - Read Only with default value x
 SC   - Self Clearing
 LH   - Latching High (remains High until read, and then returns to Low)
 LL   - Latching Low (remains Low until read, and then returns to High)
*/

register_bitfields! [
    u16,

    /*
    Standard MII Registers
    */

    /// Table 45. Control Register (Address 0)
    Control [
        /// 0.15 Reset, 1=Yes, 0=No (RW=0,SC)
        RESET   OFFSET(15) NUMBITS(1) [Yes = 1, No = 0],
        /// 0.14 Loopback, 1=Enable, 0=Disable (RW=0)
        LOOP   OFFSET(14) NUMBITS(1) [],
        /// 0.13 Speed Selection, 1=100 Mbps, 0=10 Mbps (RW=?/CFG0&!MF0)
        SPEED   OFFSET(13) NUMBITS(1) [Speed100Mbps = 1, Speed10Mbps = 0],
        /// 0.12 Auto Negotiation, 1=Enable, 0=Disable (RW=MF0)
        AN_ENABLE   OFFSET(12) NUMBITS(1) [Yes = 1, No = 0],
        /// 0.11 Power Down, 1=Yes, 0=No (RW=0)
        /// When leaving Power Down, you must wait 500 ns minimum before you can write to registers.
        POWER_DOWN   OFFSET(11) NUMBITS(1) [Yes = 1, No = 0],
        /// 0.10 Electrically Isolate from MII, 1=Enable, 0=Normal (RW=TRSTE)
        ISOLATE   OFFSET(10) NUMBITS(1) [Yes = 1, No = 0],
        /// 0.9 Restart Auto Negotiation, 1=Enable, 0=Normal (RW=0/CFG0&MF0,SC)
        AN_RESTART   OFFSET(9) NUMBITS(1) [Yes = 1, No = 0],
        /// 0.8 Duplex Mode, 1=Full Duplex, 0=Half DUplex (RW=0/FDE&!MF0)
        DUPLEX   OFFSET(8) NUMBITS(1) [FullDuplex = 1, HalfDuplex = 0],
        /// 0.7 Collision Test, 1=Enable COL signal test (with loopback), 0=Disable (RW=0)
        COLLISION_TEST   OFFSET(7) NUMBITS(1) [],
        /// 0.6:4 Transceiver Test Mode, 0=Not Supported (RO=0)
        TTM   OFFSET(4) NUMBITS(3) [],
        /// 0.3 Master-Slave Enable, 0=Not Supported (RO=0)
        MS_ENABLE   OFFSET(3) NUMBITS(1) [],
        /// 0.2 Master-Slave Value, 0=Not Supported (RO=0)
        MS_VALUE   OFFSET(2) NUMBITS(1) [],
        /// 0.1:0 Reserved (RW=0)
        RESERVED   OFFSET(0) NUMBITS(2) [],
    ],
    /// Table 46. Status Register (Address 1)
    Status [
        /// 1.15 100BASE-T4, 0=Not Supported (RO=0)
        ABLE_100T4   OFFSET(15) NUMBITS(1) [],
        /// 1.14 100BASE-X Full-Duplex, 1=Supported (RO=1)
        ABLE_100X_FD   OFFSET(14) NUMBITS(1) [],
        /// 1.13 100BASE-X Half-Duplex, 1=Supported (RO=1)
        ABLE_100X_HD   OFFSET(13) NUMBITS(1) [],
        /// 1.12 10 Mbps Full-Duplex, 1=Supported (RO=1)
        ABLE_10_FD   OFFSET(12) NUMBITS(1) [],
        /// 1.11 10 Mbps Half-Duplex, 1=Supported (RO=1)
        ABLE_10_HD   OFFSET(11) NUMBITS(1) [],
        /// 1.10 100BASE-T2 Full-Duplex, 0=Not Supported (RO=0)
        ABLE_100T2_FD   OFFSET(10) NUMBITS(1) [],
        /// 1.9 100BASE-T2 Half-Duplex, 0=Not Supported (RO=0)
        ABLE_100T2_HD   OFFSET(9) NUMBITS(1) [],
        /// 1.8 Reserved (RO=0)
        RESERVED   OFFSET(8) NUMBITS(1) [],
        /// 1.7 Master-Slave Configuration Fault, 0=Not Supported (RO=0)
        MS_CFG_FAULT   OFFSET(7) NUMBITS(1) [],
        /// 1.6 MF Preamble Suppression, 0=Not Supported (RO=0)
        MF_PS   OFFSET(6) NUMBITS(1) [],
        /// 1.5 Auto Negotiation Complete, 1=Complete, 0=Not Complete (RO=0)
        AN_COMPLETE   OFFSET(5) NUMBITS(1) [Yes = 1, No = 0],
        /// 1.4 Remote Fault in Link Partner, 1=Yes, 0=No (RO=0,LH)
        REMOTE_FAULT   OFFSET(4) NUMBITS(1) [Yes = 1, No = 0],
        /// 1.3 Auto Negotiation Ability, 1=Supported (RO=1)
        AN_ABILITY   OFFSET(3) NUMBITS(1) [],
        /// 1.2 Link Status, 1=Up, 0=Down (RO=0,LL)
        LINK   OFFSET(2) NUMBITS(1) [Up = 1, Down = 0],
        /// 1.1 Jabber Detected (10BASE-T Only), 1=Yes, 0=No (RO=0,LH)
        JABBER_DETECTED   OFFSET(1) NUMBITS(1) [Yes = 1, No = 0],
        /// 1.0 Extended Capability, 1=Extended (RO=1)
        EXT_CAPABILITY   OFFSET(1) NUMBITS(1) [],
    ],

    /*
    PHY ID number:
     Intel has OUI=00207Bh and ROUI=DE0400h (OUI with bits reversed)
     (ROUI << 10) & FFFFFFFFh = 78100000h
    */

    /// Table 47. PHY Identification Register 1 (Address 2)
    Id1 [
        /// 2.15:0 PHY ID Number (RO=0x7810)
        ID   OFFSET(0) NUMBITS(16) [],
    ],
    /// Table 48. PHY Identification Register 2 (Address 3)
    Id2 [
        /// 3.15:10 PHY ID number (RO=0b000000)
        ID   OFFSET(10) NUMBITS(6) [],
        /// 3.9:4 Manufacturer’s model number (RO=0b000000)
        MODEL   OFFSET(4) NUMBITS(6) [],
        /// 3.3:0 Manufacturer’s revision number (RO=0b0011)
        REV   OFFSET(0) NUMBITS(4) [],
    ],
    /// Table 49. Auto Negotiation Advertisement Register (Address 4)
    AnAdvertise [
        /// 4.15 Next Page, 0=Not Supported (RO=0)
        NEXT_PAGE   OFFSET(15) NUMBITS(1) [],
        /// 4.14 Reserved (RO=0)
        RESERVED_1   OFFSET(14) NUMBITS(1) [],
        /// 4.13 Remote Fault, 1=Yes, 0=No (RW=0)
        REMOTE_FAULT   OFFSET(13) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.12:11 Reserved (RW=0)
        RESERVED_2   OFFSET(11) NUMBITS(2) [],
        /// 4.10 Allow Pause for Full-Duplex, 1=Yes, 0=No (RW=0)
        ALLOW_PAUSE_FD   OFFSET(10) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.9 Allow 100BASE-T4, 1=Yes, 0=No (RW=0)
        ALLOW_100T4   OFFSET(9) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.8 Allow 100BASE-TX 1=Yes, 0=No (RW=FDE&MF4)
        ALLOW_100TX_FD   OFFSET(8) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.7 Allow 100BASE-TX 1=Yes, 0=No (RW=MF4)
        ALLOW_100TX_HD   OFFSET(7) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.6 Allow 10BASE-T full-duplex (RW=FDE&CFG1)
        ALLOW_10T_FD   OFFSET(6) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.5 Allow 10BASE-T (RW=CFG1)
        ALLOW_10T_HD   OFFSET(5) NUMBITS(1) [Yes = 1, No = 0],
        /// 4.4:0 Selector Field, 0b00001=IEEE 802.3, 0b00010=IEEE 802.9 ISLAN-16T, 0b00000=Reserved, 0b11111=Reserved (RW=0b00001)
        SF   OFFSET(0) NUMBITS(5) [Ieee802Dot3 = 0b00001],
    ],
    /// Table 50. Auto Negotiation Link Partner Ability Register (Address 5)
    AnLpAbility [
        /// 5.15 Next Page (RO)
        NEXT_PAGE   OFFSET(15) NUMBITS(1) [],
        /// 5.14 Acknowledge (RO)
        ACK   OFFSET(14) NUMBITS(1) [],
        /// 5.13 Remote Fault (RO)
        REMOTE_FAULT   OFFSET(13) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.12:11 Reserved (RO)
        RESERVED   OFFSET(11) NUMBITS(2) [],
        /// 5.10 Allow Pause for Full-Duplex (RO)
        ALLOW_PAUSE_FD   OFFSET(12) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.9 Allow 100BASE-T4 (RO)
        ALLOW_100T4   OFFSET(9) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.8 Allow 100BASE-TX Full-Duplex (RO)
        ALLOW_100TX_FD   OFFSET(8) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.7 Allow 100BASE-TX Half-Duplex (RO)
        ALLOW_100TX_HD   OFFSET(7) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.6 Allow 10BASE-T Full-Duplex (RO)
        ALLOW_10T_FD   OFFSET(6) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.5 10 Allow BASE-T Half-Duplex (RO)
        ALLOW_10T_HD   OFFSET(5) NUMBITS(1) [Yes = 1, No = 0],
        /// 5.4:0 Selector Field (RO)
        SF   OFFSET(0) NUMBITS(5) [Ieee802Dot3 = 0b0001],
    ],
    /// Table 51. Auto Negotiation Expansion (Address 6)
    AnExpansion [
        /// 6.15:5 Reserved (RO=0)
        RESERVED   OFFSET(5) NUMBITS(11) [],
        /// 6.4 Parallel Detection Fault, 1=Yes, 0=No (RO=0,LH)
        PD_FAULT   OFFSET(4) NUMBITS(1) [Yes = 1, No = 0],
        /// 6.3 Link Partner Next Page Able, 1=Capable, 0=Incapable (RO=0)
        LP_NPA   OFFSET(3) NUMBITS(1) [],
        /// 6.2 Next Page Able, 0=Not Supported (RO=0)
        NPA   OFFSET(2) NUMBITS(1) [],
        /// 6.1 Page Received (3 consecutive identical), 1=Yes, 0=No (RO=0,LH)
        PAGE_RECEIVED   OFFSET(1) NUMBITS(1) [Yes = 1, No = 0],
        /// 6.0 Link Partner Auto Negotiation Able, 1=Capable, 0=Incapable (RO=0)
        LP_ANA   OFFSET(0) NUMBITS(1) [],
    ],

    /*
    Vendor Specific MII Registers
    */

    /// Table 52. Mirror Register (Address 16, Hex 10)
    Mirror [
        /// 16.15:0 User Defined (RW=0)
        USER_DEFINED   OFFSET(0) NUMBITS(16) [],
    ],
    /// Table 53. Interrupt Enable Register (Address 17, Hex 11)
    IntEnable [
        /// 17.15:4 Reserved (RO=0)
        RESERVED   OFFSET(4) NUMBITS(12) [],
        /// 17.3 MII Driver Levels, 1=Reduced, 0=High Strength (RW=0)
        MII_DRV_LVL   OFFSET(3) NUMBITS(1) [Reduced = 1, HighStrength = 0],
        /// 17.2 Link Criteria, 1=Enhanced, 0=Standard (RW=0)
        LNK_CRITERIA   OFFSET(2) NUMBITS(1) [Enhanced = 1, Standard = 0],
        /// 17.1 Enable Interrupts, 1=Enable, 0=Disable (RW=0)
        INT_ENABLE   OFFSET(1) NUMBITS(1) [],
        /// 17.0 Trigger Interrupt, 1=Force, 0=Normal (RW=0)
        INT_TRIGGER   OFFSET(0) NUMBITS(1) [],
    ],
    /// Table 54. Interrupt Status Register (Address 18, Hex 12)
    IntStatus [
        /// 18.15 MII Interrupt Pending, 1=Yes, 0=No (RO=0,LH+1)
        MII_INT   OFFSET(15) NUMBITS(1) [Yes = 1, No = 0],
        /// 18.14 XTAL OK, 1=Stable, 0=Unstable (RO=0)
        XTAL   OFFSET(14) NUMBITS(1) [Stable = 1, Unstable = 0],
        /// 18.13:0 Reserved (RO=0)
        RESERVED   OFFSET(0) NUMBITS(14) [],
    ],
    /// Table 55. Configuration Register (Address 19, Hex 13)
    Config [
        /// 19.15 Reserved (RO=0)
        RESERVED_1   OFFSET(15) NUMBITS(1) [],
        /// 19.14 Txmit Test (100BASE-TX), 1=Enabled, 0=Disabled (RW=0)
        TXMIT_TEST   OFFSET(14) NUMBITS(1) [],
        /// 19.13 Mode, 1=Repeater, 0=Normal (RW=MF1)
        MODE   OFFSET(13) NUMBITS(1) [Repeater = 1, Normal = 0],
        /// 19.12 MDIO Interrupts, 1=Enabled, 0=Normal (RW=0)
        MDIO_INT   OFFSET(12) NUMBITS(1) [],
        /// 19.11 TP Loopback (10BASE-T), 1=Disable, 0=Enable (RW=0)
        TP_LOOPBACK   OFFSET(11) NUMBITS(1) [],
        /// 19.10 SQE (10BASE-T), 1=Enable, 0=Disable (RW=0)
        SQE   OFFSET(10) NUMBITS(1) [],
        /// 19.9 Jabber (10BASE-T), 1=Disable, 0=Enable (RW=0)
        NO_JABBER   OFFSET(9) NUMBITS(1) [],
        /// 19.8 Link Test (10BASE-T), 1=Disable, 0=Enable (RW=CFG1&!MF0)
        LINK_TEST   OFFSET(8) NUMBITS(1) [No = 1, Yes = 0],
        /// 19.7:6 LEDC Programming bits (RW=0b00)
        LEDC   OFFSET(6) NUMBITS(2) [Collision = 0b00, Off = 0b01, Activity = 0b10, AlwaysOn = 0b11],
        /// 19.5 Advance TX Clock, 1=1/2 TX_CLK, 0=Normal (RW=0)
        ATXC   OFFSET(5) NUMBITS(1) [],
        /// 19.4 5B Symbol/(100BASE-X only) 4B Nibble, 1=5B Symbol, 0=4B Nibble (RW=MF2)
        CODER   OFFSET(4) NUMBITS(1) [Symbol5b = 1, Nibble4b = 0],
        /// 19.3 Bypass Scrambler (100BASE-X only), 1=Bypass, 0=Normal (RW=MF3)
        SCRAMBLER   OFFSET(3) NUMBITS(1) [Bypass = 1, Normal = 0],
        /// 19.2 100BASE-FX, 1=Fiber, 0=Twisted Pair (RW=MF4&!MF0))
        CABLE_100FX   OFFSET(2) NUMBITS(1) [Fiber = 1, TwistedPair = 0],
        /// 19.1 Reserved (RO=0)
        RESERVED_2   OFFSET(1) NUMBITS(1) [],
        /// 19.0 Transmit Disconnect, 1=Disconnect, 0=Normal (RW=0)
        TD   OFFSET(0) NUMBITS(1) [],
    ],
    /// Table 56. Chip Status Register (Address 20, Hex 14)
    ChipStatus [
        /// 20.15:14 Reserved (RO=0)
        RESERVED_1   OFFSET(14) NUMBITS(2) [],
        /// 20.13 Link, 1=Up, 0=Down (RO=0)
        LINK   OFFSET(13) NUMBITS(1) [Up = 1, Down = 0],
        /// 20.12 Duplex Mode, 1=Full Duplex, 0=Half Duplex  (RO=?/FDE)
        DUPLEX   OFFSET(12) NUMBITS(1) [FullDuplex = 1, HalfDuplex = 0],
        /// 20.11 Speed, 1=100 Mbps, 0=10Mbps (RO=?/CFG0)
        SPEED   OFFSET(11) NUMBITS(1) [Speed100Mbps = 1, Speed10Mbps = 0],
        /// 20.10 Reserved (RO=0)
        RESERVED_2   OFFSET(10) NUMBITS(1) [],
        /// 20.9 Auto Negotiation Complete, 1=Yes, 0=No (RO=0,LH)
        AN_COMPLETE   OFFSET(9) NUMBITS(1) [Yes = 1, No = 0],
        /// 20.8 Page Received (3 consecutive identical), 1=Yes, 0=No (RO=0,LH)
        PAGE_RECEIVED   OFFSET(8) NUMBITS(1) [Yes = 1, No = 0],
        /// 20.7:3 Reserved (RO=0)
        RESERVED_3   OFFSET(3) NUMBITS(5) [],
        /// 20.2 Low-Voltage, 1=Yes, 0=No (RO=0)
        LOW_VOLTAGE_FAULT   OFFSET(2) NUMBITS(1) [Yes = 1, No = 0],
        /// 20.1:0 Reserved (RO=0)
        RESERVED_4   OFFSET(0) NUMBITS(2) [],
    ],
];
