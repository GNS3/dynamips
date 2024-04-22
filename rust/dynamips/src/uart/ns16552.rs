//! Dual UART with 16 byte buffer
//!
//! References
//! * [PC16552C/NS16C552 Dual Universal Asynchronous Receiver/Transmitter with FIFOs](https://github.com/flaviojs/dynamips-datasheets/blob/master/console/NS16552V.pdf)

use crate::c::dev_vtty::*;
use crate::c::prelude::*;
use crate::utils::Fraction;
use circular_buffer::CircularBuffer;
use std::fmt;
use std::time::Duration;
use std::time::Instant;
use tock_registers::interfaces::ReadWriteable;
use tock_registers::interfaces::Readable;
use tock_registers::interfaces::Writeable;
use tock_registers::register_bitfields;
use tock_registers::registers::InMemoryRegister;
use tock_registers::RegisterLongName;

/// Dual UART with 16 byte buffer
/// In 16450 mode the buffer is 1 byte.
#[derive(Debug)]
pub struct Ns16552 {
    pub uart: [Uart; 2],
    /// Virtual machine for interrupts
    pub vm: *mut vm_instance_t,
    /// Interrupt identifier
    pub irq: c_uint,
}
impl Ns16552 {
    pub const IDX_RBR_DLL_R: usize = 0;
    pub const IDX_THR_DLL_W: usize = 0;
    pub const IDX_IER_DLM_RW: usize = 1;
    pub const IDX_IIR_AFR_R: usize = 2;
    pub const IDX_FCR_AFR_W: usize = 2;
    pub const IDX_LCR_RW: usize = 3;
    pub const IDX_MCR_RW: usize = 4;
    pub const IDX_LSR_R: usize = 5;
    pub const IDX_MSR_R: usize = 6;
    pub const IDX_SCR_RW: usize = 7;
    pub const MASK_IER_W: u8 = 0b0000_1111;
    pub const MASK_FCR_W: u8 = 0b1100_1111;
    pub const MASK_MCR_W: u8 = 0b0001_1111;
    pub const MASK_AFR_W: u8 = 0b0000_0111;
    pub const CRYSTAL_HZ: u32 = 3_686_400;
    pub fn new() -> Self {
        Self::default()
    }
    pub fn new_from_c(vm: *mut vm_instance_t, irq: c_uint, vtty0: *mut vtty_t, vtty1: *mut vtty_t) -> Self {
        let mut x = Self { vm, irq, ..Default::default() };
        x.uart[0].set_vtty(vtty0);
        x.uart[1].set_vtty(vtty1);
        x
    }
    /// Trigger all the chip logic.
    pub fn tick(&mut self) {
        let now = Instant::now();
        self.uart[0].tick(now);
        self.uart[1].tick(now);
        self.shared_interrupt_control_logic(now);
    }
    /// Shared Interrupt Control Logic
    pub fn shared_interrupt_control_logic(&mut self, _now: Instant) {
        if !self.vm.is_null() {
            if self.uart[0].has_interrupt() || self.uart[1].has_interrupt() {
                unsafe { vm_set_irq(self.vm, self.irq) };
            } else {
                unsafe { vm_clear_irq(self.vm, self.irq) };
            }
        }
    }
    /// Read from a register.
    pub fn read_access(&mut self, idx: usize) -> Option<u8> {
        let channel = idx / 8;
        let idx = idx % 8;

        if channel >= self.uart.len() {
            return None;
        }

        let now = Instant::now();
        let uart = &mut self.uart[channel];
        uart.tick(now);
        let ret = uart.read_access(idx, now);
        uart.tick(now);
        self.shared_interrupt_control_logic(now);

        ret
    }
    /// Write to a register.
    pub fn write_access(&mut self, idx: usize, value: u8) -> Option<()> {
        let channel = idx / 8;
        let idx = idx % 8;

        if channel >= self.uart.len() {
            return None;
        }

        let mut fail = false;
        let now = Instant::now();
        if self.uart[0].afr.matches_all(AlternateFunction::CW::ConcurrentWrite) {
            // write to all channels at the same time
            for uart in self.uart.iter_mut() {
                uart.tick(now);
                if uart.write_access(idx, value, now).is_none() {
                    fail = true;
                }
                uart.tick(now);
            }
        } else {
            // write to a single channel
            let uart = &mut self.uart[channel];
            uart.tick(now);
            if uart.write_access(idx, value, now).is_none() {
                fail = true;
            }
            uart.tick(now);
            if idx == Ns16552::IDX_FCR_AFR_W {
                // the concurrent write bits are linked
                let cw = uart.afr.read(AlternateFunction::CW);
                //drop(uart);
                for uart in self.uart.iter_mut() {
                    uart.afr.modify(AlternateFunction::CW.val(cw));
                }
            }
        }
        self.shared_interrupt_control_logic(now);

        if fail {
            None
        } else {
            Some(())
        }
    }
}
impl Default for Ns16552 {
    fn default() -> Self {
        Self { uart: [Uart::default(), Uart::default()], vm: null_mut(), irq: 0 }
    }
}

