//! TODO doc

use crate::c::dynamips::*;
use crate::c::prelude::*;
use crate::c::utils::*;

/// VTTY list (TODO private)
#[no_mangle]
pub static mut vtty_list_mutex: libc::pthread_mutex_t = libc::PTHREAD_MUTEX_INITIALIZER;
// TODO private
#[no_mangle]
pub static mut vtty_list: *mut vtty_t = null_mut();
static mut TIOS: libc::termios = unsafe { zeroed::<libc::termios>() };
static mut TIOS_ORIG: libc::termios = unsafe { zeroed::<libc::termios>() };

fn vtty_list_lock() {
    unsafe { libc::pthread_mutex_lock(addr_of_mut!(vtty_list_mutex)) };
}
fn vtty_list_unlock() {
    unsafe { libc::pthread_mutex_unlock(addr_of_mut!(vtty_list_mutex)) };
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
        vtty.next = vtty_list;
        vtty.pprev = addr_of_mut!(vtty_list);

        if !vtty_list.is_null() {
            (*vtty_list).pprev = addr_of_mut!(vtty.next);
        }

        vtty_list = addr_of_mut!(*vtty);
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

/// Store a character in the FIFO buffer
fn vtty_store(vtty: &mut vtty_t, c: u8) -> c_int {
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
            if vtty_store(vtty, *data.wrapping_add(bytes as usize) as u8) == -1 {
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

        vtty_flush(vtty.as_ptr());
    }
}
