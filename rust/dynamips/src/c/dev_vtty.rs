//! TODO doc

use crate::c::dynamips::*;
use crate::c::prelude::*;
use crate::c::utils::*;
use crate::c::vm::*;
use std::cmp::max;

static mut CTRL_CODE_OK: c_int = 1;
static mut TELNET_MESSAGE_OK: c_int = 1;
/// VTTY list
static mut VTTY_LIST_MUTEX: libc::pthread_mutex_t = libc::PTHREAD_MUTEX_INITIALIZER;
static mut VTTY_LIST: *mut vtty_t = null_mut();
static mut VTTY_THREAD: libc::pthread_t = 0;
static mut TIOS: libc::termios = unsafe { zeroed::<libc::termios>() };
static mut TIOS_ORIG: libc::termios = unsafe { zeroed::<libc::termios>() };

fn vtty_list_lock() {
    unsafe { libc::pthread_mutex_lock(addr_of_mut!(VTTY_LIST_MUTEX)) };
}
fn vtty_list_unlock() {
    unsafe { libc::pthread_mutex_unlock(addr_of_mut!(VTTY_LIST_MUTEX)) };
}

fn vtty_lock(vtty: &mut vtty_t) {
    unsafe { libc::pthread_mutex_lock(addr_of_mut!(vtty.lock)) };
}
fn vtty_unlock(vtty: &mut vtty_t) {
    unsafe { libc::pthread_mutex_unlock(addr_of_mut!(vtty.lock)) };
}

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
impl virtual_tty {
    pub fn new() -> Self {
        Self {
            vm: null_mut(),
            name: null_mut(),
            type_: VTTY_TYPE_NONE,
            fd_array: [-1; VTTY_MAX_FD],
            fd_count: 0,
            tcp_port: 0,
            terminal_support: 0,
            input_state: VTTY_INPUT_TEXT,
            input_pending: 0,
            telnet_cmd: 0,
            telnet_opt: 0,
            telnet_qual: 0,
            managed_flush: 0,
            buffer: [0; VTTY_BUFFER_SIZE],
            read_ptr: 0,
            write_ptr: 0,
            lock: libc::PTHREAD_MUTEX_INITIALIZER,
            next: null_mut(),
            pprev: null_mut(),
            priv_data: null_mut(),
            user_arg: 0,
            fd_pool: fd_pool::new(),
            read_notifier: None,
            replay_buffer: [0; VTTY_BUFFER_SIZE],
            replay_ptr: 0,
            replay_full: 0,
        }
    }
}
impl Default for virtual_tty {
    fn default() -> Self {
        Self::new()
    }
}

/* Restore TTY original settings */
extern "C" fn vtty_term_reset() {
    unsafe { libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, addr_of!(TIOS_ORIG)) };
}

/* Initialize real TTY */
fn vtty_term_init() {
    unsafe {
        libc::tcgetattr(libc::STDIN_FILENO, addr_of_mut!(TIOS));

        libc::memcpy(addr_of_mut!(TIOS_ORIG).cast::<_>(), addr_of_mut!(TIOS).cast::<_>(), size_of::<libc::termios>());
        libc::atexit(vtty_term_reset);

        TIOS.c_cc[libc::VTIME] = 0;
        TIOS.c_cc[libc::VMIN] = 1;

        /* Disable Ctrl-C, Ctrl-S, Ctrl-Q and Ctrl-Z */
        TIOS.c_cc[libc::VINTR] = 0;
        TIOS.c_cc[libc::VSTART] = 0;
        TIOS.c_cc[libc::VSTOP] = 0;
        TIOS.c_cc[libc::VSUSP] = 0;

        TIOS.c_lflag &= !(libc::ICANON | libc::ECHO);
        TIOS.c_iflag &= !libc::ICRNL;
        libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, addr_of!(TIOS));
        libc::tcflush(libc::STDIN_FILENO, libc::TCIFLUSH);
    }
}

fn cfmakeraw(tios: &mut libc::termios) {
    #[cfg(has_libc_cfmakeraw)]
    unsafe {
        libc::cfmakeraw(addr_of_mut!(*tios));
    }
    #[cfg(not(has_libc_cfmakeraw))]
    unsafe {
        //#if defined(__CYGWIN__) || defined(SUNOS)
        tios.c_iflag &= !(libc::IGNBRK | libc::BRKINT | libc::PARMRK | libc::ISTRIP | libc::INLCR | libc::IGNCR | libc::ICRNL | libc::IXON);
        tios.c_oflag &= !libc::OPOST;
        tios.c_lflag &= !(libc::ECHO | libc::ECHONL | libc::ICANON | libc::ISIG | libc::IEXTEN);
        tios.c_cflag &= !(libc::CSIZE | libc::PARENB);
        tios.c_cflag |= libc::CS8;
    }
}

