#include "rsa_signature.hpp"
#include <stdexcept>

/**
 * @brief Returns a unique toy RSA key pair for each node.
 *
 * Each pair is derived from distinct small primes (p, q). The keys are
 * toy-level (fit in uint64_t), but each node has a genuinely different
 * identity so signature verification actually proves sender identity.
 *
 * All 6 triplets have been verified: sign(data, d, n) then
 * verify(data, e, n) returns true for multiple test strings.
 *
 * @param nodeID  The node's ID (0-5).
 * @return Keys   The {n, e, d} triplet for that node.
 */
Keys generateKeys(int nodeID) {
    static const struct { uint64_t n, e, d; } pool[] = {
        // Node 0: p=100003, q=100103
        { 10010600309ULL, 65537ULL, 7196851037ULL },
        // Node 1: p=100207, q=100313
        { 10052064791ULL, 65537ULL, 791731745ULL },
        // Node 2: p=100417, q=100517
        { 10093615589ULL, 65537ULL, 4482796289ULL },
        // Node 3: p=100621, q=100733
        { 10135855193ULL, 65537ULL, 1597745393ULL },
        // Node 4: p=100847, q=100957
        { 10181210579ULL, 65537ULL, 4683571625ULL },
        // Node 5: p=101063, q=101173
        { 10224846899ULL, 65537ULL, 1585875353ULL },
    };

    if (nodeID < 0 || nodeID >= 6) {
        throw std::out_of_range(
            "generateKeys: nodeID must be 0-5, got " + std::to_string(nodeID));
    }

    Keys k;
    k.n = pool[nodeID].n;
    k.e = pool[nodeID].e;
    k.d = pool[nodeID].d;
    return k;
}

/**
 * Signs the data.
 * 1. Hashes the data (mod n).
 * 2. Encrypts the hash with the PRIVATE key (d).
 */
uint64_t signData(const std::string& data, const Keys& keys) {
    // 1. Get the hash AND apply the modulus
    uint64_t hash = simpleHash(data) % keys.n; 
    
    // 2. SIGN WITH THE PRIVATE KEY (d)
    return modexp(hash, keys.d, keys.n); // ,private,public
}

/**
 * Verifies the signature.
 * 1. Re-hashes the original data (mod n).
 * 2. Decrypts the signature with the PUBLIC key (e).
 * 3. Compares the two hashes.
 */
bool verifySignature(const std::string& data, uint64_t signature, const Keys& keys) {
    // 1. Re-hash the data to get what we expect
    uint64_t expectedHash = simpleHash(data) % keys.n;
    
    // 2. VERIFY WITH THE PUBLIC KEY (e)
    uint64_t decryptedHash = modexp(signature, keys.e, keys.n); 
    
    // 3. This will now correctly return true or false.
    return expectedHash == decryptedHash;
}