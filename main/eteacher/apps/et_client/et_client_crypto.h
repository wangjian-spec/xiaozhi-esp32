#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

struct EtKeyPair {
    std::array<uint8_t, 32> private_key_seed{};
    std::array<uint8_t, 32> public_key{};
};

class EtClientCrypto {
public:
    static bool GenerateKeyPair(EtKeyPair& out, std::string& error);
    static bool PublicKeyFromSeed(const std::array<uint8_t, 32>& seed,
                                  std::array<uint8_t, 32>& public_key,
                                  std::string& error);
    static bool Sign(const std::array<uint8_t, 32>& seed,
                     std::span<const uint8_t> message,
                     std::array<uint8_t, 64>& signature,
                     std::string& error);

    static std::array<uint8_t, 32> Sha256(std::span<const uint8_t> data);
    static std::string HexEncode(std::span<const uint8_t> data);
    static std::string Base64Encode(std::span<const uint8_t> data);
    static bool Base64Decode32(const std::string& text,
                               std::array<uint8_t, 32>& out,
                               std::string& error);

    static std::string ComputeDeviceIdHex(const std::array<uint8_t, 32>& public_key);
};
