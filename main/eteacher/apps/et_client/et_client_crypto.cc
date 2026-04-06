#include "eteacher/apps/et_client/et_client_crypto.h"

#include <algorithm>

#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

extern "C" {
#include <monocypher-ed25519.h>
}

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

bool Base64DecodeFixed(const std::string& text, uint8_t* out, size_t out_size, std::string& error) {
    size_t olen = 0;
    const int ret = mbedtls_base64_decode(out,
                                          out_size,
                                          &olen,
                                          reinterpret_cast<const unsigned char*>(text.data()),
                                          text.size());
    if (ret != 0) {
        error = "base64 decode failed";
        return false;
    }
    if (olen != out_size) {
        error = "base64 decoded to unexpected length";
        return false;
    }
    return true;
}

}  // namespace

bool EtClientCrypto::GenerateKeyPair(EtKeyPair& out, std::string& error) {
    esp_fill_random(out.private_key_seed.data(), out.private_key_seed.size());
    return PublicKeyFromSeed(out.private_key_seed, out.public_key, error);
}

bool EtClientCrypto::PublicKeyFromSeed(const std::array<uint8_t, 32>& seed,
                                       std::array<uint8_t, 32>& public_key,
                                       std::string& error) {
    std::array<uint8_t, 64> secret_key{};
    std::array<uint8_t, 32> seed_copy = seed;
    crypto_ed25519_key_pair(secret_key.data(), public_key.data(), seed_copy.data());
    if (std::all_of(public_key.begin(), public_key.end(), [](uint8_t b) { return b == 0; })) {
        error = "derived public key is empty";
        return false;
    }
    return true;
}

bool EtClientCrypto::Sign(const std::array<uint8_t, 32>& seed,
                          std::span<const uint8_t> message,
                          std::array<uint8_t, 64>& signature,
                          std::string& error) {
    std::array<uint8_t, 64> secret_key{};
    std::array<uint8_t, 32> public_key{};
    std::array<uint8_t, 32> seed_copy = seed;
    crypto_ed25519_key_pair(secret_key.data(), public_key.data(), seed_copy.data());
    crypto_ed25519_sign(signature.data(), secret_key.data(), message.data(), message.size());
    if (std::all_of(signature.begin(), signature.end(), [](uint8_t b) { return b == 0; })) {
        error = "signature is empty";
        return false;
    }
    return true;
}

std::array<uint8_t, 32> EtClientCrypto::Sha256(std::span<const uint8_t> data) {
    std::array<uint8_t, 32> hash{};
    mbedtls_sha256(data.data(), data.size(), hash.data(), 0);
    return hash;
}

std::string EtClientCrypto::HexEncode(std::span<const uint8_t> data) {
    std::string result(data.size() * 2, '\0');
    for (size_t i = 0; i < data.size(); ++i) {
        result[i * 2] = kHexDigits[(data[i] >> 4) & 0x0F];
        result[i * 2 + 1] = kHexDigits[data[i] & 0x0F];
    }
    return result;
}

std::string EtClientCrypto::Base64Encode(std::span<const uint8_t> data) {
    size_t dlen = 0;
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &dlen, data.data(), data.size());
    std::string result(dlen, '\0');
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(result.data()),
                              result.size(),
                              &olen,
                              data.data(),
                              data.size()) != 0) {
        return {};
    }
    result.resize(olen);
    return result;
}

bool EtClientCrypto::Base64Decode32(const std::string& text,
                                    std::array<uint8_t, 32>& out,
                                    std::string& error) {
    return Base64DecodeFixed(text, out.data(), out.size(), error);
}

std::string EtClientCrypto::ComputeDeviceIdHex(const std::array<uint8_t, 32>& public_key) {
    return HexEncode(Sha256(public_key));
}
