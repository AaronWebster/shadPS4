// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <vector>
#include "common/types.h"

namespace Core {

// Timer-based polling infrastructure similar to xiltimer
class XilTimer {
public:
    using PollCallback = std::function<void()>;

    struct PollTask {
        std::string name;
        PollCallback callback;
        bool enabled;
        
        PollTask(std::string n, PollCallback cb)
            : name(std::move(n)), callback(std::move(cb)), enabled(true) {}
    };

    // Get singleton instance
    static XilTimer& Instance();

    // Register a polling task
    void RegisterPollTask(const std::string& name, PollCallback callback);

    // Execute all registered poll tasks
    void PollAll();

    // Enable/disable a specific task
    void SetTaskEnabled(const std::string& name, bool enabled);

private:
    XilTimer() = default;
    std::vector<PollTask> poll_tasks;
};

} // namespace Core
