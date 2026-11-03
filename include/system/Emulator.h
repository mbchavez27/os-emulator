#pragma once

#include "config/Config.h"
#include "kernel/Scheduler.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

/**
 * @brief Application state: liveness, the loaded configuration, and the scheduler.
 *
 * CommandHandler drives it; the console loops observe it. A Config only exists
 * after a successful initialize(), so no command can observe half-loaded state.
 */
class Emulator
{
public:
    /**
     * @brief Tells the console loops whether to keep reading commands.
     *
     * @return true until exitApp() is called
     */
    bool isAlive() const noexcept
    {
        return alive_;
    }

    /**
     * @brief Shuts the emulator down.
     *
     * Stops the scheduler first, so shutting down can never leave CPU threads
     * running. The console loops exit on their next check.
     */
    void exitApp()
    {
        scheduler_.stop();
        alive_ = false;
    }

    /**
     * @brief Loads config.txt and starts the CPU scheduler.
     *
     * Per the spec, the scheduler then runs for as long as the console is
     * alive. On failure the emulator stays uninitialized and nothing starts.
     *
     * @param path the config file to read
     * @param error set to the reason when loading fails
     * @return true if the config was valid and the scheduler started
     */
    bool initialize(const std::string &path, std::string &error)
    {
        auto loaded = Config::load(path, error);
        if (!loaded)
        {
            return false;
        }
        config_ = std::move(*loaded);
        scheduler_.start(*config_);
        return true;
    }

    /** @return true once initialize() has succeeded */
    bool isInitialized() const
    {
        return config_.has_value();
    }

    /**
     * @brief The loaded configuration.
     *
     * Fails fast rather than returning a default config.
     *
     * @return the config read by initialize()
     * @throws std::logic_error if called before a successful initialize()
     */
    const Config &config() const
    {
        if (!config_)
        {
            throw std::logic_error("Emulator::config() called before initialize()");
        }
        return *config_;
    }

    /** @return the CPU scheduler */
    Scheduler &scheduler() noexcept
    {
        return scheduler_;
    }

    /** @return the CPU scheduler, read-only */
    const Scheduler &scheduler() const noexcept
    {
        return scheduler_;
    }

private:
    bool alive_ = true;            /**< Cleared only by exitApp(). */
    std::optional<Config> config_; /**< Empty until initialize() succeeds. */
    Scheduler scheduler_;          /**< Started by initialize(), stopped by exitApp(). */
};
