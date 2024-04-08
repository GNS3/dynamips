//! Macros
#![allow(unused_macros)]

/// Defines an opaque struct that has unknown data, is pointer aligned(?), and cannot change address.
macro_rules! opaque_struct {
    ($name:ident) => {
        pub struct $name {
            _data: [u8; 0],
            _marker: ::std::marker::PhantomData<(*mut u8, ::std::marker::PhantomPinned)>,
        }
    };
}

pub(crate) use opaque_struct;