/// Setup serial port, return 0 if success.
fn vtty_serial_setup(vtty: &mut vtty_t, option: &vtty_serial_option_t) -> c_int {
    unsafe {
        let mut tio = zeroed::<libc::termios>();

        if libc::tcgetattr(vtty.fd_array[0], &mut tio) != 0 {
            eprintln!("vtty_serial_setup: tcgetattr failed");
            return -1;
        }

        cfmakeraw(&mut tio);

        tio.c_cflag = libc::CLOCAL; // ignore modem control lines

        tio.c_cflag &= !libc::CREAD;
        tio.c_cflag |= libc::CREAD;

        let tio_baudrate = match option.baudrate {
            50 => libc::B50,
            75 => libc::B75,
            110 => libc::B110,
            134 => libc::B134,
            150 => libc::B150,
            200 => libc::B200,
            300 => libc::B300,
            600 => libc::B600,
            1200 => libc::B1200,
            1800 => libc::B1800,
            2400 => libc::B2400,
            4800 => libc::B4800,
            9600 => libc::B9600,
            19200 => libc::B19200,
            38400 => libc::B38400,
            57600 => libc::B57600,
            #[cfg(has_libc_B76800)]
            76800 => libc::B76800,
            115200 => libc::B115200,
            #[cfg(has_libc_B230400)]
            230400 => libc::B230400,
            baudrate => {
                eprintln!("vtty_serial_setup: unsupported baudrate {}", baudrate);
                return -1;
            }
        };

        libc::cfsetospeed(&mut tio, tio_baudrate);
        libc::cfsetispeed(&mut tio, tio_baudrate);

        tio.c_cflag &= !libc::CSIZE; // clear size flag
        match option.databits {
            5 => tio.c_cflag |= libc::CS5,
            6 => tio.c_cflag |= libc::CS6,
            7 => tio.c_cflag |= libc::CS7,
            8 => tio.c_cflag |= libc::CS8,
            databits => {
                eprintln!("vtty_serial_setup: unsupported databits {}", databits);
                return -1;
            }
        }

        tio.c_iflag &= !libc::INPCK; // clear parity flag
        tio.c_cflag &= !(libc::PARENB | libc::PARODD);
        match option.parity {
            0 => {}
            2 => {
                // even
                tio.c_iflag |= libc::INPCK;
                tio.c_cflag |= libc::PARENB;
            }
            1 => {
                // odd
                tio.c_iflag |= libc::INPCK;
                tio.c_cflag |= libc::PARENB | libc::PARODD;
            }
            parity => {
                eprintln!("vtty_serial_setup: unsupported parity {}", parity);
                return -1;
            }
        }

        tio.c_cflag &= !libc::CSTOPB; // clear stop flag
        match option.stopbits {
            1 => {}
            2 => tio.c_cflag |= libc::CSTOPB,
            stopbits => {
                eprintln!("vtty_serial_setup: unsupported stopbits {}", stopbits);
                return -1;
            }
        }

        #[cfg(has_libc_CRTSCTS)]
        {
            tio.c_cflag &= !libc::CRTSCTS;
            if option.hwflow != 0 {
                tio.c_cflag |= libc::CRTSCTS;
            }
        }
        #[cfg(has_libc_CNEW_RTSCTS)]
        {
            tio.c_cflag &= !libc::CNEW_RTSCTS;
            if option.hwflow != 0 {
                tio.c_cflag |= libc::CNEW_RTSCTS;
            }
        }

        tio.c_cc[libc::VTIME] = 0;
        tio.c_cc[libc::VMIN] = 1; // block read() until one character is available

        if false {
            // not neccessary unless O_NONBLOCK used
            if libc::fcntl(vtty.fd_array[0], libc::F_SETFL, 0) != 0 {
                // enable blocking mode
                eprintln!("vtty_serial_setup: fnctl(F_SETFL) failed");
                return -1;
            }
        }

        if libc::tcflush(vtty.fd_array[0], libc::TCIOFLUSH) != 0 {
            eprintln!("vtty_serial_setup: tcflush(TCIOFLUSH) failed");
            return -1;
        }

        if libc::tcsetattr(vtty.fd_array[0], libc::TCSANOW, &tio) != 0 {
            eprintln!("vtty_serial_setup: tcsetattr(TCSANOW) failed");
            return -1;
        }

        0
    }
}

