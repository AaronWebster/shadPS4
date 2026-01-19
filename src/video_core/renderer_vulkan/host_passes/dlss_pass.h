//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

class DlssPass {
public:
    enum class Quality : int {
        Performance = 0,
        Balanced = 1,
        Quality = 2,
        UltraPerformance = 3
    };

    struct Settings {
        bool enable{false};
        Quality quality{Quality::Quality};
        bool frame_generation{false};
    };

    struct RenderInputs {
        vk::ImageView color_input;
        vk::ImageView motion_vectors;  // Motion vector buffer (optional)
        vk::ImageView depth_buffer;    // Depth buffer (optional)
        vk::Extent2D input_size;
        vk::Extent2D output_size;
        bool hdr{false};
        float jitter_offset_x{0.0f};   // Jitter offset for TAA
        float jitter_offset_y{0.0f};
        float sharpness{0.0f};         // Sharpness control (0.0-1.0)
        bool reset{false};             // Reset accumulation
    };

    void Create(vk::Device device, VmaAllocator allocator, u32 num_images, bool is_nvidia_gpu);

    vk::ImageView Render(vk::CommandBuffer cmdbuf, const RenderInputs& inputs, Settings settings);

    // Legacy interface for backward compatibility
    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                         vk::Extent2D output_size, Settings settings, bool hdr);

    bool IsAvailable() const {
        return is_available;
    }

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::UniqueImage output_image;
        vk::UniqueImageView output_image_view;
    };

    void ResizeAndInvalidate(u32 width, u32 height);
    void CreateImages(Img& img) const;

    // Streamline SDK initialization
    void InitializeStreamline(vk::Device device);
    void ShutdownStreamline();

    vk::Device device{};
    VmaAllocator allocator{};
    u32 num_images{};
    bool is_available{false};
    bool streamline_initialized{false};

    vk::Extent2D cur_size{};
    u32 cur_image{};
    u32 frame_index{0};
    std::vector<Img> available_imgs;
};

} // namespace Vulkan::HostPasses