/// UART of the chip.
pub struct Uart {
    /// Console/aux terminal
    pub vtty: *mut vtty_t,
    /// Fake modem pins
    pub modem: Modem,
    /// How long it takes to send/receive a packet with the current baud rate
    pub packet_duration: Duration,
    /// What the last packet transmission started
    pub transmit_instant: Instant,
    /// When the last packet was received
    pub receive_instant: Instant,
    /// When the last packet was read
    pub read_instant: Instant,
    /// Transmitter memory
    pub transmitter: TransmitterMemory,
    /// Receiver memory
    pub receiver: ReceiverMemory,
    // registers
    pub ier: InMemoryRegister<u8, InterruptEnable::Register>,
    pub iir: InMemoryRegister<u8, InterruptIdentification::Register>,
    pub fcr: InMemoryRegister<u8, FifoControl::Register>,
    pub lcr: InMemoryRegister<u8, LineControl::Register>,
    pub mcr: InMemoryRegister<u8, ModemControl::Register>,
    pub lsr: InMemoryRegister<u8, LineStatus::Register>,
    pub msr: InMemoryRegister<u8, ModemStatus::Register>,
    pub scr: InMemoryRegister<u8, Scratch::Register>,
    pub dll: InMemoryRegister<u8, DivisorLatchLs::Register>,
    pub dlm: InMemoryRegister<u8, DivisorLatchMs::Register>,
    pub afr: InMemoryRegister<u8, AlternateFunction::Register>,
}
impl Uart {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn has_interrupt(&self) -> bool {
        self.iir.matches_all(InterruptIdentification::IP::InterruptPending)
    }
    pub fn set_vtty(&mut self, vtty: *mut vtty_t) {
        self.vtty = vtty;
        if vtty.is_null() {
            // disconnected
            self.modem.cts = false;
            self.modem.dsr = false;
            self.modem.dcd = false;
            self.modem.ri = false;
        } else {
            self.modem.cts = true; // assume the vtty can send data
            self.modem.dsr = unsafe { vtty_is_char_avail(NonNull::new_unchecked(self.vtty)) != 0 };
            self.modem.dcd = true; // assume the vtty is connected
            self.modem.ri = false; // vtty does not have incoming calls
        }
    }
    pub fn tick(&mut self, now: Instant) {
        self.modem_control_logic(now);
        self.receiver_timing_and_control(now);
        self.transmitter_timing_and_control(now);
        self.interrupt_control_logic(now);
    }
    pub fn receive_packet_is_available(&mut self) -> bool {
        if self.mcr.matches_all(ModemControl::LOOP::WithLoopback) {
            self.transmitter.transmit_byte.is_some()
        } else {
            !self.vtty.is_null() && unsafe { vtty_is_char_avail(NonNull::new_unchecked(self.vtty)) != 0 }
        }
    }
    pub fn receive_packet(&mut self) -> bool {
        let some_byte = if self.mcr.matches_all(ModemControl::LOOP::WithLoopback) {
            self.transmitter.transmit_byte.take()
        } else {
            if self.vtty.is_null() {
                return false;
            }
            u8::try_from(unsafe { vtty_get_char(NonNull::new_unchecked(self.vtty)) }).ok()
        };
        if let Some(byte) = some_byte {
            if self.fcr.matches_all(FifoControl::FE::FifoDisable) && self.receiver.has_data() {
                // Overrun Error (NS16450 mode)
                self.lsr.modify(LineStatus::OE::OverrunError);
                self.receiver.reset();
            }
            if !self.receiver.push(Packet::Data(byte)) {
                // Overrun Error (FIFO mode)
                self.lsr.modify(LineStatus::OE::OverrunError);
            }
            self.lsr.modify(LineStatus::PE::NoParityError);
            self.lsr.modify(LineStatus::FE::NoFrameError);
            true
        } else {
            false
        }
    }
    pub fn transmit_packet_is_possible(&self) -> bool {
        if self.mcr.matches_all(ModemControl::LOOP::WithLoopback) {
            self.transmitter.transmit_byte.is_none()
        } else {
            !self.vtty.is_null() && self.transmitter.has_data()
        }
    }
    pub fn transmit_packet(&mut self) -> bool {
        if let Some(byte) = self.transmitter.pop() {
            self.transmitter.transmit_byte = Some(byte);
            if self.mcr.matches_all(ModemControl::LOOP::WithoutLoopback) {
                if self.vtty.is_null() {
                    return false;
                }
                unsafe { vtty_put_char(NonNull::new_unchecked(self.vtty), byte as c_char) };
            }
            true
        } else {
            false
        }
    }
    /// Receiver Timing and Control [<RCLK] [<>RSR] [<>LCR] [<>LSR] [>RBR]
    pub fn receiver_timing_and_control(&mut self, now: Instant) {
        let elapsed = now - self.receive_instant;
        if elapsed < self.packet_duration {
            // delay arrival of next packet
        } else if self.receive_packet_is_available() {
            // can receive next packet
            if self.receive_packet() {
                // received next packet
                if elapsed >= 2 * self.packet_duration {
                    // start receiving
                    self.receive_instant = now;
                } else {
                    // continue receiving
                    self.receive_instant += self.packet_duration;
                }
            }
        } else if elapsed >= 2 * self.packet_duration {
            // not receiving
        }
        self.lsr.modify(LineStatus::DR.val(self.receiver.has_data().into()));
    }
    /// Transmitter Timing and Control [<BAUDOUT] [<LCR] [<BG] [<>TSR] [>LSR]
    pub fn transmitter_timing_and_control(&mut self, now: Instant) {
        let elapsed = now - self.transmit_instant;
        if elapsed < self.packet_duration {
            // delay transmission of next packet
        } else if self.transmit_packet_is_possible() {
            // can transmit next packet
            if self.transmit_packet() {
                // transmitting next packet
                if elapsed >= 2 * self.packet_duration {
                    // start transmitting
                    self.transmit_instant = now;
                    self.lsr.modify(LineStatus::TEMT::Transmitting);
                } else {
                    // continue transmitting
                    self.transmit_instant += self.packet_duration
                }
            }
        } else if elapsed >= 2 * self.packet_duration {
            // stop transmitting
            if self.mcr.matches_all(ModemControl::LOOP::WithLoopback) {
                self.transmitter.transmit_byte = None;
            } else if !self.vtty.is_null() {
                unsafe { vtty_flush(NonNull::new_unchecked(self.vtty)) };
            }
            self.lsr.modify(LineStatus::TEMT::DoneTransmitting);
        }
        if self.transmitter.is_empty() {
            self.lsr.modify(LineStatus::THRE::TransmitterHoldingRegisterEmpty);
        }
    }