/// Wait for a TCP connection
fn vtty_tcp_conn_wait(vtty: &mut vtty_t) -> c_int {
    #[cfg(feature = "ENABLE_IPV6")]
    unsafe {
        let one: c_int = 1;

        vtty.fd_array = [-1; VTTY_MAX_FD];

        let mut hints = zeroed::<libc::addrinfo>();
        hints.ai_family = libc::PF_UNSPEC;
        hints.ai_socktype = libc::SOCK_STREAM;
        hints.ai_flags = libc::AI_PASSIVE;

        let port_str = format!("{}\0", vtty.tcp_port);

        /* Try to use the console binding address first, then fallback to the global binding address */
        let addr: *const c_char = if !console_binding_addr.is_null() && *console_binding_addr != 0 {
            console_binding_addr
        } else if !binding_addr.is_null() && *binding_addr != 0 {
            binding_addr
        } else {
            "127.0.0.1\0".as_ptr().cast::<_>()
        };

        let mut res0: *mut libc::addrinfo = null_mut();
        if libc::getaddrinfo(addr, port_str.as_str().as_ptr().cast::<_>(), &hints, addr_of_mut!(res0)) != 0 {
            perror("vtty_tcp_conn_wait: getaddrinfo");
            return -1;
        }

        let mut nsock: usize = 0;
        let mut next_res = res0;
        while !next_res.is_null() {
            let res = &mut *next_res;
            next_res = res.ai_next;

            if res.ai_family != libc::PF_INET && res.ai_family != libc::PF_INET6 {
                continue;
            }

            vtty.fd_array[nsock] = libc::socket(res.ai_family, res.ai_socktype, res.ai_protocol);

            if vtty.fd_array[nsock] < 0 {
                continue;
            }

            if libc::setsockopt(vtty.fd_array[nsock], libc::SOL_SOCKET, libc::SO_REUSEADDR, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("vtty_tcp_conn_wait: setsockopt(SO_REUSEADDR)");
            }

            if libc::setsockopt(vtty.fd_array[nsock], libc::SOL_SOCKET, libc::SO_KEEPALIVE, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("vtty_tcp_conn_wait: setsockopt(SO_KEEPALIVE)");
            }

            // Send telnet packets asap. Dont wait to fill packets up
            if libc::setsockopt(vtty.fd_array[nsock], libc::SOL_TCP, libc::TCP_NODELAY, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("vtty_tcp_conn_wait: setsockopt(TCP_NODELAY)");
            }

            if libc::bind(vtty.fd_array[nsock], res.ai_addr, res.ai_addrlen) < 0 || libc::listen(vtty.fd_array[nsock], 1) < 0 {
                libc::close(vtty.fd_array[nsock]);
                vtty.fd_array[nsock] = -1;
                continue;
            }

            let proto = if res.ai_family == libc::PF_INET6 { "IPv6" } else { "IPv4" };
            vm_log(vtty.vm, "VTTY", &format!("{}: waiting connection on tcp port {} for protocol {} (FD {})\n", vtty.name.as_str(), vtty.tcp_port, proto, vtty.fd_array[nsock]));

            nsock += 1;
        }

        libc::freeaddrinfo(res0);
        nsock as c_int
    }
    #[cfg(not(feature = "ENABLE_IPV6"))]
    unsafe {
        let one: c_int = 1;

        vtty.fd_array = [-1; VTTY_MAX_FD];

        vtty.fd_array[0] = libc::socket(libc::PF_INET, libc::SOCK_STREAM, 0);
        if vtty.fd_array[0] < 0 {
            perror("vtty_tcp_conn_wait: socket");
            return -1;
        }

        if libc::setsockopt(vtty.fd_array[0], libc::SOL_SOCKET, libc::SO_REUSEADDR, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
            perror("vtty_tcp_conn_wait: setsockopt(SO_REUSEADDR)");
            libc::close(vtty.fd_array[0]);
            vtty.fd_array[0] = -1;
            return -1;
        }

        if libc::setsockopt(vtty.fd_array[0], libc::SOL_SOCKET, libc::SO_KEEPALIVE, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
            perror("vtty_tcp_conn_wait: setsockopt(SO_KEEPALIVE)");
            libc::close(vtty.fd_array[0]);
            vtty.fd_array[0] = -1;
            return -1;
        }

        // Send telnet packets asap. Dont wait to fill packets up
        if libc::setsockopt(vtty.fd_array[0], libc::SOL_TCP, libc::TCP_NODELAY, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
            perror("vtty_tcp_conn_wait: setsockopt(TCP_NODELAY)");
            libc::close(vtty.fd_array[0]);
            vtty.fd_array[0] = -1;
            return -1;
        }

        let mut serv = zeroed::<libc::sockaddr_in>();
        serv.sin_family = libc::AF_INET as u16;
        serv.sin_addr.s_addr = libc::INADDR_ANY.to_be();
        serv.sin_port = (vtty.tcp_port as u16).to_be();

        if libc::bind(vtty.fd_array[0], addr_of!(serv).cast::<_>(), size_of::<libc::sockaddr_in>() as u32) < 0 {
            perror("vtty_tcp_conn_wait: bind");
            libc::close(vtty.fd_array[0]);
            vtty.fd_array[0] = -1;
            return -1;
        }

        if libc::listen(vtty.fd_array[0], 1) < 0 {
            perror("vtty_tcp_conn_wait: listen");
            libc::close(vtty.fd_array[0]);
            vtty.fd_array[0] = -1;
            return -1;
        }

        vm_log(vtty.vm, "VTTY", &format!("{}: waiting connection on tcp port {} (FD {})\n", vtty.name.as_str(), vtty.tcp_port, vtty.fd_array[0]));

        1
    }
}

