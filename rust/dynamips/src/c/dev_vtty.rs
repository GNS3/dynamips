//! TODO doc

use crate::c::prelude::*;
use crate::c::utils::fd_pool_t;

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

/// Virtual TTY structure
#[repr(C)]
pub struct virtual_tty {
    pub vm: *mut vm_instance_t,
    pub name: *mut c_char,
    pub type_: c_int,
    pub fd_array: [c_int; VTTY_MAX_FD],
    pub fd_count: c_int,
    pub tcp_port: c_int,
    pub terminal_support: c_int,
    pub input_state: c_int,
    pub input_pending: c_int,
    pub telnet_cmd: c_int,
    pub telnet_opt: c_int,
    pub telnet_qual: c_int,
    pub managed_flush: c_int,
    pub buffer: [u8; VTTY_BUFFER_SIZE],
    pub read_ptr: c_uint,
    pub write_ptr: c_uint,
    pub lock: libc::pthread_mutex_t,
    pub next: *mut virtual_tty,
    pub pprev: *mut *mut virtual_tty,
    pub priv_data: *mut c_void,
    pub user_arg: c_ulong,
    /// FD Pool (for TCP connections)
    pub fd_pool: fd_pool_t,
    /// Read notification
    pub read_notifier: Option<unsafe extern "C" fn(_: *mut virtual_tty)>,
    /// Old text for replay
    pub replay_buffer: [u8; VTTY_BUFFER_SIZE],
    pub replay_ptr: c_uint,
    pub replay_full: u8,
}
pub type vtty_t = virtual_tty;