    /// Modem Control Logic [<CTS] [<DSR] [<DCD] [<RI] [<>MCR] [>MSR] [>RTS] [>DTR] [>OUT1] [>OUT2]
    pub fn modem_control_logic(&mut self, _now: Instant) {
        let (cts, dsr, dcd, ri);
        if self.mcr.matches_all(ModemControl::LOOP::WithLoopback) {
            cts = self.mcr.read(ModemControl::RTS);
            dsr = self.mcr.read(ModemControl::DTR);
            dcd = self.mcr.read(ModemControl::OUT1);
            ri = self.mcr.read(ModemControl::OUT2);
        } else {
            cts = self.modem.cts.into();
            dsr = self.modem.dsr.into();
            dcd = self.modem.dcd.into();
            ri = self.modem.ri.into();
        }
        if self.msr.read(ModemStatus::CTS) != cts {
            self.msr.modify(ModemStatus::CTS.val(cts));
            self.msr.modify(ModemStatus::DCTS::Changed);
        }
        if self.msr.read(ModemStatus::DSR) != dsr {
            self.msr.modify(ModemStatus::DSR.val(dsr));
            self.msr.modify(ModemStatus::DDSR::Changed);
        }
        if self.msr.read(ModemStatus::RI) != ri {
            self.msr.modify(ModemStatus::RI.val(ri));
            if ri != 0 {
                // 0->1
                self.msr.modify(ModemStatus::TERI::Changed);
            }
        }
        if self.msr.read(ModemStatus::DCD) != dcd {
            self.msr.modify(ModemStatus::DCD.val(dcd));
            self.msr.modify(ModemStatus::DDCD::Changed);
        }
    }
    /// Interrupt Control Logic [<>IER] [>IIR] [>INTRPT]
    pub fn interrupt_control_logic(&mut self, now: Instant) {
        self.iir.modify(if self.fcr.is_set(FifoControl::FE) { InterruptIdentification::FE::Both } else { InterruptIdentification::FE::None });
        // highest priority level
        if self.ier.matches_all(InterruptEnable::ELSI::EnableReceiverLineStatusInterrupt) {
            // Priority 1 - Receiver line status: Overrun error, parity error, framing error, or break interrupt
            let line_status = [LineStatus::OE::OverrunError, LineStatus::PE::ParityError, LineStatus::FE::FrameError, LineStatus::BI::BreakInterrupt];
            if self.lsr.matches_any(&line_status) {
                self.iir.modify(InterruptIdentification::IP::InterruptPending);
                self.iir.modify(InterruptIdentification::IIB::ReceiverLineStatus);
                return;
            }
        }
        // second priority level
        if self.ier.matches_all(InterruptEnable::ERDAI::EnableReceivedDataAvailableInterrupt) {
            let data_ready = if self.fcr.matches_all(FifoControl::FE::FifoEnable) {
                let trigger = match self.fcr.read(FifoControl::RT) {
                    0 => 1,
                    1 => 4,
                    2 => 8,
                    3 => 14,
                    _ => unreachable!(),
                };
                self.receiver.fifo.len() >= trigger
            } else {
                self.lsr.matches_all(LineStatus::DR::DataReady)
            };
            if data_ready {
                self.iir.modify(InterruptIdentification::IP::InterruptPending);
                self.iir.modify(InterruptIdentification::IIB::ReceivedDataAvailable);
                return;
            }
            if self.fcr.matches_all(FifoControl::FE::FifoEnable) && self.receiver.has_data() {
                let receive_elapsed = now - self.receive_instant;
                let read_elapsed = now - self.read_instant;
                if receive_elapsed >= 4 * self.packet_duration && read_elapsed >= 4 * self.packet_duration {
                    self.iir.modify(InterruptIdentification::IP::InterruptPending);
                    self.iir.modify(InterruptIdentification::IIB::FifoTimeout);
                    return;
                }
            }
        }
        // third priority level
        if self.ier.matches_all(InterruptEnable::ETHREI::EnableTransmitterHoldingRegisterEmptyInterrupt) && self.lsr.matches_all(LineStatus::THRE::TransmitterHoldingRegisterEmpty) {
            self.iir.modify(InterruptIdentification::IP::InterruptPending);
            self.iir.modify(InterruptIdentification::IIB::TransmitterHoldingRegisterEmpty);
            return;
        }
        // forth priority level
        if self.ier.is_set(InterruptEnable::EMSI) {
            let modem_status = [ModemStatus::DCTS::Changed, ModemStatus::DDSR::Changed, ModemStatus::TERI::Changed, ModemStatus::DDCD::Changed];
            if self.msr.matches_any(&modem_status) {
                self.iir.modify(InterruptIdentification::IP::InterruptPending);
                self.iir.modify(InterruptIdentification::IIB::ModemStatus);
                return;
            }
        }
        // no interrupt
        self.iir.modify(InterruptIdentification::IP::NoInterrupt);
        self.iir.modify(InterruptIdentification::IIB.val(0));
    }

