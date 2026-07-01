#include "ShirayukiMemory.hpp"
#include <cstring>

namespace Shirayuki {

Patch Patch::createWithBytes(uintptr_t address, const void *bytes, size_t len) {
    Patch p;
    if (!address || !bytes || !len)
        return p;

    p.m_address = address;
    p.m_patchBytes.assign(reinterpret_cast<const uint8_t *>(bytes),
                          reinterpret_cast<const uint8_t *>(bytes) + len);

    p.m_origBytes.resize(len);
    if (Memory::read(address, p.m_origBytes.data(), len) != Status::Success) {
        return Patch{};
    }

    return p;
}

Patch Patch::createWithHex(uintptr_t address, const std::string &hex) {
    auto bytes = Hex::toBytes(hex);
    if (bytes.empty())
        return Patch{};
    return createWithBytes(address, bytes.data(), bytes.size());
}

Patch Patch::createNop(uintptr_t address, size_t count) {
    // ARM64 NOP = 0xD503201F (little-endian: 1F 20 03 D5)
    static constexpr uint8_t kNopLE[4] = {0x1F, 0x20, 0x03, 0xD5};
    std::vector<uint8_t> nops(count * 4);
    for (size_t i = 0; i < count; i++) {
        memcpy(nops.data() + i * 4, kNopLE, 4);
    }
    return createWithBytes(address, nops.data(), nops.size());
}

bool Patch::apply() {
    if (!isValid())
        return false;
    if (m_applied)
        return true;

    if (Memory::write(m_address, m_patchBytes.data(), m_patchBytes.size()) == Status::Success) {
        m_applied = true;
        return true;
    }
    return false;
}

bool Patch::restore() {
    if (!isValid())
        return false;
    if (!m_applied)
        return true;

    if (Memory::write(m_address, m_origBytes.data(), m_origBytes.size()) == Status::Success) {
        m_applied = false;
        return true;
    }
    return false;
}

bool Patch::isApplied() const {
    if (!isValid())
        return false;
    std::vector<uint8_t> current(m_patchBytes.size());
    if (Memory::read(m_address, current.data(), current.size()) != Status::Success)
        return false;
    return current == m_patchBytes;
}

std::string Patch::currentHex() const {
    if (!isValid())
        return "";
    std::vector<uint8_t> current(m_patchBytes.size());
    Memory::read(m_address, current.data(), current.size());
    return Hex::fromBytes(current.data(), current.size());
}

std::string Patch::originalHex() const {
    return Hex::fromBytes(m_origBytes);
}

std::string Patch::patchHex() const {
    return Hex::fromBytes(m_patchBytes);
}

} // namespace Shirayuki
