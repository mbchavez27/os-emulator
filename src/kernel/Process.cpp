#include "kernel/EmulatorProcess.h"

#include <algorithm>
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

std::chrono::system_clock::time_point Process::startedAt() const noexcept
{
    return startedAt_;
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

const std::vector<LogStamp> &Process::logStamps() const noexcept
{
    return logStamps_;
}

const std::map<std::string, std::uint16_t> &Process::variables() const noexcept
{
    return variables_;
}

std::optional<std::uint64_t> Process::symbolOffset(const std::string &name) const
{
    const auto symbol = symbols_.find(name);
    if (symbol == symbols_.end())
    {
        return std::nullopt;
    }
    return symbol->second;
}

std::size_t Process::currentLine() const noexcept
{
    return currentLine_;
}

std::size_t Process::linesOfCode() const noexcept
{
    return code_.size();
}

std::size_t Process::nextInstructionIndex() const noexcept
{
    return isFinished() ? code_.size() : std::min(pc_, code_.size());
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

void Process::markRunning()
{
    if (state_ == ProcessState::RUNNING)
    {
        return;
    }
    if (state_ != ProcessState::READY)
    {
        throw std::logic_error("markRunning() requires a ready process");
    }
    state_ = ProcessState::RUNNING;
}

StepOutcome Process::step(ProcessMemory &memory, std::optional<std::size_t> core)
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
        finish();
        return StepOutcome::FINISHED;
    }

    state_ = ProcessState::RUNNING;

    const Instruction &instruction = code_[pc_];
    const std::size_t executedLine = pc_ + 1;

    std::visit([&](const auto &payload) { execute(payload, memory, core); }, instruction.payload);
    currentLine_ = executedLine;

    advance();

    if (state_ == ProcessState::WAITING)
    {
        return StepOutcome::SLEEPING;
    }
    if (pc_ >= code_.size())
    {
        finish();
        return StepOutcome::FINISHED;
    }
    return StepOutcome::EXECUTED;
}

void Process::execute(const PrintInstruction &print, ProcessMemory &memory, std::optional<std::size_t> core)
{
    std::string line = print.text;
    if (print.variable)
    {
        line += std::to_string(loadVariable(memory, *print.variable));
    }
    logs_.push_back(std::move(line));
    logStamps_.push_back(LogStamp{std::chrono::system_clock::now(), core});
}

void Process::execute(const DeclareInstruction &declare, ProcessMemory &memory, std::optional<std::size_t>)
{
    storeVariable(memory, declare.name, declare.value);
}

void Process::execute(const AddInstruction &add, ProcessMemory &memory, std::optional<std::size_t>)
{
    const std::int32_t sum = static_cast<std::int32_t>(resolve(memory, add.lhs)) +
                             static_cast<std::int32_t>(resolve(memory, add.rhs));
    storeVariable(memory, add.target, clampToUint16(sum));
}

void Process::execute(const SubtractInstruction &subtract, ProcessMemory &memory, std::optional<std::size_t>)
{
    const std::int32_t difference = static_cast<std::int32_t>(resolve(memory, subtract.lhs)) -
                                    static_cast<std::int32_t>(resolve(memory, subtract.rhs));
    storeVariable(memory, subtract.target, clampToUint16(difference));
}

void Process::execute(const SleepInstruction &sleep, ProcessMemory &, std::optional<std::size_t>)
{
    if (sleep.ticks > 0)
    {
        sleepTicksRemaining_ = sleep.ticks;
        state_ = ProcessState::WAITING;
    }
}

void Process::execute(const ForInstruction &loop, ProcessMemory &, std::optional<std::size_t>)
{
    const std::size_t bodySize = forBodySizes_[pc_];
    if (loop.repeats > 0 && bodySize > 0)
    {
        frames_.push_back(ForFrame{pc_ + 1, pc_ + 1 + bodySize,
                                   static_cast<std::uint16_t>(loop.repeats - 1)});
    }
    else
    {
        // Nothing to execute in the body; advance() then moves past the FOR itself.
        pc_ += bodySize;
    }
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

    if (pc_ >= code_.size())
    {
        finish();
    }
    else
    {
        state_ = ProcessState::READY;
    }
}

void Process::finish()
{
    symbols_.clear();
    variables_.clear();
    state_ = ProcessState::FINISHED;
}

std::optional<std::uint64_t> Process::declare(ProcessMemory &memory, const std::string &name)
{
    if (const auto offset = symbolOffset(name))
    {
        return offset;
    }
    if (symbols_.size() >= kMaxVariables)
    {
        return std::nullopt;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(symbols_.size()) * 2;
    memory.write16(offset, 0);
    symbols_.emplace(name, offset);
    variables_[name] = 0;
    return offset;
}

std::uint16_t Process::loadVariable(ProcessMemory &memory, const std::string &name)
{
    const auto offset = declare(memory, name);
    if (!offset)
    {
        return 0;
    }
    const std::uint16_t value = memory.read16(*offset);
    variables_[name] = value;
    return value;
}

void Process::storeVariable(ProcessMemory &memory, const std::string &name, std::uint16_t value)
{
    if (const auto offset = declare(memory, name))
    {
        memory.write16(*offset, value);
        variables_[name] = value;
    }
}

std::uint16_t Process::resolve(ProcessMemory &memory, const Operand &operand)
{
    return operand.isLiteral() ? operand.literalValue() : loadVariable(memory, operand.variableName());
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