    pub fn read_access(&mut self, idx: usize, now: Instant) -> Option<u8> {
        let ret = match idx {
            Ns16552::IDX_RBR_DLL_R => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    // RBR
                    self.read_instant = now;
                    if let Some(packet) = self.receiver.pop() {
                        self.lsr.modify(LineStatus::PE::NoParityError); // LH or next character
                        self.lsr.modify(LineStatus::FE::NoFrameError); // LH or next character
                        if let Some(packet) = self.receiver.peek() {
                            match packet {
                                Packet::ParityError(_) => self.lsr.modify(LineStatus::PE::ParityError),
                                Packet::FrameError => self.lsr.modify(LineStatus::FE::FrameError),
                                _ => {}
                            }
                        }
                        self.lsr.modify(LineStatus::DR.val(self.receiver.has_data().into()));
                        Some(packet.value())
                    } else {
                        Some(0)
                    }
                } else {
                    Some(self.dll.get())
                }
            }
            Ns16552::IDX_IER_DLM_RW => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    Some(self.ier.get())
                } else {
                    Some(self.dlm.get())
                }
            }
            Ns16552::IDX_IIR_AFR_R => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    Some(self.iir.get())
                } else {
                    Some(self.afr.get())
                }
            }
            Ns16552::IDX_LCR_RW => Some(self.lcr.get()),
            Ns16552::IDX_MCR_RW => Some(self.mcr.get()),
            Ns16552::IDX_LSR_R => {
                let value = self.lsr.get();
                self.lsr.modify(LineStatus::OE::NoOverrunError); // LH
                self.lsr.modify(LineStatus::PE::NoParityError); // LH
                self.lsr.modify(LineStatus::FE::NoFrameError); // LH
                self.lsr.modify(LineStatus::BI::NoBreakInterrupt); // LH
                self.lsr.modify(LineStatus::ERF::NoErrorInReceiverFifo); // LH
                if self.receiver.has_error_in_fifo() {
                    self.lsr.modify(LineStatus::ERF::ErrorInReceiverFifo);
                }
                Some(value)
            }
            Ns16552::IDX_MSR_R => {
                let value = self.msr.get();
                self.msr.modify(ModemStatus::DCTS::Unchanged); // LH
                self.msr.modify(ModemStatus::DDSR::Unchanged); // LH
                self.msr.modify(ModemStatus::TERI::Unchanged); // LH
                self.msr.modify(ModemStatus::DDCD::Unchanged); // LH
                Some(value)
            }
            Ns16552::IDX_SCR_RW => Some(self.scr.get()),
            _ => None,
        };
        ret
    }
    pub fn write_access(&mut self, idx: usize, value: u8, now: Instant) -> Option<()> {
        fn update<R: RegisterLongName>(reg: &mut InMemoryRegister<u8, R>, rw_mask: u8, value: u8) {
            reg.set((reg.get() & !rw_mask) | (value & rw_mask));
        }
        match idx {
            Ns16552::IDX_THR_DLL_W => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    // THR
                    self.lsr.modify(LineStatus::THRE::TransmitterHoldingRegisterNotEmpty);
                    let mut overrun_error = false;
                    if self.fcr.matches_all(FifoControl::FE::FifoEnable) {
                        if !self.transmitter.push(value) {
                            overrun_error = true;
                        }
                    } else if !self.transmitter.has_data() && self.transmitter.push(value) {
                    } else {
                        overrun_error = true;
                    }
                    // FIXME the microcode prints bytes in bulk without checking, for now avoid data loss by sending stuff early
                    if overrun_error {
                        if self.mcr.is_set(ModemControl::LOOP) {
                            self.transmit_instant = now - self.packet_duration;
                            self.receive_instant = now - self.packet_duration;
                            self.transmitter_timing_and_control(now);
                            self.receiver_timing_and_control(now);
                            if self.transmitter.push(value) {
                                overrun_error = false; // overrun avoided
                            }
                        } else {
                            self.transmit_instant = now - self.packet_duration;
                            self.transmitter_timing_and_control(now);
                            if !self.transmitter.has_data() && self.transmitter.push(value) {
                                overrun_error = false; // overrun avoided
                            }
                        }
                    }
                    if overrun_error {
                        self.lsr.modify(LineStatus::OE::OverrunError);
                    }
                } else {
                    self.dll.set(value);
                    self.update_packet_duration();
                }
            }
            Ns16552::IDX_IER_DLM_RW => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    update(&mut self.ier, Ns16552::MASK_IER_W, value);
                } else {
                    self.dlm.set(value);
                    self.update_packet_duration();
                }
            }
            Ns16552::IDX_FCR_AFR_W => {
                if self.lcr.matches_all(LineControl::DLAB::Off) {
                    update(&mut self.fcr, Ns16552::MASK_FCR_W, value);
                } else {
                    update(&mut self.afr, Ns16552::MASK_AFR_W, value);
                }
            }
            Ns16552::IDX_LCR_RW => {
                self.lcr.set(value);
                self.update_packet_duration();
            }
            Ns16552::IDX_MCR_RW => {
                update(&mut self.mcr, Ns16552::MASK_MCR_W, value);
            }
            Ns16552::IDX_SCR_RW => self.scr.set(value),
            _ => return None,
        }
        Some(())
    }
    pub fn update_packet_duration(&mut self) {
        let ticks_per_second = Fraction(Ns16552::CRYSTAL_HZ, 16);
        let data_bits = 5 + u32::from(self.lcr.read(LineControl::WLS));
        let parity_bits = u32::from(self.lcr.read(LineControl::PEN));
        let divisor = u32::from(u16::from_le_bytes([self.dll.get(), self.dlm.get()]));
        let packets_per_second = if self.lcr.matches_all(LineControl::STB::MoreStopBits) {
            if data_bits == 5 {
                // 1.5 stop bits
                ticks_per_second.mul_div(2, 2 * divisor * (1 + data_bits + parity_bits + 1) + 1)
            } else {
                ticks_per_second / (divisor * (1 + data_bits + parity_bits + 2))
            }
        } else {
            ticks_per_second / (divisor * (1 + data_bits + parity_bits + 1))
        };
        self.packet_duration = packets_per_second.invert().into();
    }
}
impl Default for Uart {
    fn default() -> Uart {
        let now = Instant::now();
        let mut uart = Uart {
            vtty: null_mut(),
            modem: Modem::default(),
            packet_duration: Duration::ZERO,
            transmit_instant: now,
            receive_instant: now,
            read_instant: now,
            transmitter: TransmitterMemory::default(),
            receiver: ReceiverMemory::default(),
            // Table III. DUART Reset Configuration
            ier: InMemoryRegister::new(0b0000_0000),
            iir: InMemoryRegister::new(0b0000_0001),
            fcr: InMemoryRegister::new(0b0000_0000),
            lcr: InMemoryRegister::new(0b0000_0000),
            mcr: InMemoryRegister::new(0b0000_0000),
            lsr: InMemoryRegister::new(0b0110_0000),
            msr: InMemoryRegister::new(0b0000_0000),
            afr: InMemoryRegister::new(0b0000_0000),
            scr: InMemoryRegister::new(0b0000_0000),
            dll: InMemoryRegister::new(0b0000_0000),
            dlm: InMemoryRegister::new(0b0000_0000),
        };
        // sane default communication
        uart.dll.set(24); // baud 9600
        uart.lcr.modify(LineControl::WLS.val(8 - 5)); // 8 data bits
        uart.update_packet_duration();
        uart
    }
}
impl fmt::Debug for Uart {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        fmt.debug_struct("Uart")
            .field("vtty", &format_args!("{:p}", self.vtty))
            .field("modem", &self.modem)
            .field("packet_duration", &self.packet_duration)
            .field("transmit_instant", &self.packet_duration)
            .field("receive_instant", &self.packet_duration)
            .field("read_instant", &self.packet_duration)
            .field("transmitter", &self.transmitter)
            .field("receiver", &self.receiver)
            .field("ier", &format_args!("0x{:02x}", self.ier.get()))
            .field("iir", &format_args!("0x{:02x}", self.iir.get()))
            .field("fcr", &format_args!("0x{:02x}", self.fcr.get()))
            .field("lcr", &format_args!("0x{:02x}", self.lcr.get()))
            .field("mcr", &format_args!("0x{:02x}", self.mcr.get()))
            .field("lsr", &format_args!("0x{:02x}", self.lsr.get()))
            .field("msr", &format_args!("0x{:02x}", self.msr.get()))
            .field("scr", &format_args!("0x{:02x}", self.scr.get()))
            .field("dll", &format_args!("0x{:02x}", self.dll.get()))
            .field("dlm", &format_args!("0x{:02x}", self.dlm.get()))
            .field("afr", &format_args!("0x{:02x}", self.afr.get()))
            .finish()
    }
}

