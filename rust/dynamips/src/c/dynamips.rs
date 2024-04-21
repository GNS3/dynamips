//! TODO doc

use crate::c::prelude::*;

/// Binding address (NULL means any or 0.0.0.0)
#[no_mangle]
pub static mut binding_addr: *mut c_char = null_mut();

/// Console (vtty tcp) binding address (NULL means any or 0.0.0.0)
#[no_mangle]
pub static mut console_binding_addr: *mut c_char = null_mut();
