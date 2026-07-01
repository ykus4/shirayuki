#include "ShirayukiMemory.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Shirayuki {

std::vector<uint8_t> Hex::toBytes(const std::string &hex) {
    std::vector<uint8_t> bytes;

    if (hex.find(' ') != std::string::npos || hex.find('?') != std::string::npos) {
        std::istringstream ss(hex);
        std::string token;
        while (ss >> token) {
            if (token == "?" || token == "??") {
                bytes.push_back(0);
                continue;
            }
            unsigned int val;
            std::istringstream hexSS(token);
            if (!(hexSS >> std::hex >> val) || val > 0xFF)
                return {};
            bytes.push_back(static_cast<uint8_t>(val));
        }
        return bytes;
    }

    if (hex.length() >= 2) {
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            unsigned int val;
            std::istringstream hexSS(hex.substr(i, 2));
            if (!(hexSS >> std::hex >> val))
                return {};
            bytes.push_back(static_cast<uint8_t>(val));
        }
    }

    return bytes;
}

std::string Hex::fromBytes(const void *data, size_t len) {
    std::ostringstream ss;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; i++) {
        if (i > 0)
            ss << ' ';
        ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
           << static_cast<unsigned>(bytes[i]);
    }
    return ss.str();
}

std::string Hex::fromBytes(const std::vector<uint8_t> &data) {
    return fromBytes(data.data(), data.size());
}

std::string Hex::dump(uintptr_t address, size_t len, size_t bytesPerLine) {
    std::ostringstream ss;
    std::vector<uint8_t> buf(len);

    if (Memory::read(address, buf.data(), len) != Status::Success) {
        return "<read failed>";
    }

    for (size_t i = 0; i < len; i += bytesPerLine) {
        ss << std::setfill('0') << std::setw(16) << std::hex << (address + i) << "  ";

        size_t lineLen = std::min(bytesPerLine, len - i);
        for (size_t j = 0; j < bytesPerLine; j++) {
            if (j < lineLen) {
                ss << std::setfill('0') << std::setw(2) << std::hex
                   << static_cast<unsigned>(buf[i + j]) << ' ';
            } else {
                ss << "   ";
            }
            if (j == 7)
                ss << ' ';
        }

        ss << " |";
        for (size_t j = 0; j < lineLen; j++) {
            char c = static_cast<char>(buf[i + j]);
            ss << (isprint(c) ? c : '.');
        }
        ss << "|\n";
    }

    return ss.str();
}

bool Hex::isValid(const std::string &hex) {
    if (hex.empty())
        return false;
    for (char c : hex) {
        if (c == ' ' || c == '?')
            continue;
        if (!isxdigit(c))
            return false;
    }
    return true;
}

} // namespace Shirayuki
