// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "xiltimer.h"
#include "common/logging/log.h"

namespace Core {

XilTimer& XilTimer::Instance() {
    static XilTimer instance;
    return instance;
}

void XilTimer::RegisterPollTask(const std::string& name, PollCallback callback) {
    LOG_INFO(Core, "Registering poll task: {}", name);
    poll_tasks.emplace_back(name, std::move(callback));
}

void XilTimer::PollAll() {
    for (auto& task : poll_tasks) {
        if (task.enabled && task.callback) {
            try {
                task.callback();
            } catch (const std::exception& e) {
                LOG_ERROR(Core, "Exception in poll task '{}': {}", task.name, e.what());
            } catch (...) {
                LOG_ERROR(Core, "Unknown exception in poll task '{}'", task.name);
            }
        }
    }
}

void XilTimer::SetTaskEnabled(const std::string& name, bool enabled) {
    for (auto& task : poll_tasks) {
        if (task.name == name) {
            task.enabled = enabled;
            LOG_DEBUG(Core, "Poll task '{}' {}", name, enabled ? "enabled" : "disabled");
            break;
        }
    }
}

} // namespace Core