/// Modem signals
#[derive(Default, Debug, Copy, Clone)]
pub struct Modem {
    /// Clear to Send
    cts: bool,
    /// Data Set Ready
    dsr: bool,
    /// Ring Indicator
    ri: bool,
    /// Data Carrier Detect
    dcd: bool,
}

/// Transmitter memory
#[derive(Debug, Clone)]
pub struct TransmitterMemory {
    pub fifo: CircularBuffer<16, u8>,
    pub transmit_byte: Option<u8>,
}
impl TransmitterMemory {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn is_empty(&self) -> bool {
        self.fifo.is_empty()
    }
    pub fn has_data(&self) -> bool {
        !self.fifo.is_empty()
    }
    pub fn has_space(&self) -> bool {
        self.fifo.len() < self.fifo.capacity()
    }
    pub fn push(&mut self, value: u8) -> bool {
        if self.has_space() {
            self.fifo.push_back(value);
            true
        } else {
            false
        }
    }
    pub fn pop(&mut self) -> Option<u8> {
        self.fifo.pop_front()
    }
}
impl Default for TransmitterMemory {
    fn default() -> Self {
        Self { fifo: CircularBuffer::new(), transmit_byte: None }
    }
}

/// Receiver memory
#[derive(Debug, Clone)]
pub struct ReceiverMemory {
    pub fifo: CircularBuffer<16, Packet>,
}
impl ReceiverMemory {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn is_empty(&self) -> bool {
        self.fifo.is_empty()
    }
    pub fn has_data(&self) -> bool {
        !self.fifo.is_empty()
    }
    pub fn has_space(&mut self) -> bool {
        self.fifo.len() < self.fifo.capacity()
    }
    pub fn reset(&mut self) {
        self.fifo.clear();
    }
    pub fn push(&mut self, packet: Packet) -> bool {
        if self.has_space() {
            self.fifo.push_back(packet);
            true
        } else {
            false
        }
    }
    pub fn pop(&mut self) -> Option<Packet> {
        self.fifo.pop_front()
    }
    pub fn peek(&mut self) -> Option<&Packet> {
        self.fifo.front()
    }
    pub fn has_error_in_fifo(&self) -> bool {
        self.fifo.iter().any(|x| x.is_error())
    }
}
impl Default for ReceiverMemory {
    fn default() -> Self {
        Self { fifo: CircularBuffer::new() }
    }
}

