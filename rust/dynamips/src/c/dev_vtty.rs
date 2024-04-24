//! TODO doc

use crate::c::dynamips::*;
use crate::c::prelude::*;
use crate::c::utils::*;
use crate::c::vm::*;
use circular_buffer::CircularBuffer;
use std::cmp::max;
use std::sync::Mutex;
use std::sync::Once;
use std::sync::OnceLock;

static mut CTRL_CODE_OK: c_int = 1;
static mut TELNET_MESSAGE_OK: c_int = 1;
static mut VTTY_THREAD: libc::pthread_t = 0;
static mut TIOS: libc::termios = unsafe { zeroed::<libc::termios>() };
static mut TIOS_ORIG: libc::termios = unsafe { zeroed::<libc::termios>() };

/// Allow vtty pointers to be sent between threads. (unsafe)
#[repr(transparent)]
struct VttyPtr(pub *mut Vtty);
unsafe impl Send for VttyPtr {}

/// VTTY list
fn vtty_list() -> &'static Mutex<Vec<VttyPtr>> {
    static VTTY_LIST: OnceLock<Mutex<Vec<VttyPtr>>> = OnceLock::new();
    VTTY_LIST.get_or_init(|| Mutex::new(Vec::new()))
}

/// 4 Kb should be enough for a keyboard buffer
pub const VTTY_BUFFER_SIZE: usize = 4096;

/// Maximum listening socket number
pub const VTTY_MAX_FD: usize = 10;

/// VTTY connection types
#[derive(Default, Debug)]
pub enum VttyType {
    #[default]
    None,
    Term,
    Tcp,
    Serial,
}
pub const VTTY_TYPE_NONE: c_int = 0;
pub const VTTY_TYPE_TERM: c_int = 1;
pub const VTTY_TYPE_TCP: c_int = 2;
pub const VTTY_TYPE_SERIAL: c_int = 3;

/// VTTY input states
#[derive(Default)]
pub enum VttyInput {
    #[default]
    Text,
    Vt1,
    Vt2,
    Remote,
    Telnet,
    TelnetIyou(u8),
    TelnetSb1(u8),
    TelnetSb2(u8, u8),
    TelnetSbTtype(u8, u8, u8),
    TelnetNext,
}

#[no_mangle]
pub extern "C" fn vtty_parse_serial_option(mut option: NonNull<vtty_serial_option_t>, optarg: NonNull<c_char>) -> c_int {
    unsafe {
        if let Err(err) = option.as_mut().parse(optarg.as_ptr().as_str()) {
            eprintln!("vtty_parse_serial_option: {}", err);
            -1
        } else {
            0
        }
    }
}

/// Virtual TTY
#[derive(Default)]
pub struct Vtty {
    pub c: virtual_tty,
    pub name: String,
    pub type_: VttyType,
    pub tcp_port: u16,
    pub terminal_support: bool,
    pub input_pending: bool,
    pub input_state: VttyInput,
    pub fd_array: Vec<c_int>,
    /// FD Pool (for TCP connections)
    pub fd_pool: Mutex<Vec<c_int>>,
    pub buffer: Mutex<CircularBuffer<VTTY_BUFFER_SIZE, u8>>,
    /// Old text for replay
    pub replay_buffer: CircularBuffer<VTTY_BUFFER_SIZE, u8>,
}
impl Vtty {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn to_c(ptr: *mut Self) -> *mut vtty_t {
        unsafe { ptr.byte_add(offset_of!(Self, c)).cast::<_>() }
    }
    pub fn from_c(ptr: *mut vtty_t) -> *mut Self {
        unsafe { ptr.byte_sub(offset_of!(Self, c)).cast::<_>() }
    }
}
impl From<&mut Vtty> for NonNull<virtual_tty> {
    fn from(vtty: &mut Vtty) -> Self {
        NonNull::new(addr_of_mut!(vtty.c)).unwrap()
    }
}
impl From<NonNull<virtual_tty>> for &mut Vtty {
    fn from(vtty: NonNull<virtual_tty>) -> Self {
        unsafe { &mut *Vtty::from_c(vtty.as_ptr()) }
    }
}
#[cfg(test)]
#[test]
fn test_vtty_as_c_from_c_roundtrip() {
    let mut x = Box::new(Vtty::new());
    let ptr = addr_of_mut!(*x);
    let to_ptr = Vtty::to_c(ptr);
    let offset = unsafe { to_ptr.byte_offset_from(ptr) };
    assert_eq!(offset, offset_of!(Vtty, c) as isize);
    let from_ptr = Vtty::from_c(to_ptr);
    assert_eq!(ptr, from_ptr);
}

