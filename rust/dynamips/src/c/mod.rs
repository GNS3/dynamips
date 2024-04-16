//! Stuff that interacts with C code.
#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(unused_imports)]

pub(crate) mod prelude {
    //! What is needed to interact with C code.

    pub(crate) use crate::macros::opaque_struct;
    pub(crate) use std::ffi::*;
    pub(crate) use std::ptr::null_mut;
    pub(crate) use std::ptr::NonNull;
    pub(crate) type size_t = usize;

    /// cbindgen:no-export
    #[repr(C)]
    pub struct vm_instance(());
    pub type vm_instance_t = vm_instance;

    /// cbindgen:no-export
    #[repr(C)]
    pub struct virtual_tty(());
    pub type vtty_t = virtual_tty;

    extern "C" {
        pub fn vm_clear_irq(vm: *mut vm_instance_t, irq: c_uint);
        pub fn vm_set_irq(vm: *mut vm_instance_t, irq: c_uint);
        pub fn vtty_flush(vtty: *mut vtty_t);
        pub fn vtty_get_char(vtty: *mut vtty_t) -> c_int;
        pub fn vtty_is_char_avail(vtty: *mut vtty_t) -> c_int;
        pub fn vtty_put_char(vtty: *mut vtty_t, ch: c_char);
    }
}

pub mod dev_lxt907a;
pub mod dev_ns16552;
