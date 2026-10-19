#pragma once

#include "Config.h"
#include "Scheduler.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

/**
 * @brief Shared application state: liveness plus loaded configuration.
 *
 * CommandHandler mutates it; the main loop observes it. A Config only exists
 * after a successful initialize(), so no command can observe half-loaded state.
 */
class Emulator
{
public:
    /// True while CLI loop should keep running. Set false by exit_app().
    bool is_app_alive = true;

    /// Signals shutdown; loop exits on next iteration check.
    void exit_app()
    {
        scheduler_.stop();
        is_app_alive = false;
    }

    /// Loads and validates config.txt. On failure the emulator stays uninitialized.
    bool initialize(const std::string &path, std::string &error)
    {
        auto loaded = Config::load(path, error);
        if (!loaded)
        {
            return false;
        }
        config_ = std::move(*loaded);
        return true;
    }

    bool isInitialized() const
    {
        return config_.has_value();
    }

    /// Fails fast rather than returning a null or default config.
    const Config &config() const
    {
        if (!config_)
        {
            throw std::logic_error("Emulator::config() called before initialize()");
        }
        return *config_;
    }

    Scheduler &scheduler() noexcept
    {
        return scheduler_;
    }

    const Scheduler &scheduler() const noexcept
    {
        return scheduler_;
    }

private:
    std::optional<Config> config_;
    Scheduler scheduler_;
};