/// Virtual TTY structure
#[repr(C)]
pub struct virtual_tty {
    pub vm: *mut vm_instance_t,
    pub managed_flush: c_int,
    pub priv_data: *mut c_void,
    pub user_arg: c_ulong,
    /// Read notification
    pub read_notifier: Option<unsafe extern "C" fn(_: *mut virtual_tty)>,
}
pub type vtty_t = virtual_tty;
impl virtual_tty {
    pub fn new() -> Self {
        Self { vm: null_mut(), managed_flush: 0, priv_data: null_mut(), user_arg: 0, read_notifier: None }
    }
}
impl Default for virtual_tty {
    fn default() -> Self {
        Self::new()
    }
}

/// Terminal code.
pub struct VttyTerm;
impl VttyTerm {
    /// Restore TTY original settings
    extern "C" fn reset() {
        unsafe { libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, addr_of!(TIOS_ORIG)) };
    }
    /// Initialize real TTY
    pub fn init() {
        static INIT: Once = Once::new();
        INIT.call_once(|| unsafe {
            libc::tcgetattr(libc::STDIN_FILENO, addr_of_mut!(TIOS));

            libc::memcpy(addr_of_mut!(TIOS_ORIG).cast::<_>(), addr_of_mut!(TIOS).cast::<_>(), size_of::<libc::termios>());
            libc::atexit(VttyTerm::reset);

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
        });
    }
    /// Read a character from the terminal
    pub fn read(vtty: &mut Vtty) -> c_int {
        unsafe {
            let mut c: u8 = 0;

            for &fd in vtty.fd_array.iter() {
                if fd != -1 && libc::read(fd, addr_of_mut!(c).cast::<_>(), 1) == 1 {
                    return c.into();
                }
            }

            perror("read from vtty failed");
            -1
        }
    }
}