/// Create a virtual tty
#[no_mangle]
pub extern "C" fn vtty_create(vm: *mut vm_instance_t, name: NonNull<c_char>, type_: c_int, tcp_port: c_int, option: *const vtty_serial_option_t) -> *mut vtty_t {
    unsafe {
        let option = NonNull::new(option.cast_mut()).unwrap();
        let mut vtty = Box::new(virtual_tty::new());
        vtty.name = name.as_ptr();
        vtty.type_ = type_;
        vtty.vm = vm;
        vtty.fd_count = 0;
        libc::pthread_mutex_init(&mut vtty.lock, null_mut());
        vtty.terminal_support = 1;
        vtty.input_state = VTTY_INPUT_TEXT;
        fd_pool_init(&mut vtty.fd_pool);
        vtty.fd_array = [-1; VTTY_MAX_FD];

        match vtty.type_ {
            VTTY_TYPE_NONE => {}

            VTTY_TYPE_TERM => {
                vtty_term_init();
                vtty.fd_array[0] = libc::STDIN_FILENO;
            }

            VTTY_TYPE_TCP => {
                vtty.tcp_port = tcp_port;
                vtty.fd_count = vtty_tcp_conn_wait(&mut vtty);
            }

            VTTY_TYPE_SERIAL => {
                vtty.fd_array[0] = libc::open(option.as_ref().device, libc::O_RDWR);
                if vtty.fd_array[0] < 0 {
                    eprintln!("VTTY: open failed");
                    return null_mut();
                }
                if vtty_serial_setup(&mut vtty, option.as_ref()) != 0 {
                    eprintln!("VTTY: setup failed");
                    libc::close(vtty.fd_array[0]);
                    return null_mut();
                }
                vtty.terminal_support = 0;
            }

            _ => {
                eprintln!("tty_create: bad vtty type {}", vtty.type_);
                return null_mut();
            }
        }

        /* Add this new VTTY to the list */
        vtty_list_lock();
        vtty.next = VTTY_LIST;
        vtty.pprev = addr_of_mut!(VTTY_LIST);

        if !VTTY_LIST.is_null() {
            (*VTTY_LIST).pprev = addr_of_mut!(vtty.next);
        }

        VTTY_LIST = addr_of_mut!(*vtty);
        vtty_list_unlock();
        Box::into_raw(vtty) // memory managed by C
    }
}

/// Delete a virtual tty
#[no_mangle]
pub extern "C" fn vtty_delete(vtty: *mut vtty_t) {
    unsafe {
        if !vtty.is_null() {
            let mut vtty = Box::from_raw(vtty); // memory managed by rust
            vtty_list_lock();
            if !vtty.pprev.is_null() {
                if !vtty.next.is_null() {
                    (*vtty.next).pprev = vtty.pprev;
                }
                *(vtty.pprev) = vtty.next;
            }
            vtty_list_unlock();

            match vtty.type_ {
                VTTY_TYPE_TCP => {
                    for i in 0..vtty.fd_count as usize {
                        if vtty.fd_array[i] != -1 {
                            vm_log(vtty.vm, "VTTY", &format!("{}: closing FD {}\n", vtty.name.as_str(), vtty.fd_array[i]));
                            libc::close(vtty.fd_array[i]);
                        }
                    }

                    fd_pool_free(&mut vtty.fd_pool);
                    vtty.fd_count = 0;
                }

                _ => {
                    // We don't close FD 0 since it is stdin
                    if vtty.fd_array[0] > 0 {
                        vm_log(vtty.vm, "VTTY", &format!("{}: closing FD {}\n", vtty.name.as_str(), vtty.fd_array[0]));
                        libc::close(vtty.fd_array[0]);
                    }
                }
            }
        }
    }
}

/// Store a character in the FIFO buffer (TODO private)
#[no_mangle]
pub extern "C" fn vtty_store(mut vtty: NonNull<vtty_t>, c: u8) -> c_int {
    let vtty = unsafe { vtty.as_mut() };
    vtty_lock(vtty);
    let mut nwptr = vtty.write_ptr + 1;
    if nwptr == VTTY_BUFFER_SIZE as u32 {
        nwptr = 0;
    }

    if nwptr == vtty.read_ptr {
        vtty_unlock(vtty);
        return -1;
    }

    vtty.buffer[vtty.write_ptr as usize] = c;
    vtty.write_ptr = nwptr;
    vtty_unlock(vtty);
    0
}

/// Store arbritary data in the FIFO buffer
#[no_mangle]
pub extern "C" fn vtty_store_data(vtty: *mut vtty_t, data: *mut c_char, len: c_int) -> c_int {
    unsafe {
        if vtty.is_null() || data.is_null() || len < 0 {
            return -1; // invalid argument
        }

        let vtty = &mut *vtty;
        let mut bytes = 0;
        while bytes < len {
            if vtty_store(vtty.into(), *data.wrapping_add(bytes as usize) as u8) == -1 {
                break;
            }
            bytes += 1;
        }

        vtty.input_pending = 1;
        bytes
    }
}

