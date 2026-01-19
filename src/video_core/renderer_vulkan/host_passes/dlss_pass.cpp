//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "core/debug_state.h"
#include "video_core/renderer_vulkan/host_passes/dlss_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#ifdef _WIN32
// Include Streamline SDK headers only on Windows
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#endif

namespace Vulkan::HostPasses {

void DlssPass::Create(vk::Device device, VmaAllocator allocator, u32 num_images, bool is_nvidia_gpu) {
    this->device = device;
    this->allocator = allocator;
    this->num_images = num_images;

    // DLSS requires NVIDIA GPU and Streamline SDK integration
    // For now, only check if it's NVIDIA hardware
    is_available = is_nvidia_gpu;

#ifdef _WIN32
    if (is_nvidia_gpu) {
        InitializeStreamline(device);
    }
#else
    LOG_WARNING(Render_Vulkan, "DLSS is only supported on Windows with NVIDIA GPUs");
#endif

    available_imgs.resize(num_images);
    for (int i = 0; i < num_images; ++i) {
        auto& img = available_imgs[i];
        img.id = i;
        img.output_image = VideoCore::UniqueImage(device, allocator);
    }
}

void DlssPass::InitializeStreamline(vk::Device device) {
#ifdef _WIN32
    if (streamline_initialized) {
        return;
    }

    LOG_INFO(Render_Vulkan, "Initializing NVIDIA Streamline SDK for DLSS 4.5");

    // TODO: Complete Streamline initialization
    // This requires:
    // 1. sl::Preferences setup with application info
    // 2. sl::init() call with Vulkan device handles
    // 3. Feature registration for DLSS-SR and DLSS-G
    // 4. Query supported quality modes and capabilities
    
    // For now, mark as not initialized until full implementation
    streamline_initialized = false;
    
    LOG_WARNING(Render_Vulkan, "Streamline SDK initialization requires additional setup - DLSS will use passthrough mode");
#endif
}

void DlssPass::ShutdownStreamline() {
#ifdef _WIN32
    if (streamline_initialized) {
        // TODO: Call sl::shutdown() when fully implemented
        streamline_initialized = false;
        LOG_INFO(Render_Vulkan, "Streamline SDK shutdown");
    }
#endif
}

vk::ImageView DlssPass::Render(vk::CommandBuffer cmdbuf, const RenderInputs& inputs, Settings settings) {
    // If DLSS is not enabled or not available, pass through input
    if (!settings.enable || !is_available) {
        return inputs.color_input;
    }

    // If no upscaling is needed (input >= output), pass through
    if (inputs.input_size.width >= inputs.output_size.width && 
        inputs.input_size.height >= inputs.output_size.height) {
        return inputs.color_input;
    }

#ifdef _WIN32
    if (streamline_initialized) {
        // TODO: Implement actual DLSS evaluation with Streamline SDK
        // This would involve:
        // 1. Setting up sl::Resource tags for input textures (color, motion vectors, depth)
        // 2. Configuring DLSS constants (quality mode, sharpness, jitter)
        // 3. Calling sl::evaluateFeature with kFeatureDLSS
        // 4. For frame generation (DLSS 4.5), also evaluate kFeatureDLSS_G
        // 5. Proper synchronization and resource state transitions
        
        LOG_DEBUG(Render_Vulkan, "DLSS evaluation with motion vectors: {}, depth: {}", 
                  inputs.motion_vectors ? "yes" : "no",
                  inputs.depth_buffer ? "yes" : "no");
    }
#endif
    
    // Prepare output infrastructure
    if (inputs.output_size != cur_size) {
        ResizeAndInvalidate(inputs.output_size.width, inputs.output_size.height);
    }

    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }

    if (img.dirty) {
        CreateImages(img);
    }

    frame_index++;

    // When DLSS SDK is integrated, the upscaled result would be written to img.output_image
    // and we would return img.output_image_view.get() instead of the input
    // For now, return input as passthrough
    return inputs.color_input;
}

// Legacy interface for backward compatibility
vk::ImageView DlssPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView input,
                                vk::Extent2D input_size, vk::Extent2D output_size, 
                                Settings settings, bool hdr) {
    RenderInputs inputs{};
    inputs.color_input = input;
    inputs.input_size = input_size;
    inputs.output_size = output_size;
    inputs.hdr = hdr;
    
    return Render(cmdbuf, inputs, settings);
}

void DlssPass::ResizeAndInvalidate(u32 width, u32 height) {
    this->cur_size = vk::Extent2D{
        .width = width,
        .height = height,
    };
    for (int i = 0; i < num_images; ++i) {
        available_imgs[i].dirty = true;
    }
}

void DlssPass::CreateImages(Img& img) const {
    img.dirty = false;

    // Create output image for DLSS
    vk::ImageCreateInfo image_create_info{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent{
            .width = cur_size.width,
            .height = cur_size.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    img.output_image.Create(image_create_info);
    SetObjectName(device, static_cast<vk::Image>(img.output_image), "DLSS Output Image #{}",
                  img.id);

    vk::ImageViewCreateInfo image_view_create_info{
        .image = img.output_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    img.output_image_view =
        Check<"create DLSS output image view">(device.createImageViewUnique(image_view_create_info));
    SetObjectName(device, img.output_image_view.get(), "DLSS Output ImageView #{}", img.id);
}

} // namespace Vulkan::HostPasses
