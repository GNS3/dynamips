//! Crate rust-dynamips
#![allow(clippy::unusual_byte_groupings)]

pub mod c;
pub mod macros;
pub mod phy;
pub mod uart;
pub mod utils;

use libc_alloc::LibcAlloc;

/// All allocations use libc.
/// This means C and rust allocate/deallocate compatible memory, but C will not execute drop code.
#[global_allocator]
static ALLOCATOR: LibcAlloc = LibcAlloc;