/// Read a character from the buffer (-1 if the buffer is empty)
#[no_mangle]
pub extern "C" fn vtty_get_char(mut vtty: NonNull<vtty_t>) -> c_int {
    unsafe {
        let vtty = vtty.as_mut();

        vtty_lock(vtty);

        if vtty.read_ptr == vtty.write_ptr {
            vtty_unlock(vtty);
            return -1;
        }

        let c = vtty.buffer[vtty.read_ptr as usize];
        vtty.read_ptr += 1;

        if vtty.read_ptr == VTTY_BUFFER_SIZE as u32 {
            vtty.read_ptr = 0;
        }

        vtty_unlock(vtty);
        c.into()
    }
}

/// Put char to vtty
#[no_mangle]
pub extern "C" fn vtty_put_char(mut vtty: NonNull<vtty_t>, mut ch: c_char) {
    unsafe {
        let vtty = vtty.as_mut();

        match vtty.type_ {
            VTTY_TYPE_NONE => {}

            VTTY_TYPE_TERM | VTTY_TYPE_SERIAL => {
                if libc::write(vtty.fd_array[0], addr_of!(ch).cast::<_>(), 1) != 1 {
                    vm_log(vtty.vm, "VTTY", &format!("{}: put char 0x{:02x} failed ({})\n", vtty.name.as_str(), ch, strerror(errno())));
                }
            }

            VTTY_TYPE_TCP => {
                fd_pool_send(addr_of_mut!(vtty.fd_pool), addr_of_mut!(ch).cast::<_>(), 1, 0);
            }

            _ => {
                vm_error(vtty.vm, &format!("vtty_put_char: bad vtty type {}\n", vtty.type_));
                libc::exit(1);
            }
        }

        // store char for replay
        vtty.replay_buffer[vtty.replay_ptr as usize] = ch as u8;

        vtty.replay_ptr += 1;
        if vtty.replay_ptr == VTTY_BUFFER_SIZE as u32 {
            vtty.replay_ptr = 0;
            vtty.replay_full = 1;
        }
    }
}

/// Put a buffer to vtty
#[no_mangle]
pub extern "C" fn vtty_put_buffer(vtty: NonNull<vtty_t>, buf: *mut c_char, len: size_t) {
    unsafe {
        for i in 0..len {
            vtty_put_char(vtty, *buf.add(i));
        }

        vtty_flush(vtty);
    }
}

/// Flush VTTY output
#[no_mangle]
pub extern "C" fn vtty_flush(vtty: NonNull<vtty_t>) {
    unsafe {
        let vtty = vtty.as_ref();
        match vtty.type_ {
            VTTY_TYPE_TERM | VTTY_TYPE_SERIAL => {
                if vtty.fd_array[0] != -1 {
                    libc::fsync(vtty.fd_array[0]);
                }
            }
            _ => {}
        }
    }
}

/// Returns TRUE if a character is available in buffer
#[no_mangle]
pub extern "C" fn vtty_is_char_avail(mut vtty: NonNull<vtty_t>) -> c_int {
    unsafe {
        let vtty = vtty.as_mut();
        vtty_lock(vtty);
        let res = (vtty.read_ptr != vtty.write_ptr).into();
        vtty_unlock(vtty);
        res
    }
}

/// Store CTRL+C in buffer
#[no_mangle]
pub extern "C" fn vtty_store_ctrlc(vtty: *mut vtty_t) -> c_int {
    unsafe {
        if !vtty.is_null() {
            let vtty = &mut *vtty;
            vtty_store(vtty.into(), 0x03);
        }
        0
    }
}

// Allow the user to disable the CTRL code for the monitor interface
#[no_mangle]
pub extern "C" fn vtty_set_ctrlhandler(n: c_int) {
    unsafe {
        CTRL_CODE_OK = n;
    }
}

/// Allow the user to disable the telnet message for AUX and CONSOLE
#[no_mangle]
pub extern "C" fn vtty_set_telnetmsg(n: c_int) {
    unsafe {
        TELNET_MESSAGE_OK = n;
    }
}

// from arpa/telnet.h
/// interpret as command
const IAC: u8 = 255;
/// you are not to use option
const DONT: u8 = 254;
/// please, you use option
const DO: u8 = 253;
/// I won't use option
const WONT: u8 = 252;
/// I will use option
const WILL: u8 = 251;
/// interpret as subnegotiation
const SB: u8 = 250;
/// end sub negotiation
const SE: u8 = 240;
/// echo
const TELOPT_ECHO: u8 = 1;
/// suppress go ahead
const TELOPT_SGA: u8 = 3;
/// terminal type
const TELOPT_TTYPE: u8 = 24;
/// Linemode option
const TELOPT_LINEMODE: u8 = 34;
/// option is...
const TELQUAL_IS: u8 = 0;
/// send option
const TELQUAL_SEND: u8 = 1;

/// Send Telnet command: WILL TELOPT_ECHO
fn vtty_telnet_will_echo(fd: c_int) {
    unsafe {
        let cmd = [IAC, WILL, TELOPT_ECHO];
        libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
    }
}

/// Send Telnet command: Suppress Go-Ahead
fn vtty_telnet_will_suppress_go_ahead(fd: c_int) {
    unsafe {
        let cmd = [IAC, WILL, TELOPT_SGA];
        libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
    }
}

