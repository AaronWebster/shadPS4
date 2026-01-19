// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "net_phy.h"
#include "common/config.h"
#include "common/logging/log.h"
#include <SDL3/SDL_timer.h>

namespace Libraries::NetPhy {

void Phy_Init(PhyDriver* phy) {
    if (!phy) {
        LOG_ERROR(Lib_Net, "Phy_Init: Invalid PHY driver pointer");
        return;
    }

    LOG_INFO(Lib_Net, "PHY driver initializing...");
    phy->state = PhyState::INITIALIZING;
    phy->init_counter = 0;
    phy->link_up = false;
    phy->last_poll_time = SDL_GetTicks();
}

void Phy_Poll(PhyDriver* phy) {
    if (!phy) {
        return;
    }

    u32 current_time = SDL_GetTicks();
    phy->last_poll_time = current_time;

    switch (phy->state) {
    case PhyState::UNINITIALIZED:
        // PHY not initialized, do nothing
        break;
        
    case PhyState::INITIALIZING:
        // Simulate initialization progress
        phy->init_counter++;
        
        // After a few poll cycles, mark as operational
        if (phy->init_counter >= 3) {
            phy->state = PhyState::OPERATIONAL;
            phy->link_up = Config::getIsConnectedToNetwork();
            LOG_INFO(Lib_Net, "PHY driver now operational (link {})", 
                     phy->link_up ? "up" : "down");
        }
        break;
        
    case PhyState::OPERATIONAL:
        // Update link status based on network configuration
        phy->link_up = Config::getIsConnectedToNetwork();
        break;
        
    case PhyState::ERROR:
        // In error state, stay there unless re-initialized
        break;
    }
}

bool Phy_IsOperational(const PhyDriver* phy) {
    return phy && phy->state == PhyState::OPERATIONAL;
}

} // namespace Libraries::NetPhy
