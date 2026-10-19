#include "Process.h"

#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

void flattenBlock(const std::vector<Instruction> &block,
                  std::vector<Instruction> &code,
                  std::vector<std::size_t> &forBodySizes)
{
    for (const Instruction &instruction : block)
    {
        const auto *loop = std::get_if<ForInstruction>(&instruction.payload);
        if (loop == nullptr)
        {
            code.push_back(instruction);
            forBodySizes.push_back(0);
            continue;
        }

        // Keep the FOR node for its repeat count but drop the nested body;
        // the body instructions follow it in the flat vector.
        code.push_back(Instruction{ForInstruction{loop->repeats, {}}});
        const std::size_t index = code.size() - 1;
        forBodySizes.push_back(0);

        const std::size_t bodyStart = code.size();
        flattenBlock(loop->body, code, forBodySizes);
        forBodySizes[index] = code.size() - bodyStart;
    }
}

std::uint16_t clampToUint16(std::int32_t value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max()))
    {
        return std::numeric_limits<std::uint16_t>::max();
    }
    return static_cast<std::uint16_t>(value);
}

} // namespace

Process::Process(std::uint32_t id, std::string name, Program program)
    : id_(id), name_(std::move(name))
{
    if (id_ == 0)
    {
        throw std::invalid_argument("Process id must be positive");
    }
    if (name_.empty())
    {
        throw std::invalid_argument("Process name must not be empty");
    }
    if (program.instructions.empty())
    {
        throw std::invalid_argument("Process program must not be empty");
    }

    flattenBlock(program.instructions, code_, forBodySizes_);
}

std::uint32_t Process::id() const noexcept
{
    return id_;
}

const std::string &Process::name() const noexcept
{
    return name_;
}

ProcessState Process::state() const noexcept
{
    return state_;
}

bool Process::isFinished() const noexcept
{
    return state_ == ProcessState::FINISHED;
}

bool Process::isSleeping() const noexcept
{
    return state_ == ProcessState::WAITING;
}

const std::vector<std::string> &Process::logs() const noexcept
{
    return logs_;
}

const std::map<std::string, std::uint16_t> &Process::variables() const noexcept
{
    return variables_;
}

std::size_t Process::currentLine() const noexcept
{
    return currentLine_;
}

std::size_t Process::linesOfCode() const noexcept
{
    return code_.size();
}

std::uint8_t Process::sleepTicksRemaining() const noexcept
{
    return sleepTicksRemaining_;
}

void Process::markReady()
{
    if (state_ == ProcessState::READY)
    {
        return;
    }
    if (state_ != ProcessState::RUNNING)
    {
        throw std::logic_error("markReady() requires a running process");
    }
    state_ = ProcessState::READY;
}

StepOutcome Process::step()
{
    if (state_ == ProcessState::FINISHED)
    {
        return StepOutcome::FINISHED;
    }
    if (state_ == ProcessState::WAITING)
    {
        return StepOutcome::SLEEPING;
    }
    if (pc_ >= code_.size())
    {
        state_ = ProcessState::FINISHED;
        return StepOutcome::FINISHED;
    }

    state_ = ProcessState::RUNNING;

    const Instruction &instruction = code_[pc_];
    const std::size_t executedLine = pc_ + 1;

    bool sleeping = false;
    std::uint8_t sleepTicks = 0;

    std::visit(
        [&](const auto &payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, PrintInstruction>)
            {
                std::string line = payload.text;
                if (payload.variable)
                {
                    line += std::to_string(variableSlot(*payload.variable));
                }
                logs_.push_back(std::move(line));
            }
            else if constexpr (std::is_same_v<T, DeclareInstruction>)
            {
                variableSlot(payload.name) = payload.value;
            }
            else if constexpr (std::is_same_v<T, AddInstruction>)
            {
                const std::int32_t result =
                    static_cast<std::int32_t>(resolve(payload.lhs)) +
                    static_cast<std::int32_t>(resolve(payload.rhs));
                variableSlot(payload.target) = clampToUint16(result);
            }
            else if constexpr (std::is_same_v<T, SubtractInstruction>)
            {
                const std::int32_t result =
                    static_cast<std::int32_t>(resolve(payload.lhs)) -
                    static_cast<std::int32_t>(resolve(payload.rhs));
                variableSlot(payload.target) = clampToUint16(result);
            }
            else if constexpr (std::is_same_v<T, SleepInstruction>)
            {
                sleeping = payload.ticks > 0;
                sleepTicks = payload.ticks;
            }
            else
            {
                const std::size_t bodySize = forBodySizes_[pc_];
                if (payload.repeats > 0 && bodySize > 0)
                {
                    frames_.push_back(
                        ForFrame{pc_ + 1,
                                 pc_ + 1 + bodySize,
                                 static_cast<std::uint16_t>(payload.repeats - 1)});
                }
                else
                {
                    // Nothing to execute in the body; advance() then moves
                    // past the FOR itself.
                    pc_ += bodySize;
                }
            }
        },
        instruction.payload);

    currentLine_ = executedLine;

    if (sleeping)
    {
        sleepTicksRemaining_ = sleepTicks;
        state_ = ProcessState::WAITING;
    }

    advance();

    if (state_ == ProcessState::WAITING)
    {
        return StepOutcome::SLEEPING;
    }
    if (pc_ >= code_.size())
    {
        state_ = ProcessState::FINISHED;
        return StepOutcome::FINISHED;
    }
    return StepOutcome::EXECUTED;
}

void Process::tickSleep()
{
    if (state_ != ProcessState::WAITING || sleepTicksRemaining_ == 0)
    {
        return;
    }

    --sleepTicksRemaining_;
    if (sleepTicksRemaining_ > 0)
    {
        return;
    }

    state_ = pc_ >= code_.size() ? ProcessState::FINISHED : ProcessState::READY;
}

std::uint16_t &Process::variableSlot(const std::string &name)
{
    return variables_[name];
}

std::uint16_t Process::resolve(const Operand &operand)
{
    return operand.isLiteral ? operand.value : variableSlot(operand.name);
}

void Process::advance()
{
    ++pc_;

    while (!frames_.empty() && pc_ >= frames_.back().bodyEnd)
    {
        ForFrame frame = frames_.back();
        frames_.pop_back();

        // An empty body has nothing to repeat, so fall through to the
        // instruction after the FOR.
        if (frame.repeatsRemaining > 0 && frame.bodyEnd > frame.bodyStart)
        {
            --frame.repeatsRemaining;
            pc_ = frame.bodyStart;
            frames_.push_back(frame);
            break;
        }
    }
}