/// Tcp code.
pub struct VttyTcp;
impl VttyTcp {
    /// Wait for a TCP connection (IPv4+IPv6)
    #[cfg(has_ipv6)]
    fn conn_wait_ipv4_ipv6(vtty: &mut Vtty) -> c_int {
        unsafe {
            let one: c_int = 1;

            vtty.fd_array.clear();

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
                perror("VttyTcp::conn_wait_ipv4_ipv6: getaddrinfo");
                return -1;
            }

            let mut next_res = res0;
            while !next_res.is_null() {
                let res = &mut *next_res;
                next_res = res.ai_next;

                if res.ai_family != libc::PF_INET && res.ai_family != libc::PF_INET6 {
                    continue;
                }

                let fd = libc::socket(res.ai_family, res.ai_socktype, res.ai_protocol);

                if fd < 0 {
                    continue;
                }

                if libc::setsockopt(fd, libc::SOL_SOCKET, libc::SO_REUSEADDR, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                    perror("VttyTcp::conn_wait_ipv4_ipv6: setsockopt(SO_REUSEADDR)");
                }

                if libc::setsockopt(fd, libc::SOL_SOCKET, libc::SO_KEEPALIVE, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                    perror("VttyTcp::conn_wait_ipv4_ipv6: setsockopt(SO_KEEPALIVE)");
                }

                // Send telnet packets asap. Dont wait to fill packets up
                if libc::setsockopt(fd, libc::SOL_TCP, libc::TCP_NODELAY, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                    perror("VttyTcp::conn_wait_ipv4_ipv6: setsockopt(TCP_NODELAY)");
                }

                if libc::bind(fd, res.ai_addr, res.ai_addrlen) < 0 || libc::listen(fd, 1) < 0 {
                    libc::close(fd);
                    continue;
                }

                let proto = if res.ai_family == libc::PF_INET6 { "IPv6" } else { "IPv4" };
                vm_log(vtty.c.vm, "VTTY", &format!("{}: waiting connection on tcp port {} for protocol {} (FD {})\n", vtty.name.as_str(), vtty.tcp_port, proto, fd));

                vtty.fd_array.push(fd);
            }

            libc::freeaddrinfo(res0);
            vtty.fd_array.len() as c_int
        }
    }
    /// Wait for a TCP connection (IPv4)
    fn conn_wait_ipv4(vtty: &mut Vtty) -> c_int {
        unsafe {
            let one: c_int = 1;

            vtty.fd_array.clear();

            let fd = libc::socket(libc::PF_INET, libc::SOCK_STREAM, 0);
            if fd < 0 {
                perror("VttyTcp::conn_wait_ipv4: socket");
                return -1;
            }

            if libc::setsockopt(fd, libc::SOL_SOCKET, libc::SO_REUSEADDR, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("VttyTcp::conn_wait_ipv4: setsockopt(SO_REUSEADDR)");
                libc::close(fd);
                return -1;
            }

            if libc::setsockopt(fd, libc::SOL_SOCKET, libc::SO_KEEPALIVE, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("VttyTcp::conn_wait_ipv4: setsockopt(SO_KEEPALIVE)");
                libc::close(fd);
                return -1;
            }

            // Send telnet packets asap. Dont wait to fill packets up
            if libc::setsockopt(fd, libc::SOL_TCP, libc::TCP_NODELAY, addr_of!(one).cast::<_>(), size_of::<c_int>() as u32) < 0 {
                perror("VttyTcp::conn_wait_ipv4: setsockopt(TCP_NODELAY)");
                libc::close(fd);
                return -1;
            }

            let mut serv = zeroed::<libc::sockaddr_in>();
            serv.sin_family = libc::AF_INET as u16;
            serv.sin_addr.s_addr = libc::INADDR_ANY.to_be();
            serv.sin_port = vtty.tcp_port.to_be();

            if libc::bind(fd, addr_of!(serv).cast::<_>(), size_of::<libc::sockaddr_in>() as u32) < 0 {
                perror("VttyTcp::conn_wait_ipv4: bind");
                libc::close(fd);
                return -1;
            }

            if libc::listen(fd, 1) < 0 {
                perror("VttyTcp::conn_wait_ipv4: listen");
                libc::close(fd);
                return -1;
            }

            vm_log(vtty.c.vm, "VTTY", &format!("{}: waiting connection on tcp port {} (FD {})\n", vtty.name.as_str(), vtty.tcp_port, fd));

            vtty.fd_array.push(fd);
            vtty.fd_array.len() as c_int
        }
    }
    /// Accept a TCP connection
    fn conn_accept(vtty: NonNull<vtty_t>, nsock: c_int) -> c_int {
        unsafe {
            let vtty = Vtty::from_c(vtty.as_ptr()).as_mut().unwrap();
            let nsock = nsock as usize;

            let fd = libc::accept(vtty.fd_array[nsock], null_mut(), null_mut());
            if fd < 0 {
                vm_error(vtty.c.vm, &format!("VttyTcp::conn_accept: accept on port {} failed {}\n", vtty.tcp_port, strerror(errno())));
                return -1;
            }

            // Register the new FD
            vtty.fd_pool.lock().unwrap().push(fd);

            vm_log(vtty.c.vm, "VTTY", &format!("{} is now connected (accept_fd={},conn_fd={})\n", vtty.name.as_str(), vtty.fd_array[nsock], fd));

            // Adapt Telnet settings
            if vtty.terminal_support {
                Telnet::do_ttype(fd);
                Telnet::will_echo(fd);
                Telnet::will_suppress_go_ahead(fd);
                Telnet::dont_linemode(fd);
                vtty.input_state = VttyInput::Text;
            }

            if TELNET_MESSAGE_OK == 1 {
                fd_puts(fd, 0, &format!("Connected to Dynamips VM \"{}\" (ID {}, type {}) - {}\r\nPress ENTER to get the prompt.\r\n", vm_get_name(vtty.c.vm).as_str(), vm_get_instance_id(vtty.c.vm), vm_get_type(vtty.c.vm).as_str(), vtty.name.as_str()));
                // replay old text
                let mut bytes = vtty.replay_buffer.make_contiguous();
                while !bytes.is_empty() {
                    let n = libc::send(fd, bytes.as_ptr().cast::<_>(), bytes.len(), 0);
                    if n < 0 {
                        perror("VttyTcp::conn_accept: send");
                        break;
                    }
                    bytes = &mut bytes[n as usize..];
                }
                // warn if not running
                if vm_get_status(vtty.c.vm) != VM_STATUS_RUNNING {
                    fd_puts(fd, 0, &format!("\r\n!!! WARNING - VM is not running, will be unresponsive (status={}) !!!\r\n", vm_get_status(vtty.c.vm)));
                }
                vtty_flush((&mut vtty.c).into());
            }
            0
        }
    }
    /// Read a character from the TCP connection.
    fn read(_vtty: &mut Vtty, fd_slot: &mut c_int) -> c_int {
        unsafe {
            let fd = *fd_slot;
            let mut c: u8 = 0;

            if libc::read(fd, addr_of_mut!(c).cast::<_>(), 1) == 1 {
                return c.into();
            }

            // problem with the connection
            libc::shutdown(fd, 2);
            libc::close(fd);
            *fd_slot = -1;

            // Shouldn't happen...
            -1
        }
    }
}