/// Send Telnet command: Don't use linemode
fn vtty_telnet_dont_linemode(fd: c_int) {
    unsafe {
        let cmd = [IAC, DONT, TELOPT_LINEMODE];
        libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
    }
}

/// Send Telnet command: does the client support terminal type message?
fn vtty_telnet_do_ttype(fd: c_int) {
    unsafe {
        let cmd = [IAC, DO, TELOPT_TTYPE];
        libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
    }
}

/// Accept a TCP connection
fn vtty_tcp_conn_accept(mut vtty: NonNull<vtty_t>, nsock: c_int) -> c_int {
    unsafe {
        let vtty = vtty.as_mut();
        let nsock = nsock as usize;
        let mut fd_slot: *mut c_int = null_mut();

        if fd_pool_get_free_slot(addr_of_mut!(vtty.fd_pool), addr_of_mut!(fd_slot)) < 0 {
            vm_error(vtty.vm, "unable to create a new VTTY TCP connection\n");
            return -1;
        }

        let fd = libc::accept(vtty.fd_array[nsock], null_mut(), null_mut());
        if fd < 0 {
            vm_error(vtty.vm, &format!("vtty_tcp_conn_accept: accept on port {} failed {}\n", vtty.tcp_port, strerror(errno())));
            return -1;
        }

        // Register the new FD
        *fd_slot = fd;

        vm_log(vtty.vm, "VTTY", &format!("{} is now connected (accept_fd={},conn_fd={})\n", vtty.name.as_str(), vtty.fd_array[nsock], fd));

        // Adapt Telnet settings
        if vtty.terminal_support != 0 {
            vtty_telnet_do_ttype(fd);
            vtty_telnet_will_echo(fd);
            vtty_telnet_will_suppress_go_ahead(fd);
            vtty_telnet_dont_linemode(fd);
            vtty.input_state = VTTY_INPUT_TEXT;
        }

        if TELNET_MESSAGE_OK == 1 {
            fd_puts(fd, 0, &format!("Connected to Dynamips VM \"{}\" (ID {}, type {}) - {}\r\nPress ENTER to get the prompt.\r\n", vm_get_name(vtty.vm).as_str(), vm_get_instance_id(vtty.vm), vm_get_type(vtty.vm).as_str(), vtty.name.as_str()));
            // replay old text
            if vtty.replay_full != 0 {
                let mut i = vtty.replay_ptr as usize;
                while i < VTTY_BUFFER_SIZE {
                    let n = libc::send(fd, addr_of!(vtty.replay_buffer[i]).cast::<_>(), VTTY_BUFFER_SIZE - i, 0);
                    if n < 0 {
                        perror("vtty_tcp_conn_accept: send");
                        break;
                    }
                    i += n as usize;
                }
            }
            let mut i = 0;
            while i < vtty.read_ptr as usize {
                let n = libc::send(fd, addr_of!(vtty.replay_buffer[i]).cast::<_>(), vtty.replay_ptr as usize - i, 0);
                if n < 0 {
                    perror("vtty_tcp_conn_accept: send");
                    break;
                }
                i += n as usize;
            }
            // warn if not running
            if vm_get_status(vtty.vm) != VM_STATUS_RUNNING {
                fd_puts(fd, 0, &format!("\r\n!!! WARNING - VM is not running, will be unresponsive (status={}) !!!\r\n", vm_get_status(vtty.vm)));
            }
            vtty_flush(vtty.into());
        }
        0
    }
}

/// Read a character from the terminal
fn vtty_term_read(vtty: NonNull<vtty_t>) -> c_int {
    unsafe {
        let mut c: u8 = 0;

        if libc::read(vtty.as_ref().fd_array[0], addr_of_mut!(c).cast::<_>(), 1) == 1 {
            return c.into();
        }

        perror("read from vtty failed");
        -1
    }
}

/// Read a character from the TCP connection.
fn vtty_tcp_read(_vtty: NonNull<vtty_t>, mut fd_slot: NonNull<c_int>) -> c_int {
    unsafe {
        let fd = *fd_slot.as_ref();
        let mut c: u8 = 0;

        if libc::read(fd, addr_of_mut!(c).cast::<_>(), 1) == 1 {
            return c.into();
        }

        // problem with the connection
        libc::shutdown(fd, 2);
        libc::close(fd);
        *fd_slot.as_mut() = -1;

        // Shouldn't happen...
        -1
    }
}

/// Read a character from the virtual TTY.
///
/// If the VTTY is a TCP connection, restart it in case of error.
fn vtty_read(vtty: NonNull<vtty_t>, fd_slot: NonNull<c_int>) -> c_int {
    unsafe {
        match vtty.as_ref().type_ {
            VTTY_TYPE_TERM | VTTY_TYPE_SERIAL => vtty_term_read(vtty),
            VTTY_TYPE_TCP => vtty_tcp_read(vtty, fd_slot),
            _ => {
                eprintln!("vtty_read: bad vtty type {}\n", vtty.as_ref().type_);
                -1
            }
        }
    }
}

