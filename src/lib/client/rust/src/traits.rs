// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Traits for use with the rust client library for cobalt.

/// `AsEventCode` is any type that can be converted into a `u32` for the purposes of reporting to
/// cobalt.
pub trait AsEventCode {
    fn as_event_code(&self) -> u32;
}

impl AsEventCode for u32 {
    fn as_event_code(&self) -> u32 {
        *self
    }
}

/// `AsEventCodes` is any type that can be converted into a `Vec<u32>` for the purposes of reporting
/// to cobalt.
pub trait AsEventCodes {
    /// Converts the source type into a `Vec<u32>` of event codes.
    fn as_event_codes(&self) -> Vec<u32>;
}

impl AsEventCodes for () {
    fn as_event_codes(&self) -> Vec<u32> {
        Vec::new()
    }
}

impl<A: AsEventCode> AsEventCodes for A {
    fn as_event_codes(&self) -> Vec<u32> {
        vec![self.as_event_code()]
    }
}

impl<A: AsEventCode> AsEventCodes for Vec<A> {
    fn as_event_codes(&self) -> Vec<u32> {
        self.iter().map(AsEventCode::as_event_code).collect()
    }
}

impl<A: AsEventCode> AsEventCodes for [A] {
    fn as_event_codes(&self) -> Vec<u32> {
        self.iter().map(AsEventCode::as_event_code).collect()
    }
}

macro_rules! array_impls {
    ($($N:expr)+) => {
        $(
            impl<A: AsEventCode> AsEventCodes for [A; $N] {
                fn as_event_codes(&self) -> Vec<u32> {
                    self[..].as_event_codes()
                }
            }
        )+
    }
}

array_impls! {0 1 2 3 4 5 6}

macro_rules! tuple_impls {
    ($(($($N:ident => $i:tt),*)),*) => {
        $(
            impl <$($N: AsEventCode),*> AsEventCodes for ($($N),*,) {
                fn as_event_codes(&self) -> Vec<u32> {
                    vec![$(
                        self.$i.as_event_code()
                    ),*]
                }
            }
        )*
    }
}

tuple_impls! {
    (A => 0),
    (A => 0, B => 1),
    (A => 0, B => 1, C => 2),
    (A => 0, B => 1, C => 2, D => 3),
    (A => 0, B => 1, C => 2, D => 3, E => 4),
    (A => 0, B => 1, C => 2, D => 3, E => 4, F => 5)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone, Copy)]
    enum EventCode1 {
        A = 1,
        B = 2,
    }

    impl AsEventCode for EventCode1 {
        fn as_event_code(&self) -> u32 {
            *self as u32
        }
    }

    #[derive(Clone, Copy)]
    enum EventCode2 {
        C = 3,
        D = 4,
    }

    impl AsEventCode for EventCode2 {
        fn as_event_code(&self) -> u32 {
            *self as u32
        }
    }

    #[test]
    fn test_as_event_codes() {
        assert_eq!(().as_event_codes(), vec![]);
        assert_eq!(([] as [u32; 0]).as_event_codes(), vec![]);
        assert_eq!(1.as_event_codes(), vec![1]);
        assert_eq!([1].as_event_codes(), vec![1]);
        assert_eq!(vec![1].as_event_codes(), vec![1]);
        assert_eq!([1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!(vec![1, 2].as_event_codes(), vec![1, 2]);
        assert_eq!([1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!(vec![1, 2, 3].as_event_codes(), vec![1, 2, 3]);
        assert_eq!([1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!(vec![1, 2, 3, 4].as_event_codes(), vec![1, 2, 3, 4]);
        assert_eq!([1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);
        assert_eq!(vec![1, 2, 3, 4, 5].as_event_codes(), vec![1, 2, 3, 4, 5]);

        assert_eq!((EventCode1::A, EventCode2::C).as_event_codes(), vec![1, 3]);
        assert_eq!(
            (0, EventCode1::B, 10, EventCode2::D).as_event_codes(),
            vec![0, 2, 10, 4]
        );
    }
}