/// Commmand line support utility
#[repr(C)]
#[derive(Debug)]
pub struct VttySerialOption {
    /// must free this pointer if C deallocates
    pub device: *mut c_char,
    pub baudrate: c_int,
    pub databits: c_int,
    pub parity: c_int,
    pub stopbits: c_int,
    pub hwflow: c_int,
}
pub type vtty_serial_option_t = VttySerialOption;
impl VttySerialOption {
    /// Parse serial interface descriptor string, return true if success
    /// string takes the form "device:baudrate:databits:parity:stopbits:hwflow"
    /// device is mandatory, other options are optional (default=9600,8,N,1,0).
    fn parse(&mut self, optarg: &str) -> Result<(), &'static str> {
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
impl Drop for VttySerialOption {
    fn drop(&mut self) {
        if !self.device.is_null() {
            unsafe { libc::free(self.device.cast::<_>()) };
            self.device = null_mut();
        }
    }
}

/// Serial code.
pub struct VttySerial;
impl VttySerial {
    /// #if defined(__CYGWIN__) || defined(SUNOS)
    fn cfmakeraw(tios: &mut libc::termios) {
        tios.c_iflag &= !(libc::IGNBRK | libc::BRKINT | libc::PARMRK | libc::ISTRIP | libc::INLCR | libc::IGNCR | libc::ICRNL | libc::IXON);
        tios.c_oflag &= !libc::OPOST;
        tios.c_lflag &= !(libc::ECHO | libc::ECHONL | libc::ICANON | libc::ISIG | libc::IEXTEN);
        tios.c_cflag &= !(libc::CSIZE | libc::PARENB);
        tios.c_cflag |= libc::CS8;
    }
    /// Setup serial port, return 0 if success.
    fn setup(vtty: &mut Vtty, option: &VttySerialOption) -> c_int {
        unsafe {
            let mut tio = zeroed::<libc::termios>();

            if libc::tcgetattr(vtty.fd_array[0], &mut tio) != 0 {
                eprintln!("vtty_serial_setup: tcgetattr failed");
                return -1;
            }

            #[cfg(has_libc_cfmakeraw)]
            libc::cfmakeraw(addr_of_mut!(tio));
            #[cfg(not(has_libc_cfmakeraw))]
            VttySerial::cfmakeraw(&mut tio);

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
}

/// Create a virtual tty
#[no_mangle]
pub extern "C" fn vtty_create(vm: *mut vm_instance_t, name: NonNull<c_char>, type_: c_int, tcp_port: c_int, option: *const vtty_serial_option_t) -> *mut vtty_t {
    unsafe {
        let option = NonNull::new(option.cast_mut()).unwrap();
        let mut vtty = Box::new(Vtty::new());
        vtty.name = name.as_ptr().as_str().into();
        vtty.c.vm = vm;
        vtty.terminal_support = true;
        vtty.input_state = VttyInput::Text;
        vtty.fd_array.clear();
        vtty.fd_pool.lock().unwrap().clear();

        match type_ {
            VTTY_TYPE_NONE => {
                vtty.type_ = VttyType::None;
            }

            VTTY_TYPE_TERM => {
                vtty.type_ = VttyType::Term;
                VttyTerm::init();
                vtty.fd_array.push(libc::STDIN_FILENO);
            }

            VTTY_TYPE_TCP => {
                vtty.type_ = VttyType::Tcp;
                if let Ok(tcp_port) = u16::try_from(tcp_port) {
                    vtty.tcp_port = tcp_port;
                    #[cfg(feature = "ENABLE_IPV6")]
                    VttyTcp::conn_wait_ipv4_ipv6(&mut vtty);
                    #[cfg(not(feature = "ENABLE_IPV6"))]
                    VttyTcp::conn_wait_ipv4(&mut vtty);
                } else {
                    eprintln!("VTTY: invalid tcp_port {}", tcp_port);
                    return null_mut();
                }
            }

            VTTY_TYPE_SERIAL => {
                vtty.type_ = VttyType::Serial;
                vtty.fd_array.push(libc::open(option.as_ref().device, libc::O_RDWR));
                if vtty.fd_array[0] < 0 {
                    eprintln!("VTTY: open failed");
                    return null_mut();
                }
                if VttySerial::setup(&mut vtty, option.as_ref()) != 0 {
                    eprintln!("VTTY: setup failed");
                    libc::close(vtty.fd_array[0]);
                    return null_mut();
                }
                vtty.terminal_support = false;
            }

            _ => {
                eprintln!("tty_create: bad vtty type {}", type_);
                return null_mut();
            }
        }

        // Add this new VTTY to the list
        let mut list = vtty_list().lock().unwrap();
        list.push(VttyPtr(addr_of_mut!(*vtty)));
        drop(list);
        Vtty::to_c(Box::into_raw(vtty)) // memory managed by C
    }
}

/// Delete a virtual tty
#[no_mangle]
pub extern "C" fn vtty_delete(vtty: *mut vtty_t) {
    unsafe {
        if !vtty.is_null() {
            let mut vtty = Box::from_raw(Vtty::from_c(vtty)); // memory managed by rust
            let mut list = vtty_list().lock().unwrap();
            list.retain(|x| x.0 != addr_of_mut!(*vtty));
            drop(list);

            match vtty.type_ {
                VttyType::Tcp => {
                    for i in 0..vtty.fd_array.len() {
                        if vtty.fd_array[i] != -1 {
                            vm_log(vtty.c.vm, "VTTY", &format!("{}: closing FD {}\n", vtty.name.as_str(), vtty.fd_array[i]));
                            libc::close(vtty.fd_array[i]);
                        }
                    }

                    let fd_pool: Vec<_> = take(&mut vtty.fd_pool.lock().unwrap());
                    for fd in fd_pool {
                        if fd != -1 {
                            libc::shutdown(fd, 2);
                            libc::close(fd);
                        }
                    }
                }

                _ => {
                    // We don't close FD 0 since it is stdin
                    for fd in vtty.fd_array {
                        if fd > 0 {
                            vm_log(vtty.c.vm, "VTTY", &format!("{}: closing FD {}\n", vtty.name.as_str(), fd));
                            libc::close(fd);
                        }
                    }
                }
            }
        }
    }
}

/// Store a character in the FIFO buffer (TODO private)
#[no_mangle]
pub extern "C" fn vtty_store(vtty: NonNull<vtty_t>, c: u8) -> c_int {
    unsafe {
        let vtty = Vtty::from_c(vtty.as_ptr()).as_mut().unwrap();
        if vtty.buffer.lock().unwrap().try_push_back(c).is_ok() {
            0
        } else {
            -1
        }
    }
}

/// Store arbritary data in the FIFO buffer
#[no_mangle]
pub extern "C" fn vtty_store_data(vtty: *mut vtty_t, data: *mut c_char, len: c_int) -> c_int {
    unsafe {
        if vtty.is_null() || data.is_null() || len < 0 {
            return -1; // invalid argument
        }

        let vtty = &mut *Vtty::from_c(vtty);
        let mut bytes = 0;
        while bytes < len {
            if vtty_store(vtty.into(), *data.wrapping_add(bytes as usize) as u8) == -1 {
                break;
            }
            bytes += 1;
        }

        vtty.input_pending = true;
        bytes
    }
}

/// Read a character from the buffer (-1 if the buffer is empty)
#[no_mangle]
pub extern "C" fn vtty_get_char(vtty: NonNull<vtty_t>) -> c_int {
    unsafe {
        let vtty = Vtty::from_c(vtty.as_ptr()).as_mut().unwrap();
        if let Some(c) = vtty.buffer.lock().unwrap().pop_front() {
            c.into()
        } else {
            -1
        }
    }
}

/// Put char to vtty
#[no_mangle]
pub extern "C" fn vtty_put_char(vtty: NonNull<vtty_t>, ch: c_char) {
    unsafe {
        let vtty: &mut Vtty = vtty.into();

        match vtty.type_ {
            VttyType::None => {}

            VttyType::Term | VttyType::Serial => {
                for &fd in vtty.fd_array.iter() {
                    if fd != -1 && libc::write(fd, addr_of!(ch).cast::<_>(), 1) != 1 {
                        vm_log(vtty.c.vm, "VTTY", &format!("{}: put char 0x{:02x} failed ({})\n", vtty.name.as_str(), ch, strerror(errno())));
                    }
                }
            }

            VttyType::Tcp => {
                let fd_pool: Vec<_> = vtty.fd_pool.lock().unwrap().clone();
                for fd in fd_pool {
                    if fd != -1 && libc::send(fd, addr_of!(ch).cast::<_>(), 1, 0) != 1 {
                        vtty.fd_pool.lock().unwrap().retain(|tfd| *tfd != fd);
                        libc::shutdown(fd, 2);
                        libc::close(fd);
                    }
                }
            }
        }

        // store char for replay
        vtty.replay_buffer.push_back(ch as u8);
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
        let vtty: &mut Vtty = vtty.into();
        match vtty.type_ {
            VttyType::Term | VttyType::Serial => {
                for &fd in vtty.fd_array.iter() {
                    if fd != -1 {
                        libc::fsync(fd);
                    }
                }
            }
            _ => {}
        }
    }
}

/// Returns TRUE if a character is available in buffer
#[no_mangle]
pub extern "C" fn vtty_is_char_avail(vtty: NonNull<vtty_t>) -> c_int {
    unsafe {
        let vtty = Vtty::from_c(vtty.as_ptr()).as_mut().unwrap();
        let not_empty = !vtty.buffer.lock().unwrap().is_empty();
        not_empty.into()
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

/// Telnet code.
pub struct Telnet;
impl Telnet {
    // from arpa/telnet.h
    /// interpret as command
    pub const IAC: u8 = 255;
    /// you are not to use option
    pub const DONT: u8 = 254;
    /// please, you use option
    pub const DO: u8 = 253;
    /// I won't use option
    pub const WONT: u8 = 252;
    /// I will use option
    pub const WILL: u8 = 251;
    /// interpret as subnegotiation
    pub const SB: u8 = 250;
    /// end sub negotiation
    pub const SE: u8 = 240;
    /// echo
    pub const TELOPT_ECHO: u8 = 1;
    /// suppress go ahead
    pub const TELOPT_SGA: u8 = 3;
    /// terminal type
    pub const TELOPT_TTYPE: u8 = 24;
    /// Linemode option
    pub const TELOPT_LINEMODE: u8 = 34;
    /// option is...
    pub const TELQUAL_IS: u8 = 0;
    /// send option
    pub const TELQUAL_SEND: u8 = 1;
    /// Send Telnet command: WILL TELOPT_ECHO
    pub fn will_echo(fd: c_int) {
        unsafe {
            let cmd = [Self::IAC, Self::WILL, Self::TELOPT_ECHO];
            libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
        }
    }
    /// Send Telnet command: Suppress Go-Ahead
    pub fn will_suppress_go_ahead(fd: c_int) {
        unsafe {
            let cmd = [Self::IAC, Self::WILL, Self::TELOPT_SGA];
            libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
        }
    }
    /// Send Telnet command: Don't use linemode
    pub fn dont_linemode(fd: c_int) {
        unsafe {
            let cmd = [Self::IAC, Self::DONT, Self::TELOPT_LINEMODE];
            libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
        }
    }
    /// Send Telnet command: does the client support terminal type message?
    pub fn do_ttype(fd: c_int) {
        unsafe {
            let cmd = [Self::IAC, Self::DO, Self::TELOPT_TTYPE];
            libc::write(fd, cmd.as_ptr().cast::<_>(), cmd.len());
        }
    }
}

/// Read a character from the virtual TTY.
///
/// If the VTTY is a TCP connection, restart it in case of error.
fn vtty_read(vtty: &mut Vtty, fd_slot: &mut c_int) -> c_int {
    match vtty.type_ {
        VttyType::Term | VttyType::Serial => VttyTerm::read(vtty),
        VttyType::Tcp => VttyTcp::read(vtty, fd_slot),
        _ => {
            eprintln!("vtty_read: bad vtty type {:?}\n", vtty.type_);
            -1
        }
    }
}

/// Read a character (until one is available) and store it in buffer
fn vtty_read_and_store(vtty: &mut Vtty, fd_slot: &mut c_int) {
    unsafe {
        // wait until we get a character input
        let c = vtty_read(vtty, fd_slot);

        // if read error, do nothing
        if c < 0 {
            return;
        }
        let c = c as u8;

        // If something was read, make sure the handler is informed
        vtty.input_pending = true;

        if !vtty.terminal_support {
            vtty_store(vtty.into(), c);
            return;
        }

        match vtty.input_state {
            VttyInput::Text => {
                match c {
                    0x1b => {
                        vtty.input_state = VttyInput::Vt1;
                    }

                    /* Ctrl + ']' (0x1d, 29), or Alt-Gr + '*' (0xb3, 179) */
                    0x1d | 0xb3 => {
                        if CTRL_CODE_OK == 1 {
                            vtty.input_state = VttyInput::Remote;
                        } else {
                            vtty_store(vtty.into(), c);
                        }
                    }
                    Telnet::IAC => {
                        vtty.input_state = VttyInput::Telnet;
                    }
                    // NULL - Must be ignored - generated by Linux telnet
                    0 => {}
                    // LF (Line Feed) - Must be ignored on Windows platform
                    10 => {}
                    // Store a standard character
                    _ => {
                        vtty_store(vtty.into(), c);
                    }
                }
            }

            VttyInput::Vt1 => {
                match c {
                    0x5b => {
                        vtty.input_state = VttyInput::Vt2;
                        return;
                    }
                    _ => {
                        vtty_store(vtty.into(), 0x1b);
                        vtty_store(vtty.into(), c);
                    }
                }
                vtty.input_state = VttyInput::Text;
            }

            VttyInput::Vt2 => {
                match c {
                    // Up Arrow
                    0x41 => {
                        vtty_store(vtty.into(), 16);
                    }
                    // Down Arrow
                    0x42 => {
                        vtty_store(vtty.into(), 14);
                    }
                    // Right Arrow
                    0x43 => {
                        vtty_store(vtty.into(), 6);
                    }
                    // Left Arrow
                    0x44 => {
                        vtty_store(vtty.into(), 2);
                    }
                    _ => {
                        vtty_store(vtty.into(), 0x1b);
                        vtty_store(vtty.into(), 0x5b);
                        vtty_store(vtty.into(), c);
                    }
                }
                vtty.input_state = VttyInput::Text;
            }

            VttyInput::Remote => {
                remote_control(vtty.into(), c);
                vtty.input_state = VttyInput::Text;
            }

            VttyInput::Telnet => {
                match c {
                    Telnet::WILL | Telnet::WONT | Telnet::DO | Telnet::DONT => {
                        vtty.input_state = VttyInput::TelnetIyou(c);
                        return;
                    }
                    Telnet::SB => {
                        vtty.input_state = VttyInput::TelnetSb1(c);
                        return;
                    }
                    Telnet::SE => {}
                    Telnet::IAC => {
                        vtty_store(vtty.into(), Telnet::IAC);
                    }
                    _ => {}
                }
                vtty.input_state = VttyInput::Text;
            }

            VttyInput::TelnetIyou(cmd) => {
                let opt = c;
                // if telnet client can support ttype, ask it to send ttype string
                if cmd == Telnet::WILL && opt == Telnet::TELOPT_TTYPE {
                    vtty_put_char(vtty.into(), Telnet::IAC as c_char);
                    vtty_put_char(vtty.into(), Telnet::SB as c_char);
                    vtty_put_char(vtty.into(), Telnet::TELOPT_TTYPE as c_char);
                    vtty_put_char(vtty.into(), Telnet::TELQUAL_SEND as c_char);
                    vtty_put_char(vtty.into(), Telnet::IAC as c_char);
                    vtty_put_char(vtty.into(), Telnet::SE as c_char);
                }
                vtty.input_state = VttyInput::Text;
            }

            VttyInput::TelnetSb1(cmd) => {
                let opt = c;
                vtty.input_state = VttyInput::TelnetSb2(cmd, opt);
            }

            VttyInput::TelnetSb2(cmd, opt) => {
                let qual = c;
                if opt == Telnet::TELOPT_TTYPE && qual == Telnet::TELQUAL_IS {
                    vtty.input_state = VttyInput::TelnetSbTtype(cmd, opt, qual);
                } else {
                    vtty.input_state = VttyInput::TelnetNext;
                }
            }

            VttyInput::TelnetSbTtype(_cmd, _opt, _qual) => {
                // parse ttype string: first char is sufficient
                // if client is xterm or vt, set the title bar
                if c == b'x' || c == b'X' || c == b'v' || c == b'V' {
                    fd_puts(*fd_slot, 0, &format!("\033]0;{}\07", vm_get_name(vtty.c.vm).as_str()));
                }
                vtty.input_state = VttyInput::TelnetNext;
            }

            VttyInput::TelnetNext => {
                // ignore all chars until next IAC
                if c == Telnet::IAC {
                    vtty.input_state = VttyInput::Telnet;
                }
            }
        }
    }
}

// VTTY thread
extern "C" fn vtty_thread_main(_arg: *mut c_void) -> *mut c_void {
    unsafe {
        let mut rfds = zeroed::<libc::fd_set>();
        let mut fd_max;
        let mut tv = zeroed::<libc::timeval>();
        loop {
            // Build the FD set
            let list = vtty_list().lock().unwrap();
            libc::FD_ZERO(addr_of_mut!(rfds));
            fd_max = -1;
            for vtty in list.iter() {
                let vtty = NonNull::new(vtty.0).unwrap().as_mut();

                match vtty.type_ {
                    VttyType::Tcp => {
                        for &fd in vtty.fd_array.iter() {
                            if fd != -1 {
                                libc::FD_SET(fd, addr_of_mut!(rfds));
                                fd_max = max(fd, fd_max);
                            }
                        }

                        let fd_pool: Vec<_> = vtty.fd_pool.lock().unwrap().clone();
                        for fd in fd_pool {
                            if fd != -1 {
                                libc::FD_SET(fd, addr_of_mut!(rfds));
                                fd_max = max(fd, fd_max);
                            }
                        }
                    }

                    _ => {
                        for &fd in vtty.fd_array.iter() {
                            if fd != -1 {
                                libc::FD_SET(fd, addr_of_mut!(rfds));
                                fd_max = max(fd, fd_max);
                            }
                        }
                    }
                }
            }
            drop(list);

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
            let list = vtty_list().lock().unwrap();
            for vtty in list.iter() {
                let vtty = NonNull::new(vtty.0).unwrap().as_mut();

                match vtty.type_ {
                    VttyType::Tcp => {
                        // check incoming connection
                        for i in 0..vtty.fd_array.len() {
                            if vtty.fd_array[i] == -1 {
                                continue;
                            }

                            if !libc::FD_ISSET(vtty.fd_array[i], addr_of_mut!(rfds)) {
                                continue;
                            }

                            VttyTcp::conn_accept(vtty.into(), i as c_int);
                        }

                        // check established connection
                        let fd_pool: Vec<_> = vtty.fd_pool.lock().unwrap().clone();
                        for fd in fd_pool {
                            if fd != -1 && libc::FD_ISSET(fd, addr_of!(rfds)) {
                                let mut fd_slot = fd;
                                vtty_read_and_store(vtty, &mut fd_slot);
                                if fd_slot == -1 {
                                    vtty.fd_pool.lock().unwrap().retain(|rfd| *rfd != fd);
                                }
                            }
                        }
                    }

                    // Term, Serial
                    _ => {
                        for i in 0..vtty.fd_array.len() {
                            if vtty.fd_array[i] != -1 && libc::FD_ISSET(vtty.fd_array[i], addr_of!(rfds)) {
                                let mut fd_slot = vtty.fd_array[i];
                                vtty_read_and_store(vtty, &mut fd_slot);
                                if fd_slot == -1 {
                                    vtty.fd_array[i] = -1;
                                }
                                vtty.input_pending = true;
                            }
                        }
                    }
                }

                if vtty.input_pending {
                    if let Some(read_notifier) = vtty.c.read_notifier {
                        read_notifier(Vtty::to_c(addr_of_mut!(*vtty)));
                    }

                    vtty.input_pending = false;
                }

                // Flush any pending output
                if vtty.c.managed_flush == 0 {
                    vtty_flush(vtty.into());
                }
            }
            drop(list);
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
