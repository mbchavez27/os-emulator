#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Config;

/// Barebones process instructions from the CSOPESY spec, section 4.
enum class Op
{
    PRINT,
    DECLARE,
    ADD,
    SUBTRACT,
    SLEEP,
    FOR
};

/// An arithmetic argument: either a variable name or a uint16 literal.
struct Operand
{
    static Operand variable(std::string name);
    static Operand literal(uint16_t value);

    bool isLiteral = false;
    std::string name;   ///< Used when !isLiteral.
    uint16_t value = 0; ///< Used when isLiteral.
};

struct PrintInstruction
{
    std::string text;                    ///< Literal part of the message.
    std::optional<std::string> variable; ///< Appended when present.
};

struct DeclareInstruction
{
    std::string name;
    uint16_t value = 0;
};

struct AddInstruction
{
    std::string target;
    Operand lhs;
    Operand rhs;
};

struct SubtractInstruction
{
    std::string target;
    Operand lhs;
    Operand rhs;
};

struct SleepInstruction
{
    uint8_t ticks = 0;
};

struct Instruction; // Forward declaration for the recursive FOR body.

struct ForInstruction
{
    uint16_t repeats = 0;
    std::vector<Instruction> body;
};

/// One instruction; the active variant alternative is the operation.
struct Instruction
{
    using Payload = std::variant<PrintInstruction,
                                 DeclareInstruction,
                                 AddInstruction,
                                 SubtractInstruction,
                                 SleepInstruction,
                                 ForInstruction>;

    Payload payload;
};

/// A generated program plus its static size.
struct Program
{
    std::vector<Instruction> instructions;

    /// Same value used later for `Lines of code:`.
    std::size_t instructionCount() const;

    /// Counts every node recursively; FOR and its body are both counted.
    static std::size_t countInstructions(const std::vector<Instruction> &instructions);

    /// Indented, source-like rendering for verification and debugging.
    std::string toString() const;
};

/// Maps the active payload to its Op.
Op opOf(const Instruction &instruction);

/**
 * @brief Generates a deterministic random program.
 *
 * Identical config, process name, and seed produce an identical program.
 * Every instruction node counts toward [minIns, maxIns], including FOR bodies.
 *
 * @throws std::invalid_argument if minIns == 0 or minIns > maxIns.
 */
Program generateProgram(const Config &config,
                        const std::string &processName,
                        std::uint32_t seed);
