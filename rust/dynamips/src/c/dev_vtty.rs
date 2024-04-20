//! TODO doc

use crate::c::prelude::*;

/// 4 Kb should be enough for a keyboard buffer
pub const VTTY_BUFFER_SIZE: usize = 4096;

/// Maximum listening socket number
pub const VTTY_MAX_FD: usize = 10;

/// VTTY connection types (TODO enum)
pub const VTTY_TYPE_NONE: c_int = 0;
pub const VTTY_TYPE_TERM: c_int = 1;
pub const VTTY_TYPE_TCP: c_int = 2;
pub const VTTY_TYPE_SERIAL: c_int = 3;

/// VTTY input states (TODO enum)
pub const VTTY_INPUT_TEXT: c_int = 0;
pub const VTTY_INPUT_VT1: c_int = 1;
pub const VTTY_INPUT_VT2: c_int = 2;
pub const VTTY_INPUT_REMOTE: c_int = 3;
pub const VTTY_INPUT_TELNET: c_int = 4;
pub const VTTY_INPUT_TELNET_IYOU: c_int = 5;
pub const VTTY_INPUT_TELNET_SB1: c_int = 6;
pub const VTTY_INPUT_TELNET_SB2: c_int = 7;
pub const VTTY_INPUT_TELNET_SB_TTYPE: c_int = 8;
pub const VTTY_INPUT_TELNET_NEXT: c_int = 9;

/// Commmand line support utility
#[repr(C)]
#[derive(Debug)]
pub struct vtty_serial_option {
    /// must free this pointer if C deallocates
    pub device: *mut c_char,
    pub baudrate: c_int,
    pub databits: c_int,
    pub parity: c_int,
    pub stopbits: c_int,
    pub hwflow: c_int,
}
pub type vtty_serial_option_t = vtty_serial_option;
impl vtty_serial_option {
    /// Parse serial interface descriptor string, return true if success
    /// string takes the form "device:baudrate:databits:parity:stopbits:hwflow"
    /// device is mandatory, other options are optional (default=9600,8,N,1,0).
    fn parse_serial_option(&mut self, optarg: &str) -> Result<(), &'static str> {
        let mut args = optarg.split(':');
        if let Some(device) = args.next() {
            assert!(self.device.is_null());
            self.device = strdup(device);
            if self.device.is_null() {
                return Err("unable to copy string");
            }
        } else {
            return Err("invalid string");
        }
        self.baudrate = args.next().and_then(|s| s.parse::<c_int>().ok()).unwrap_or(9600);
        self.databits = args.next().and_then(|s| s.parse::<c_int>().ok()).unwrap_or(8);
        self.parity = args.next().and_then(|s| s.chars().nth(0)).map_or(0, |c| match c {
            'o' | 'O' => 1, // odd
            'e' | 'E' => 2, // even
            _ => 0,         // none
        });
        self.stopbits = args.next().and_then(|s| s.parse::<c_int>().ok()).unwrap_or(1);
        self.hwflow = args.next().and_then(|s| s.parse::<c_int>().ok()).unwrap_or(0);
        Ok(())
    }
}
impl Drop for vtty_serial_option {
    fn drop(&mut self) {
        if !self.device.is_null() {
            unsafe { libc::free(self.device.cast::<_>()) };
            self.device = null_mut();
        }
    }
}

#[no_mangle]
pub extern "C" fn vtty_parse_serial_option(mut option: NonNull<vtty_serial_option_t>, optarg: NonNull<c_char>) -> c_int {
    let option = unsafe { option.as_mut() };
    let optarg = unsafe { CStr::from_ptr(optarg.as_ptr()).to_str().unwrap() };
    if let Err(err) = option.parse_serial_option(optarg) {
        eprintln!("vtty_parse_serial_option: {}", err);
        -1
    } else {
        0
    }
}
