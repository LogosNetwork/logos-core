#ifndef LGS_INTERFACE_H
#define LGS_INTERFACE_H

#if __cplusplus
extern "C" {
#endif

typedef unsigned char * lgs_uint128; // 16byte array for public and private keys
typedef unsigned char * lgs_uint256; // 32byte array for public and private keys
typedef unsigned char * lgs_uint512; // 64byte array for signatures
typedef void * lgs_transaction;

// Convert amount bytes 'source' to a 39 byte not-null-terminated decimal string 'destination'
void lgs_uint128_to_dec(lgs_uint128 source, char *destination);
// Convert public/private key bytes 'source' to a 64 byte not-null-terminated hex string 'destination'
void lgs_uint256_to_string(lgs_uint256 source, char *destination);
// Convert public key bytes 'source' to a 65 byte non-null-terminated account string 'destination'
void lgs_uint256_to_address(lgs_uint256 source, char *destination);
// Convert public/private key bytes 'source' to a 128 byte not-null-terminated hex string 'destination'
void lgs_uint512_to_string(lgs_uint512 source, char *destination);

// Convert 39 byte decimal string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int lgs_uint128_from_dec(const char *source, lgs_uint128 destination);
// Convert 64 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int lgs_uint256_from_string(const char *source, lgs_uint256 destination);
// Convert 128 byte hex string 'source' to a byte array 'destination'
// Return 0 on success, nonzero on error
int lgs_uint512_from_string(const char *source, lgs_uint512 destination);

// Check if the null-terminated string 'account' is a valid lgs account number
// Return 0 on correct, nonzero on invalid
int lgs_valid_address(const char *account);

// Create a new random number in to 'destination'
void lgs_generate_random(lgs_uint256 destination);
// Retrieve the deterministic private key for 'seed' at 'index'
void lgs_seed_key(lgs_uint256 seed, int index, lgs_uint256);
// Derive the public key 'pub' from 'key'
void lgs_key_account(lgs_uint256 const key, lgs_uint256 pub);

// Sign 'transaction' using 'private_key' and write to 'signature'
char * lgs_sign_transaction(const char *transaction, const lgs_uint256 private_key);
// Generate work for 'transaction'
char * lgs_work_transaction(const char *transaction);

#if __cplusplus
} // extern "C"
#endif

#endif // LGS_INTERFACE_H
