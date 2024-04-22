//! Stuff that interacts with C code.
#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(unused_imports)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]

pub(crate) mod prelude {
    //! What is needed to interact with C code.

    pub(crate) use crate::macros::opaque_struct;
    pub(crate) use sptr::Strict;
    pub(crate) use static_assertions::const_assert_eq;
    pub(crate) use std::ffi::*;
    pub(crate) use std::mem::size_of;
    pub(crate) use std::ptr::null_mut;
    pub(crate) use std::ptr::NonNull;
    pub(crate) type size_t = usize;
    pub(crate) type ssize_t = isize;
    pub(crate) use crate::c::dev_vtty::vtty_t;
    pub(crate) use crate::c::utils::fd_pool_t;
    pub(crate) use std::mem::zeroed;
    pub(crate) use std::ptr::addr_of;
    pub(crate) use std::ptr::addr_of_mut;

    /// cbindgen:no-export
    #[repr(C)]
    pub struct vm_instance(());
    pub type vm_instance_t = vm_instance;

    extern "C" {
        pub fn fd_pool_check_input(pool: *mut fd_pool_t, fds: *mut libc::fd_set, cbk: Option<unsafe extern "C" fn(fd_slot: *mut c_int, opt: *mut c_void)>, opt: *mut c_void) -> c_int;
        pub fn fd_pool_free(pool: *mut fd_pool_t);
        pub fn fd_pool_get_free_slot(pool: *mut fd_pool_t, slot: *mut *mut c_int) -> c_int;
        pub fn fd_pool_init(pool: *mut fd_pool_t);
        pub fn fd_pool_send(pool: *mut fd_pool_t, buffer: *mut c_void, len: size_t, flags: c_int) -> c_int;
        pub fn fd_pool_set_fds(pool: *mut fd_pool_t, fds: *mut libc::fd_set) -> c_int;
        pub fn remote_control(vtty: NonNull<vtty_t>, c: u8);
        pub fn vm_clear_irq(vm: *mut vm_instance_t, irq: c_uint);
        pub fn vm_error_msg(vm: *mut vm_instance_t, msg: *mut c_char);
        pub fn vm_get_instance_id(vm: *mut vm_instance_t) -> c_int;
        pub fn vm_get_name(vm: *mut vm_instance_t) -> *mut c_char;
        pub fn vm_get_status(vm: *mut vm_instance_t) -> c_int;
        pub fn vm_get_type(vm: *mut vm_instance_t) -> *mut c_char;
        pub fn vm_set_irq(vm: *mut vm_instance_t, irq: c_uint);
        pub fn vm_log_msg(vm: *mut vm_instance_t, module: *mut c_char, msg: *mut c_char);
    }

    // compatibility functions

    pub(crate) fn strdup(s: &str) -> *mut c_char {
        const_assert_eq!(size_of::<u8>(), size_of::<c_char>());
        let bytes = s.as_bytes();
        let n = bytes.len();
        let p: *mut c_char = unsafe { libc::malloc(n + 1).cast::<_>() };
        if !p.is_null() {
            unsafe { libc::memcpy(p.cast::<_>(), bytes.as_ptr().cast::<_>(), n) };
            unsafe { *p.add(n) = 0 };
        }
        p
    }

    pub(crate) fn errno() -> c_int {
        unsafe { *libc::__errno_location() }
    }

    pub(crate) fn strerror(errno: c_int) -> &'static str {
        let err = unsafe { libc::strerror(errno) };
        if err.is_null() {
            "(null)"
        } else {
            unsafe { CStr::from_ptr(err).to_str().unwrap() }
        }
    }

    pub(crate) fn perror(s: &str) {
        if s.is_empty() {
            eprintln!("{}", strerror(errno()));
        } else {
            eprintln!("{}: {}", s, strerror(errno()));
        }
    }

    pub(crate) fn vm_log(vm: *mut vm_instance_t, module: &str, msg: &str) {
        let mut module = module.to_owned();
        module.push('\0');
        let module = module.as_mut_str().as_mut_ptr().cast::<_>();
        let mut msg = msg.to_owned();
        msg.push('\0');
        let msg = msg.as_mut_str().as_mut_ptr().cast::<_>();
        unsafe { vm_log_msg(vm, module, msg) };
    }

    pub(crate) fn vm_error(vm: *mut vm_instance_t, msg: &str) {
        let mut msg = msg.to_owned();
        msg.push('\0');
        let msg = msg.as_mut_str().as_mut_ptr().cast::<_>();
        unsafe { vm_error_msg(vm, msg) };
    }

    pub(crate) fn fd_puts(fd: c_int, flags: c_int, msg: &str) {
        extern "C" {
            pub fn fd_puts(fd: c_int, flags: c_int, msg: *mut c_char) -> ssize_t;
        }
        let mut msg = msg.to_owned();
        msg.push('\0');
        let msg = msg.as_mut_str().as_mut_ptr().cast::<_>();
        unsafe { fd_puts(fd, flags, msg) };
    }

    pub trait PtrAsStr {
        fn as_str(&self) -> &str;
    }
    impl PtrAsStr for *const c_char {
        fn as_str(&self) -> &str {
            unsafe { CStr::from_ptr(*self).to_str().unwrap() }
        }
    }
    impl PtrAsStr for *mut c_char {
        fn as_str(&self) -> &str {
            unsafe { CStr::from_ptr(*self).to_str().unwrap() }
        }
    }
}

pub mod dev_lxt907a;
pub mod dev_ns16552;
pub mod dev_vtty;
pub mod dynamips;
pub mod utils;
pub mod vm;
