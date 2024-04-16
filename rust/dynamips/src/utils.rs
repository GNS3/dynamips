//! Random utilities.

use gcd::Gcd;
use std::time::Duration;

/// Unsigned number represented by a fraction.
#[derive(Default, Debug, Copy, Clone, PartialEq, Eq)]
pub struct Fraction(pub u32, pub u32);
impl Fraction {
    pub fn reduce(self) -> Self {
        let div = self.0.gcd(self.1);
        if div > 1 {
            Fraction(self.0 / div, self.1 / div)
        } else {
            self
        }
    }
    pub fn invert(self) -> Fraction {
        Fraction(self.1, self.0)
    }
    pub fn mul_div(self, mul: u32, div: u32) -> Fraction {
        Fraction(self.0 * mul, self.1 * div).reduce()
    }
    pub fn into_u32(self) -> u32 {
        assert!(self.0 % self.1 == 0);
        self.0 / self.1
    }
    pub fn into_f64(self) -> f64 {
        f64::from(self.0) / f64::from(self.1)
    }
}
impl std::ops::Div<u32> for Fraction {
    type Output = Fraction;
    fn div(self, div: u32) -> Fraction {
        #[allow(clippy::suspicious_arithmetic_impl)]
        Fraction(self.0, self.1 * div).reduce()
    }
}
impl std::ops::Mul<u32> for Fraction {
    type Output = Fraction;
    fn mul(self, mul: u32) -> Fraction {
        Fraction(self.0 * mul, self.1).reduce()
    }
}
impl From<Fraction> for Duration {
    fn from(x: Fraction) -> Self {
        Duration::from_secs(u64::from(x.0)) / x.1
    }
}

impl From<Duration> for Fraction {
    fn from(x: Duration) -> Self {
        assert!(x.as_secs() == 0);
        Self(x.subsec_nanos(), 1_000_000_000).reduce()
    }
}
