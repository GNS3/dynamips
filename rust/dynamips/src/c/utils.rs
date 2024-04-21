//! TODO doc

use crate::c::prelude::*;

/// FD pool
pub const FD_POOL_MAX: usize = 16;

#[repr(C)]
#[derive(Debug)]
pub struct fd_pool {
    pub fd: [c_int; FD_POOL_MAX],
    /// must free this pointer if C deallocates
    pub next: *mut fd_pool,
}
pub type fd_pool_t = fd_pool;
impl fd_pool {
    pub fn new() -> Self {
        Self { fd: [0; FD_POOL_MAX], next: null_mut() }
    }
}
impl Default for fd_pool {
    fn default() -> Self {
        Self::new()
    }
}
impl Drop for fd_pool {
    fn drop(&mut self) {
        while !self.next.is_null() {
            let ptr = self.next;
            self.next = unsafe { (*ptr).next };
            unsafe { libc::free(ptr.cast::<_>()) };
        }
    }
}
