//! Stuff that interacts with C code.

pub(crate) mod prelude {
    #![allow(unused_imports)]
    //#![allow(dead_code)]
    //#![allow(non_camel_case_types)]

    pub(crate) use crate::macros::opaque_struct;
    pub(crate) use std::ffi::*;
    pub(crate) use std::ptr::null_mut;
    pub(crate) use std::ptr::NonNull;
}

pub mod dev_lxt907a;
