// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Libraries::NetPhy {

// PHY (Physical Layer) driver state
enum class PhyState : u32 {
    UNINITIALIZED = 0, // PHY not initialized yet
    INITIALIZING = 1,  // PHY initialization in progress
    OPERATIONAL = 2,   // PHY is operational and ready
    ERROR = 3          // PHY encountered an error
};

// PHY driver state structure
struct PhyDriver {
    PhyState state;
    u32 init_counter;      // Counter for initialization progress
    bool link_up;          // Link status
    u32 last_poll_time;    // Last time Poll was called
    
    PhyDriver() 
        : state(PhyState::UNINITIALIZED)
        , init_counter(0)
        , link_up(false)
        , last_poll_time(0) {}
};

// Initialize the PHY driver
void Phy_Init(PhyDriver* phy);

// Poll the PHY driver to advance its state
void Phy_Poll(PhyDriver* phy);

// Check if PHY is operational
bool Phy_IsOperational(const PhyDriver* phy);

} // namespace Libraries::NetPhy