/// Read a character (until one is available) and store it in buffer
fn vtty_read_and_store(mut vtty: NonNull<vtty_t>, fd_slot: NonNull<c_int>) {
    unsafe {
        // wait until we get a character input
        let c = vtty_read(vtty, fd_slot);

        // if read error, do nothing
        if c < 0 {
            return;
        }
        let c = c as u8;

        // If something was read, make sure the handler is informed
        vtty.as_mut().input_pending = 1;

        if vtty.as_ref().terminal_support == 0 {
            vtty_store(vtty, c);
            return;
        }

        match vtty.as_ref().input_state {
            VTTY_INPUT_TEXT => {
                match c {
                    0x1b => {
                        vtty.as_mut().input_state = VTTY_INPUT_VT1;
                    }

                    /* Ctrl + ']' (0x1d, 29), or Alt-Gr + '*' (0xb3, 179) */
                    0x1d | 0xb3 => {
                        if CTRL_CODE_OK == 1 {
                            vtty.as_mut().input_state = VTTY_INPUT_REMOTE;
                        } else {
                            vtty_store(vtty, c);
                        }
                    }
                    IAC => {
                        vtty.as_mut().input_state = VTTY_INPUT_TELNET;
                    }
                    // NULL - Must be ignored - generated by Linux telnet
                    0 => {}
                    // LF (Line Feed) - Must be ignored on Windows platform
                    10 => {}
                    // Store a standard character
                    _ => {
                        vtty_store(vtty, c);
                    }
                }
            }

            VTTY_INPUT_VT1 => {
                match c {
                    0x5b => {
                        vtty.as_mut().input_state = VTTY_INPUT_VT2;
                        return;
                    }
                    _ => {
                        vtty_store(vtty, 0x1b);
                        vtty_store(vtty, c);
                    }
                }
                vtty.as_mut().input_state = VTTY_INPUT_TEXT;
            }

            VTTY_INPUT_VT2 => {
                match c {
                    // Up Arrow
                    0x41 => {
                        vtty_store(vtty, 16);
                    }
                    // Down Arrow
                    0x42 => {
                        vtty_store(vtty, 14);
                    }
                    // Right Arrow
                    0x43 => {
                        vtty_store(vtty, 6);
                    }
                    // Left Arrow
                    0x44 => {
                        vtty_store(vtty, 2);
                    }
                    _ => {
                        vtty_store(vtty, 0x5b);
                        vtty_store(vtty, 0x1b);
                        vtty_store(vtty, c);
                    }
                }
                vtty.as_mut().input_state = VTTY_INPUT_TEXT;
            }

            VTTY_INPUT_REMOTE => {
                remote_control(vtty, c);
                vtty.as_mut().input_state = VTTY_INPUT_TEXT;
            }

            VTTY_INPUT_TELNET => {
                vtty.as_mut().telnet_cmd = c.into();
                match c {
                    WILL | WONT | DO | DONT => {
                        vtty.as_mut().input_state = VTTY_INPUT_TELNET_IYOU;
                        return;
                    }
                    SB => {
                        vtty.as_mut().telnet_cmd = c.into();
                        vtty.as_mut().input_state = VTTY_INPUT_TELNET_SB1;
                        return;
                    }
                    SE => {}
                    IAC => {
                        vtty_store(vtty, IAC);
                    }
                    _ => {}
                }
                vtty.as_mut().input_state = VTTY_INPUT_TEXT;
            }

            VTTY_INPUT_TELNET_IYOU => {
                vtty.as_mut().telnet_opt = c.into();
                // if telnet client can support ttype, ask it to send ttype string
                if vtty.as_ref().telnet_cmd == WILL.into() && vtty.as_ref().telnet_opt == TELOPT_TTYPE.into() {
                    vtty_put_char(vtty, IAC as c_char);
                    vtty_put_char(vtty, SB as c_char);
                    vtty_put_char(vtty, TELOPT_TTYPE as c_char);
                    vtty_put_char(vtty, TELQUAL_SEND as c_char);
                    vtty_put_char(vtty, IAC as c_char);
                    vtty_put_char(vtty, SE as c_char);
                }
                vtty.as_mut().input_state = VTTY_INPUT_TEXT;
            }

            VTTY_INPUT_TELNET_SB1 => {
                vtty.as_mut().telnet_opt = c.into();
                vtty.as_mut().input_state = VTTY_INPUT_TELNET_SB2;
            }

            VTTY_INPUT_TELNET_SB2 => {
                vtty.as_mut().telnet_qual = c.into();
                if vtty.as_ref().telnet_opt == TELOPT_TTYPE.into() && vtty.as_ref().telnet_qual == TELQUAL_IS.into() {
                    vtty.as_mut().input_state = VTTY_INPUT_TELNET_SB_TTYPE;
                } else {
                    vtty.as_mut().input_state = VTTY_INPUT_TELNET_NEXT;
                }
            }

            VTTY_INPUT_TELNET_SB_TTYPE => {
                // parse ttype string: first char is sufficient
                // if client is xterm or vt, set the title bar
                if c == b'x' || c == b'X' || c == b'v' || c == b'V' {
                    fd_puts(*fd_slot.as_ref(), 0, &format!("\033]0;{}\07", vm_get_name(vtty.as_ref().vm).as_str()));
                }
                vtty.as_mut().input_state = VTTY_INPUT_TELNET_NEXT;
            }

            VTTY_INPUT_TELNET_NEXT => {
                // ignore all chars until next IAC
                if c == IAC {
                    vtty.as_mut().input_state = VTTY_INPUT_TELNET;
                }
            }

            _ => {}
        }
    }
}