/// Packet of data
#[derive(Debug, Copy, Clone)]
pub enum Packet {
    /// Packet with data
    Data(u8),
    /// Packet with data and a parity error
    ParityError(u8),
    /// The packet did not have a valid stop bit
    FrameError,
    ///
    BreakInterrupt,
}
impl Packet {
    pub fn value(&self) -> u8 {
        match self {
            Self::Data(x) => *x,
            Self::ParityError(x) => *x,
            _ => 0,
        }
    }
    pub fn is_error(&self) -> bool {
        !matches!(self, Self::Data(_))
    }
}

/*
Abreviations:
 RW - Read/Write
 RO - Read Only
 WO - Write Only
 LH - Latching High (remains High until read, and then returns to Low)
*/

// Table II. Register Summary for an Individual Channel
register_bitfields![u8,
    /// Receiver Buffer Register (idx=0, DLAB=0, RO)
    ReceiverBuffer [
        /// Bits
        B OFFSET(0) NUMBITS(8) [],
    ],
    /// Transmitter Holding Register (idx=0, DLAB=0, WO)
    TransmitterHolding [
        /// Bits
        B OFFSET(0) NUMBITS(8) [],
    ],
    /// 8.7 Interrupt Enable Register (idx=1, DLAB=0, RW)
    InterruptEnable [
        /// Enable Received Data Available Interrupt
        ERDAI OFFSET(0) NUMBITS(1) [EnableReceivedDataAvailableInterrupt = 1, DisableReceivedDataAvailableInterrupt = 0],
        /// Enable Transmitter Holding Register Empty Interrupt
        ETHREI OFFSET(1) NUMBITS(1) [EnableTransmitterHoldingRegisterEmptyInterrupt = 1, DisableTransmitterHoldingRegisterEmptyInterrupt = 0],
        /// Enable Receiver Line Status Interrupt
        ELSI OFFSET(2) NUMBITS(1) [EnableReceiverLineStatusInterrupt = 1, DisableReceiverLineStatusInterrupt = 0],
        /// Enable Modem Status Interrupt
        EMSI OFFSET(3) NUMBITS(1) [EnableModemStatusInterrupt = 1, DisableModemStatusInterrupt = 0],
    ],
    /// 8.6 Interrupt Identification Register (idx=2, DLAB=0, RO)
    InterruptIdentification [
        /// "0" if Interrupt is Pending
        IP OFFSET(0) NUMBITS(1) [NoInterrupt = 1, InterruptPending = 0],
        /// Interrupt ID Bits
        IIB OFFSET(1) NUMBITS(3) [
            // Highest priority level
            ReceiverLineStatus = 0b011,
            // Second priority level
            ReceivedDataAvailable = 0b010,
            FifoTimeout = 0b110,
            // Third priority level
            TransmitterHoldingRegisterEmpty = 0b001,
            // Forth priority level
            ModemStatus = 0b000,
        ],
        /// Fifo's Enabled
        FE OFFSET(6) NUMBITS(2) [Both = 0b11, None = 0b00],
    ],
    /// 8.5 Fifo Control Register (idx=2, DLAB=0, WO)
    FifoControl [
        /// Fifo Enable
        FE OFFSET(0) NUMBITS(1) [FifoEnable = 1, FifoDisable = 0],
        /// Receiver Fifo Reset
        RFR OFFSET(1) NUMBITS(1) [Yes = 1, No = 0],
        /// Transmitter Fifo Reset
        XFR OFFSET(2) NUMBITS(1) [Yes = 1, No = 0],
        /// Dma Mode Select
        DMS OFFSET(3) NUMBITS(1) [],
        /// Receiver Trigger (1/4/8/14)
        RT OFFSET(6) NUMBITS(2) [],
    ],
    /// 8.1 Line Control Register (idx=3, RW)
    LineControl [
        /// Word Length Select, 5 to 8 bits.
        WLS OFFSET(0) NUMBITS(2) [],
        /// Number of Stop Bits, 1 or 1.5 for 5-bit words and 2 for 6/7/8-bit words
        STB OFFSET(2) NUMBITS(1) [
            /// 1 stop bit
            OneStopBit = 0,
            /// 1.5 stop bits for 5-bit words and 2 stop bits for 6/7/8-bit words
            MoreStopBits = 1,
        ],
        /// Parity Enable
        PEN OFFSET(3) NUMBITS(1) [ParityEnable = 1, ParityDisable = 0],
        /// Even Parity Select
        EPS OFFSET(4) NUMBITS(1) [Even = 1, Odd = 0],
        /// Stick Parity
        SP OFFSET(5) NUMBITS(1) [On = 1, Off = 0],
        /// Set Break
        SB OFFSET(6) NUMBITS(1) [On = 1, Off = 0],
        /// Divisor Latch Access Bit
        DLAB OFFSET(7) NUMBITS(1) [On = 1, Off = 0],
    ],
    /// 8.8 Modem Control Register (idx=4, RW)
    ModemControl [
        /// Data Terminal Ready
        DTR OFFSET(0) NUMBITS(1) [On = 1, Off = 0],
        /// Request to Send
        RTS OFFSET(1) NUMBITS(1) [On = 1, Off = 0],
        /// Out 1
        OUT1 OFFSET(2) NUMBITS(1) [On = 1, Off = 0],
        /// Out 2
        OUT2 OFFSET(3) NUMBITS(1) [On = 1, Off = 0],
        // Loopback
        LOOP OFFSET(4) NUMBITS(1) [WithLoopback = 1, WithoutLoopback = 0],
    ],
    /// 8.4 Line Status Register (idx=5, RO?)
    LineStatus [
        /// Data Ready (for reading)
        DR OFFSET(0) NUMBITS(1) [DataReady = 1, NoDataReady = 0],
        /// Overrun Error (too much data received) (LH)
        OE OFFSET(1) NUMBITS(1) [OverrunError = 1, NoOverrunError = 0],
        /// Parity Error (in current character (LH or next character)
        PE OFFSET(2) NUMBITS(1) [ParityError = 1, NoParityError = 0],
        /// Current character has a frame error (bad stop bits). (LH or next character)
        FE OFFSET(3) NUMBITS(1) [FrameError = 1, NoFrameError = 0],
        /// Break Interrupt (no data received) (LH or new character)
        BI OFFSET(4) NUMBITS(1) [BreakInterrupt = 1, NoBreakInterrupt = 0],
        /// Transmitter Holding Register (triggers cpu interrupt)
        THRE OFFSET(5) NUMBITS(1) [TransmitterHoldingRegisterEmpty = 1, TransmitterHoldingRegisterNotEmpty = 0],
        /// Transmitter Empty
        TEMT OFFSET(6) NUMBITS(1) [DoneTransmitting = 1, Transmitting = 0],
        /// Error in Receiver Fifo. (LH with 0 errors)
        ERF OFFSET(7) NUMBITS(1) [ErrorInReceiverFifo = 1, NoErrorInReceiverFifo = 0],
    ],
    /// 8.9 Modem Status Register (idx=6, RO?)
    ModemStatus [
        /// Delta Clear to Send (LH)
        DCTS   OFFSET(0) NUMBITS(1) [Changed = 1, Unchanged = 0],
        /// Delta Data Set Ready (LH)
        DDSR   OFFSET(1) NUMBITS(1) [Changed = 1, Unchanged = 0],
        /// Trailing Edge of Ring Indicator (LH)
        TERI   OFFSET(2) NUMBITS(1) [Changed = 1, Unchanged = 0],
        /// Delta Data Carrier Detect (LH)
        DDCD   OFFSET(3) NUMBITS(1) [Changed = 1, Unchanged = 0],
        /// Clear to Send
        CTS   OFFSET(4) NUMBITS(1) [On = 1, Off = 0],
        /// Data Set Ready
        DSR   OFFSET(5) NUMBITS(1) [On = 1, Off = 0],
        /// Ring Indicator
        RI   OFFSET(6) NUMBITS(1) [On = 1, Off = 0],
        /// Data Carrier Detect
        DCD   OFFSET(7) NUMBITS(1) [On = 1, Off = 0],
    ],
    /// 8.11 Scratchpad Register (idx=7, RW)
    Scratch [
        /// Bits
        B OFFSET(0) NUMBITS(8) [],
    ],
    /// Divisor Latch (LS) (idx=0, DLAB=1, RW)
    DivisorLatchLs [
        /// Bits
        B OFFSET(0) NUMBITS(8) [],
    ],
    /// Divisor Latch (MS) (idx=1, DLAB=1, RW)
    DivisorLatchMs [
        /// Bits
        B OFFSET(0) NUMBITS(8) [],
    ],
    /// 8.10 Alternate Function Register (idx=2, DLAB=1, RW)
    AlternateFunction [
        /// Write to both uart at the same time.
        CW OFFSET(0) NUMBITS(1) [ConcurrentWrite = 1, NormalWrite = 0],
        /// Which signal goes to the MF pin.
        MF OFFSET(1) NUMBITS(2) [Out2 = 0b00, BaudOut = 0b01, RxRdy = 0b10, High = 0b11],
    ],
];

