#pragma once

#include <cstdint>

/**
 * @brief A process's view of its own memory: uint16 values at byte addresses.
 *
 * Process executes instructions against this interface, so it stays unaware
 * of frames, paging, and the scheduler. The scheduler backs it with the
 * MemoryManager, where every access can page-fault; checks can back it with
 * a plain byte array.
 */
class ProcessMemory
{
public:
    virtual ~ProcessMemory() = default;

    /**
     * @brief Reads the uint16 stored at a byte address.
     *
     * @param address byte offset in the process's memory
     * @return the stored value; memory never written reads as 0
     * @throws std::out_of_range if the 2 bytes are outside the process's memory
     */
    virtual std::uint16_t read16(std::uint64_t address) = 0;

    /**
     * @brief Stores a uint16 at a byte address.
     *
     * @param address byte offset in the process's memory
     * @param value the value to store
     * @throws std::out_of_range if the 2 bytes are outside the process's memory
     */
    virtual void write16(std::uint64_t address, std::uint16_t value) = 0;

protected:
    ProcessMemory() = default;
    ProcessMemory(const ProcessMemory &) = default;
    ProcessMemory &operator=(const ProcessMemory &) = default;
};
