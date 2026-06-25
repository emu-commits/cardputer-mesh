// PKC (Curve25519) crypto for Meshtastic per-node DMs — ported from Plai's
// mbedtls implementation, byte-compatible with Meshtastic's encryptCurve25519():
//   shared = X25519(our_priv, peer_pub); key = SHA256(shared) (AES-256);
//   nonce(13) = packetId_lo(4) ‖ extraNonce(4) ‖ fromNode(4) ‖ 0;
//   AES-256-CCM, 8-byte tag; payload = ciphertext ‖ tag(8) ‖ extraNonce(4).
#pragma once
#include <cstdint>
#include <cstring>
#include "esp_random.h"
#include "esp_log.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ccm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

namespace pkc {

constexpr size_t OVERHEAD = 12;   // 8-byte tag + 4-byte extra nonce

inline int rng_cb(void*, unsigned char* buf, size_t len) { esp_fill_random(buf, len); return 0; }

// shared = X25519(private, peer_public); written little-endian (native X25519).
inline bool shared_secret(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]) {
    mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi d, z;
    mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&d); mbedtls_mpi_init(&z);
    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
        mbedtls_mpi_read_binary_le(&d, priv, 32) == 0 &&
        mbedtls_mpi_read_binary_le(&Q.MBEDTLS_PRIVATE(X), peer_pub, 32) == 0 &&
        mbedtls_mpi_lset(&Q.MBEDTLS_PRIVATE(Z), 1) == 0 &&
        mbedtls_ecdh_compute_shared(&grp, &z, &Q, &d, rng_cb, nullptr) == 0) {
        std::memset(out, 0, 32);
        ok = (mbedtls_mpi_write_binary_le(&z, out, 32) == 0);
    }
    mbedtls_mpi_free(&z); mbedtls_mpi_free(&d); mbedtls_ecp_point_free(&Q); mbedtls_ecp_group_free(&grp);
    return ok;
}

// Generate an X25519 keypair (private + public, both little-endian 32 bytes).
inline bool generate_keypair(uint8_t priv[32], uint8_t pub[32]) {
    mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg; mbedtls_ecp_group grp; mbedtls_mpi d; mbedtls_ecp_point Q;
    mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg); mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d); mbedtls_ecp_point_init(&Q);
    const char* pers = "meshtastic_x25519";
    bool ok = false;
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char*)pers, std::strlen(pers)) == 0 &&
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
        mbedtls_ecdh_gen_public(&grp, &d, &Q, mbedtls_ctr_drbg_random, &drbg) == 0) {
        mbedtls_mpi inv; mbedtls_mpi_init(&inv);
        if (mbedtls_mpi_cmp_int(&Q.MBEDTLS_PRIVATE(Z), 1) != 0) {       // normalize to affine X
            mbedtls_mpi_inv_mod(&inv, &Q.MBEDTLS_PRIVATE(Z), &grp.P);
            mbedtls_mpi_mul_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &inv);
            mbedtls_mpi_mod_mpi(&Q.MBEDTLS_PRIVATE(X), &Q.MBEDTLS_PRIVATE(X), &grp.P);
        }
        mbedtls_mpi_free(&inv);
        std::memset(priv, 0, 32); std::memset(pub, 0, 32);
        ok = mbedtls_mpi_write_binary_le(&d, priv, 32) == 0 &&
             mbedtls_mpi_write_binary_le(&Q.MBEDTLS_PRIVATE(X), pub, 32) == 0;
    }
    mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
    return ok;
}

// Encrypt plaintext -> out (len plaintext_len + OVERHEAD). Returns out length, or 0.
inline size_t encrypt(const uint8_t priv[32], const uint8_t peer_pub[32], uint32_t from_node,
                      uint32_t packet_id, const uint8_t* pt, size_t ptlen, uint8_t* out) {
    uint8_t key[32];
    if (!shared_secret(priv, peer_pub, key)) return 0;
    mbedtls_sha256(key, 32, key, 0);
    uint32_t extra = esp_random();
    uint8_t nonce[13] = {};
    uint64_t id64 = packet_id;
    std::memcpy(nonce, &id64, 8); std::memcpy(nonce + 4, &extra, 4); std::memcpy(nonce + 8, &from_node, 4);
    mbedtls_ccm_context ccm; mbedtls_ccm_init(&ccm);
    size_t outlen = 0;
    if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
        mbedtls_ccm_encrypt_and_tag(&ccm, ptlen, nonce, 13, nullptr, 0, pt, out, out + ptlen, 8) == 0) {
        std::memcpy(out + ptlen + 8, &extra, 4);
        outlen = ptlen + OVERHEAD;
    }
    mbedtls_ccm_free(&ccm);
    return outlen;
}

// Decrypt a PKC payload (in = ciphertext ‖ tag ‖ extraNonce). from_node = sender.
// Returns plaintext length, or 0 on auth failure.
inline size_t decrypt(const uint8_t priv[32], const uint8_t peer_pub[32], uint32_t from_node,
                      uint32_t packet_id, const uint8_t* in, size_t inlen, uint8_t* out) {
    if (inlen <= OVERHEAD) return 0;
    uint8_t key[32];
    if (!shared_secret(priv, peer_pub, key)) return 0;
    mbedtls_sha256(key, 32, key, 0);
    const uint8_t* auth = in + inlen - OVERHEAD;     // 8-byte tag ‖ 4-byte extra
    uint32_t extra = 0; std::memcpy(&extra, auth + 8, 4);
    uint8_t nonce[13] = {};
    uint64_t id64 = packet_id;
    std::memcpy(nonce, &id64, 8); std::memcpy(nonce + 4, &extra, 4); std::memcpy(nonce + 8, &from_node, 4);
    size_t clen = inlen - OVERHEAD;
    mbedtls_ccm_context ccm; mbedtls_ccm_init(&ccm);
    size_t outlen = 0;
    if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
        mbedtls_ccm_auth_decrypt(&ccm, clen, nonce, 13, nullptr, 0, in, out, auth, 8) == 0)
        outlen = clen;
    mbedtls_ccm_free(&ccm);
    return outlen;
}

} // namespace pkc
