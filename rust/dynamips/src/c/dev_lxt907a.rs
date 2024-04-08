//! C interface for [`crate::phy::lxt970a::Lxt970A`].

use crate::c::prelude::*;
use crate::phy::lxt970a::Lxt970A;

/// Allocate a new PHY.
#[no_mangle]
pub extern "C" fn lxt970a_new() -> *mut Lxt970A {
    let phy = Box::new(Lxt970A::new());
    Box::into_raw(phy) // memory managed by C
}

/// Deallocate a PHY.
#[no_mangle]
pub extern "C" fn lxt970a_drop(phy: *mut Lxt970A) {
    let phy = NonNull::new(phy).unwrap();
    let _ = unsafe { Box::from_raw(phy.as_ptr()) }; // memory managed by rust
}

/// Notify PHY if a nio is available.
#[no_mangle]
pub extern "C" fn lxt970a_set_has_nio(phy: *mut Lxt970A, has_nio: bool) {
    let mut phy = NonNull::new(phy).unwrap();
    let phy = unsafe { phy.as_mut() };
    phy.has_nio = has_nio;
}

/// Check if the link is up.
#[no_mangle]
pub extern "C" fn lxt970a_link_is_up(phy: *mut Lxt970A) -> bool {
    let mut phy = NonNull::new(phy).unwrap();
    let phy = unsafe { phy.as_mut() };
    phy.link_is_up()
}

/// MII register read access.
#[no_mangle]
pub extern "C" fn lxt970a_mii_read_access(phy: *mut Lxt970A, reg: c_uint) -> u16 {
    let mut phy = NonNull::new(phy).unwrap();
    let phy = unsafe { phy.as_mut() };
    let reg = usize::try_from(reg).unwrap();
    phy.mii_read_access(reg)
}

/// MII register write access.
#[no_mangle]
pub extern "C" fn lxt970a_mii_write_access(phy: *mut Lxt970A, reg: c_uint, value: u16) {
    let mut phy = NonNull::new(phy).unwrap();
    let phy = unsafe { phy.as_mut() };
    let reg = usize::try_from(reg).unwrap();
    phy.mii_write_access(reg, value)
}