/// VTTY TCP input
extern "C" fn vtty_tcp_input(fd_slot: *mut c_int, opt: *mut c_void) {
    let vtty = NonNull::new(opt.cast::<vtty_t>()).unwrap();
    let fd_slot = NonNull::new(fd_slot).unwrap();
    vtty_read_and_store(vtty, fd_slot);
}

// VTTY thread
extern "C" fn vtty_thread_main(_arg: *mut c_void) -> *mut c_void {
    unsafe {
        let mut rfds = zeroed::<libc::fd_set>();
        let mut fd_max;
        let mut tv = zeroed::<libc::timeval>();
        loop {
            // Build the FD set
            vtty_list_lock();
            libc::FD_ZERO(addr_of_mut!(rfds));
            fd_max = -1;
            let mut next_vtty = VTTY_LIST;
            while !next_vtty.is_null() {
                let mut vtty = NonNull::new(next_vtty).unwrap();
                next_vtty = vtty.as_ref().next;

                match vtty.as_ref().type_ {
                    VTTY_TYPE_TCP => {
                        for i in 0..vtty.as_ref().fd_count as usize {
                            if vtty.as_ref().fd_array[i] != -1 {
                                libc::FD_SET(vtty.as_ref().fd_array[i], addr_of_mut!(rfds));
                                if vtty.as_ref().fd_array[i] > fd_max {
                                    fd_max = vtty.as_ref().fd_array[i];
                                }
                            }
                        }

                        let fd_tcp = fd_pool_set_fds(addr_of_mut!(vtty.as_mut().fd_pool), addr_of_mut!(rfds));
                        fd_max = max(fd_tcp, fd_max);
                    }

                    _ => {
                        if vtty.as_ref().fd_array[0] != -1 {
                            libc::FD_SET(vtty.as_ref().fd_array[0], addr_of_mut!(rfds));
                            fd_max = max(vtty.as_ref().fd_array[0], fd_max);
                        }
                    }
                }
            }
            vtty_list_unlock();

            // Wait for incoming data
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000; // 50 ms
            let res = libc::select(fd_max + 1, addr_of_mut!(rfds), null_mut(), null_mut(), addr_of_mut!(tv));

            if res == -1 {
                if errno() != libc::EINTR {
                    perror("vtty_thread: select");
                }
                continue;
            }

            // Examine active FDs and call user handlers
            vtty_list_lock();
            let mut next_vtty = VTTY_LIST;
            while !next_vtty.is_null() {
                let mut vtty = NonNull::new(next_vtty).unwrap();
                next_vtty = vtty.as_ref().next;

                match vtty.as_ref().type_ {
                    VTTY_TYPE_TCP => {
                        // check incoming connection
                        for i in 0..vtty.as_ref().fd_count as usize {
                            if vtty.as_ref().fd_array[i] == -1 {
                                continue;
                            }

                            if !libc::FD_ISSET(vtty.as_ref().fd_array[i], addr_of_mut!(rfds)) {
                                continue;
                            }

                            vtty_tcp_conn_accept(vtty, i as c_int);
                        }

                        // check established connection
                        fd_pool_check_input(addr_of_mut!(vtty.as_mut().fd_pool), addr_of_mut!(rfds), Some(vtty_tcp_input), vtty.as_ptr().cast::<_>());
                    }

                    // Term, Serial
                    _ => {
                        if vtty.as_ref().fd_array[0] != -1 && libc::FD_ISSET(vtty.as_ref().fd_array[0], addr_of!(rfds)) {
                            vtty_read_and_store(vtty, NonNull::new_unchecked(addr_of_mut!(vtty.as_mut().fd_array[0])));
                            vtty.as_mut().input_pending = 1;
                        }
                    }
                }

                if vtty.as_ref().input_pending != 0 {
                    if let Some(read_notifier) = vtty.as_ref().read_notifier {
                        read_notifier(vtty.as_ptr());
                    }

                    vtty.as_mut().input_pending = 0;
                }

                // Flush any pending output
                if vtty.as_ref().managed_flush == 0 {
                    vtty_flush(vtty);
                }
            }
            vtty_list_unlock();
        }
    }
}

/// Initialize the VTTY thread
#[no_mangle]
pub extern "C" fn vtty_init() -> c_int {
    unsafe {
        if libc::pthread_create(addr_of_mut!(VTTY_THREAD), null_mut(), vtty_thread_main, null_mut()) != 0 {
            perror("vtty: pthread_create");
            return -1;
        }

        0
    }
}
