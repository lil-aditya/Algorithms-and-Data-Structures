#pragma once
#include <cstdint>

/// Modular exponentiation: (base^exp) % mod
uint64_t modexp(uint64_t base, uint64_t exp, uint64_t mod); 

/// Extended GCD: returns gcd and sets x,y so that a*x + b*y = gcd
uint64_t egcd(uint64_t a, uint64_t b, int64_t &x, int64_t &y);

/// Modular inverse of a mod m (returns -1 if inverse doesn't exist)
int64_t modInverse(int64_t a, int64_t m);

/// Safe modular multiplication: (a * b) % mod without overflow
uint64_t modMul(uint64_t a, uint64_t b, uint64_t mod);
