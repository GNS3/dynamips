//! TODO doc

use crate::c::prelude::*;

// VM instance status (TODO enum)
/// VM is halted and no HW resources are used
pub const VM_STATUS_HALTED: c_int = 0;
/// Shutdown procedure engaged
pub const VM_STATUS_SHUTDOWN: c_int = 1;
/// VM is running
pub const VM_STATUS_RUNNING: c_int = 2;
/// VM is suspended
pub const VM_STATUS_SUSPENDED: c_int = 3;