#[cfg(test)]
mod test {
    use super::*;
    use crate::utils::Fraction;

    #[test]
    fn test_crystal() {
        //  1200 divisor was 192
        //  9600 divisor was  24
        // 19200 divisor was  12
        // Suggests a crystal of 3686400 hz
        println!("crystal={:?}", Ns16552::CRYSTAL_HZ);
        let ticks_per_second = Fraction(Ns16552::CRYSTAL_HZ, 16);
        println!("ticks_per_second={:?}", ticks_per_second);
        assert_eq!(Fraction(1200, 1), ticks_per_second / 192);
        assert_eq!(Fraction(9600, 1), ticks_per_second / 24);
        assert_eq!(Fraction(19200, 1), ticks_per_second / 12);
        fn packets_per_second_to_duration(packets_per_second: Fraction) -> Duration {
            packets_per_second.invert().into()
        }
        let packet_bits = 10; // start + 8 + stop
        println!("packet_duration={:?}", packets_per_second_to_duration(Fraction(1200, packet_bits)));
        println!("packet_duration={:?}", packets_per_second_to_duration(Fraction(9600, packet_bits)));
        println!("packet_duration={:?}", packets_per_second_to_duration(Fraction(19200, packet_bits)));
    }

    #[test]
    fn test_uart_packet_duration() {
        let mut uart = Uart::default();
        uart.dlm.set(0);
        uart.dll.set(24); // baud 9600
        uart.lcr.modify(LineControl::WLS.val(8 - 5)); // 8 data bits
        uart.lcr.modify(LineControl::PEN::ParityDisable); // 0 parity bits
        uart.lcr.modify(LineControl::STB::OneStopBit); // 1 stop bit
        uart.update_packet_duration();
        assert_eq!(Duration::from_nanos(1_041_666), uart.packet_duration);

        uart.lcr.modify(LineControl::STB::MoreStopBits); // 2 stop bits
        uart.update_packet_duration();
        assert_eq!(Duration::from_nanos(1_145_833), uart.packet_duration);

        uart.dll.set(0); // no baud
        uart.update_packet_duration();
        assert_eq!(Duration::ZERO, uart.packet_duration);
    }
}
