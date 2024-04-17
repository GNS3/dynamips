//! C interface for [`crate::uart::ns16552::Ns16552`].

use crate::c::prelude::*;
use crate::uart::ns16552::Ns16552;
use std::sync::Mutex;

/// DUART that can be used by multiple threads.
pub struct MutexNs16552(Mutex<Ns16552>);
impl MutexNs16552 {
    fn new_from_c(vm: *mut vm_instance_t, irq: c_uint, vtty0: *mut vtty_t, vtty1: *mut vtty_t) -> Self {
        Self(Mutex::new(Ns16552::new_from_c(vm, irq, vtty0, vtty1)))
    }
}

/// Allocate a new DUART.
#[no_mangle]
pub extern "C" fn ns16552_new(vm: *mut vm_instance_t, irq: c_uint, vtty0: *mut vtty_t, vtty1: *mut vtty_t) -> *mut MutexNs16552 {
    let duart = Box::new(MutexNs16552::new_from_c(vm, irq, vtty0, vtty1));
    Box::into_raw(duart) // memory managed by C
}

/// Deallocate a DUART.
#[no_mangle]
pub extern "C" fn ns16552_drop(duart: *mut MutexNs16552) {
    let duart = NonNull::new(duart).unwrap();
    let _ = unsafe { Box::from_raw(duart.as_ptr()) }; // memory managed by rust
}

/// Read from a register.
#[no_mangle]
pub extern "C" fn ns16552_read_access(duart: *mut MutexNs16552, idx: u32) -> u8 {
    let mut duart = NonNull::new(duart).unwrap();
    let mut duart = unsafe { duart.as_mut().0.lock().unwrap() };
    duart.read_access(idx as usize).unwrap_or(0)
}

/// Write to a register.
#[no_mangle]
pub extern "C" fn ns16552_write_access(duart: *mut MutexNs16552, idx: u32, value: u8) {
    let mut duart = NonNull::new(duart).unwrap();
    let mut duart = unsafe { duart.as_mut().0.lock().unwrap() };
    let _ = duart.write_access(idx as usize, value);
}

/// Trigger all the chip logic.
#[no_mangle]
pub extern "C" fn ns16552_tick(duart: *mut MutexNs16552) {
    let mut duart = NonNull::new(duart).unwrap();
    let mut duart = unsafe { duart.as_mut().0.lock().unwrap() };
    duart.tick();
}
