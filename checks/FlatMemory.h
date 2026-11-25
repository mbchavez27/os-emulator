#pragma once

#include "kernel/ProcessMemory.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

/**
 * @brief Unpaged ProcessMemory for checks that step a Process directly.
 *
 * Little-endian like MemoryManager, zero-filled, and bounds-checked.
 */
class FlatMemory final : public ProcessMemory
{
public:
    explicit FlatMemory(std::size_t bytes) : bytes_(bytes, 0) {}

    std::uint16_t read16(std::uint64_t address) override
    {
        checkRange(address);
        return static_cast<std::uint16_t>(bytes_[address] | (bytes_[address + 1] << 8));
    }

    void write16(std::uint64_t address, std::uint16_t value) override
    {
        checkRange(address);
        bytes_[address] = static_cast<std::uint8_t>(value & 0xFF);
        bytes_[address + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    /** @return the raw bytes, to inspect where values landed */
    const std::vector<std::uint8_t> &bytes() const noexcept { return bytes_; }

private:
    void checkRange(std::uint64_t address) const
    {
        if (bytes_.size() < 2 || address > bytes_.size() - 2)
        {
            throw std::out_of_range("FlatMemory address out of range");
        }
    }

    std::vector<std::uint8_t> bytes_;
};
