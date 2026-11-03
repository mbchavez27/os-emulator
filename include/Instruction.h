#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Config;

/** @brief The barebones process instructions from the CSOPESY spec. */
enum class Op
{
    PRINT,
    DECLARE,
    ADD,
    SUBTRACT,
    SLEEP,
    FOR
};

/**
 * @brief An arithmetic argument: a variable name or a uint16 literal, never both.
 *
 * Built only through variable() and literal(), so a contradictory operand
 * cannot exist.
 */
class Operand
{
public:
    /**
     * @brief Creates an operand that reads a variable.
     *
     * @param name the variable name
     * @return the operand
     * @throws std::invalid_argument if name is empty
     */
    static Operand variable(std::string name);

    /**
     * @brief Creates an operand with a fixed value.
     *
     * @param value the uint16 literal
     * @return the operand
     */
    static Operand literal(uint16_t value);

    /** @return true if this operand is a literal rather than a variable */
    bool isLiteral() const noexcept;

    /**
     * @return the variable name
     * @throws std::bad_variant_access if this is a literal
     */
    const std::string &variableName() const;

    /**
     * @return the literal value
     * @throws std::bad_variant_access if this is a variable
     */
    uint16_t literalValue() const;

    /** @return the source form: the variable name or the number */
    std::string toString() const;

private:
    explicit Operand(std::variant<std::string, uint16_t> value);

    std::variant<std::string, uint16_t> value_; /**< Exactly one of name or literal. */
};

/** @brief `PRINT (msg)` or `PRINT ("msg" + var)`. */
struct PrintInstruction
{
    std::string text;                    /**< Literal part of the message. */
    std::optional<std::string> variable; /**< Variable whose value is appended, if any. */
};

/** @brief `DECLARE (var, value)`. */
struct DeclareInstruction
{
    std::string name;   /**< Variable to declare. */
    uint16_t value = 0; /**< Initial value. */
};

/** @brief `ADD (target, lhs, rhs)`: target = lhs + rhs, clamped. */
struct AddInstruction
{
    std::string target;
    Operand lhs;
    Operand rhs;
};

/** @brief `SUBTRACT (target, lhs, rhs)`: target = lhs - rhs, clamped. */
struct SubtractInstruction
{
    std::string target;
    Operand lhs;
    Operand rhs;
};

/** @brief `SLEEP (ticks)`: gives up the CPU for the given number of ticks. */
struct SleepInstruction
{
    uint8_t ticks = 0;
};

struct Instruction; // Forward declaration for the recursive FOR body.

/** @brief `FOR ([body], repeats)`; bodies may nest up to 3 levels. */
struct ForInstruction
{
    uint16_t repeats = 0;          /**< Times the body runs. */
    std::vector<Instruction> body; /**< Instructions repeated each pass. */
};

/** @brief One instruction; the active variant alternative is the operation. */
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

/** @brief A generated program. */
struct Program
{
    std::vector<Instruction> instructions; /**< Top-level instructions, FOR bodies nested. */

    /** @return the total instruction count, the value shown as `Lines of code:` */
    std::size_t instructionCount() const;

    /**
     * @brief Counts instructions recursively; a FOR and every instruction in its body each count once.
     *
     * @param instructions the instructions to count
     * @return the total count
     */
    static std::size_t countInstructions(const std::vector<Instruction> &instructions);

    /** @return an indented, source-like rendering for verification and debugging */
    std::string toString() const;
};

/**
 * @brief Identifies an instruction's operation.
 *
 * @param instruction the instruction to inspect
 * @return the Op matching its active payload
 */
Op opOf(const Instruction &instruction);

/**
 * @brief Largest program generateProgram() will build.
 *
 * config.txt still accepts the spec's [1, 2^32] instruction range, but each
 * instruction is held in memory (~360 MB per million), so a larger program
 * would exhaust RAM and freeze the scheduler.
 */
constexpr std::uint64_t kMaxProgramInstructions = 1'000'000;

/**
 * @brief Generates a deterministic random program.
 *
 * Identical config, process name, and seed produce an identical program.
 * Every instruction counts toward [minIns, maxIns], including FOR bodies.
 *
 * @param config supplies minIns and maxIns
 * @param processName used in the default PRINT message
 * @param seed random seed; the process ID in practice
 * @return the generated program
 * @throws std::invalid_argument if minIns is 0 or greater than maxIns
 * @throws std::length_error if the picked size exceeds kMaxProgramInstructions
 */
Program generateProgram(const Config &config,
                        const std::string &processName,
                        std::uint32_t seed);
