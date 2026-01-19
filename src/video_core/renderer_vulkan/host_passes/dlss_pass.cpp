//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/config.h"
#include "common/logging/log.h"
#include "core/debug_state.h"
#include "video_core/renderer_vulkan/host_passes/dlss_pass.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#include <vk_mem_alloc.h>

#ifdef _WIN32
// Include Streamline SDK headers only on Windows
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_helpers_vk.h>

namespace {
// Convert DlssPass quality setting to Streamline DLSSMode
sl::DLSSMode ConvertQuality(Vulkan::HostPasses::DlssPass::Quality quality) {
    switch (quality) {
    case Vulkan::HostPasses::DlssPass::Quality::Performance:
        return sl::DLSSMode::eMaxPerformance;
    case Vulkan::HostPasses::DlssPass::Quality::Balanced:
        return sl::DLSSMode::eBalanced;
    case Vulkan::HostPasses::DlssPass::Quality::Quality:
        return sl::DLSSMode::eMaxQuality;
    case Vulkan::HostPasses::DlssPass::Quality::UltraPerformance:
        return sl::DLSSMode::eUltraPerformance;
    default:
        return sl::DLSSMode::eMaxQuality;
    }
}
} // namespace
#endif

namespace Vulkan::HostPasses {

void DlssPass::Create(vk::Device device, vk::Instance instance, vk::PhysicalDevice physical_device,
                      VmaAllocator allocator, u32 num_images, u32 graphics_queue_family,
                      bool is_nvidia_gpu) {
    this->device = device;
    this->instance = instance;
    this->physical_device = physical_device;
    this->allocator = allocator;
    this->num_images = num_images;
    this->graphics_queue_family = graphics_queue_family;

    // DLSS requires NVIDIA GPU and Streamline SDK integration
    // For now, only check if it's NVIDIA hardware
    is_available = is_nvidia_gpu;

#ifdef _WIN32
    if (is_nvidia_gpu) {
        InitializeStreamline();
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

void DlssPass::InitializeStreamline() {
#ifdef _WIN32
    if (streamline_initialized) {
        return;
    }

    LOG_INFO(Render_Vulkan, "Initializing NVIDIA Streamline SDK for DLSS 4.5");

    // Setup Streamline preferences
    sl::Preferences prefs{};
    prefs.showConsole = false;
    prefs.logLevel = sl::LogLevel::eDefault;
    prefs.pathsToPlugins = nullptr;
    prefs.numPathsToPlugins = 0;
    prefs.pathToLogsAndData = nullptr;
    prefs.allocateCallback = nullptr;
    prefs.releaseCallback = nullptr;
    prefs.logMessageCallback = nullptr;
    prefs.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                  sl::PreferenceFlags::eAllowOTA |
                  sl::PreferenceFlags::eLoadDownloadedPlugins |
                  sl::PreferenceFlags::eUseFrameBasedResourceTagging;

    // Initialize Streamline SDK
    sl::Result result = slInit(prefs, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to initialize Streamline SDK: {}", static_cast<int>(result));
        streamline_initialized = false;
        return;
    }

    // Setup Vulkan device information for Streamline
    sl::VulkanInfo vk_info{};
    vk_info.device = static_cast<VkDevice>(device);
    vk_info.instance = static_cast<VkInstance>(instance);
    vk_info.physicalDevice = static_cast<VkPhysicalDevice>(physical_device);
    vk_info.graphicsQueueFamily = graphics_queue_family;
    vk_info.graphicsQueueIndex = 0;
    vk_info.computeQueueFamily = graphics_queue_family;
    vk_info.computeQueueIndex = 0;
    vk_info.opticalFlowQueueFamily = graphics_queue_family;
    vk_info.opticalFlowQueueIndex = 0;
    vk_info.useNativeOpticalFlowMode = false;

    result = slSetVulkanInfo(vk_info);
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to set Vulkan info for Streamline: {}", static_cast<int>(result));
        slShutdown();
        streamline_initialized = false;
        return;
    }

    // Check if DLSS is supported
    sl::AdapterInfo adapter_info{};
    adapter_info.vkPhysicalDevice = static_cast<VkPhysicalDevice>(physical_device);
    
    result = slIsFeatureSupported(sl::kFeatureDLSS, adapter_info);
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "DLSS feature is not supported on this adapter: {}", static_cast<int>(result));
        slShutdown();
        streamline_initialized = false;
        is_available = false;
        return;
    }

    // Set DLSS feature to loaded state
    result = slSetFeatureLoaded(sl::kFeatureDLSS, true);
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to load DLSS feature: {}", static_cast<int>(result));
        slShutdown();
        streamline_initialized = false;
        return;
    }

    streamline_initialized = true;
    LOG_INFO(Render_Vulkan, "Streamline SDK initialized successfully for DLSS");
#endif
}

void DlssPass::ShutdownStreamline() {
#ifdef _WIN32
    if (streamline_initialized) {
        slShutdown();
        streamline_initialized = false;
        LOG_INFO(Render_Vulkan, "Streamline SDK shutdown completed");
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
    if (!streamline_initialized) {
        LOG_DEBUG(Render_Vulkan, "Streamline not initialized, using passthrough mode");
        return inputs.color_input;
    }

    PrepareOutputImage(inputs.output_size);

    // Setup DLSS options
    sl::DLSSOptions dlss_options{};
    dlss_options.mode = ConvertQuality(settings.quality);
    dlss_options.outputWidth = inputs.output_size.width;
    dlss_options.outputHeight = inputs.output_size.height;
    dlss_options.colorBuffersHDR = inputs.hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    // Setup DLSS constants
    sl::Constants constants{};
    constants.jitterOffset = {inputs.jitter_offset_x, inputs.jitter_offset_y};
    constants.mvecScale = {1.0f, 1.0f};  // Motion vector scale
    constants.reset = inputs.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.sharpness = inputs.sharpness;

    // Get a new frame token
    sl::FrameToken* frame_token = nullptr;
    sl::Result result = slGetNewFrameToken(&frame_token, &frame_index);
    if (result != sl::Result::eOk || !frame_token) {
        LOG_ERROR(Render_Vulkan, "Failed to get frame token: {}", static_cast<int>(result));
        return inputs.color_input;
    }

    // Set constants for this frame
    sl::ViewportHandle viewport{0};
    result = slSetConstants(constants, *frame_token, viewport);
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to set DLSS constants: {}", static_cast<int>(result));
        return inputs.color_input;
    }

    // Create and tag resources for DLSS evaluation
    std::vector<sl::ResourceTag> tags;

    // Tag color input buffer
    if (!inputs.color_image) {
        LOG_WARNING(Render_Vulkan, "Color input image handle not available, using passthrough mode");
        frame_index++;
        return inputs.color_input;
    }

    sl::Resource colorInput{};
    colorInput.type = sl::ResourceType::eTex2d;
    colorInput.native = reinterpret_cast<void*>(static_cast<VkImage>(inputs.color_image));
    colorInput.memory = reinterpret_cast<void*>(static_cast<VkDeviceMemory>(inputs.color_memory));
    colorInput.view = reinterpret_cast<void*>(static_cast<VkImageView>(inputs.color_input));
    colorInput.state = sl::ResourceState::eTextureRead;
    colorInput.extent = {inputs.input_size.width, inputs.input_size.height, 1};
    
    tags.push_back({sl::kBufferTypeScalingInputColor, colorInput});

    // Tag output buffer
    auto& output_img = available_imgs[cur_image];
    sl::Resource colorOutput{};
    colorOutput.type = sl::ResourceType::eTex2d;
    colorOutput.native = reinterpret_cast<void*>(static_cast<VkImage>(output_img.output_image.image));
    colorOutput.view = reinterpret_cast<void*>(static_cast<VkImageView>(output_img.output_image_view.get()));
    colorOutput.state = sl::ResourceState::eTextureWrite;
    colorOutput.extent = {inputs.output_size.width, inputs.output_size.height, 1};
    
    // Get device memory for output image
    if (output_img.output_image.allocation) {
        VmaAllocationInfo alloc_info{};
        vmaGetAllocationInfo(output_img.output_image.allocator, 
                            output_img.output_image.allocation, &alloc_info);
        colorOutput.memory = reinterpret_cast<void*>(alloc_info.deviceMemory);
    }
    
    tags.push_back({sl::kBufferTypeScalingOutputColor, colorOutput});

    // Tag motion vectors if provided
    if (inputs.motion_vectors && inputs.motion_vectors_image) {
        sl::Resource motionVectors{};
        motionVectors.type = sl::ResourceType::eTex2d;
        motionVectors.native = reinterpret_cast<void*>(static_cast<VkImage>(inputs.motion_vectors_image));
        motionVectors.memory = reinterpret_cast<void*>(static_cast<VkDeviceMemory>(inputs.motion_vectors_memory));
        motionVectors.view = reinterpret_cast<void*>(static_cast<VkImageView>(inputs.motion_vectors));
        motionVectors.state = sl::ResourceState::eTextureRead;
        motionVectors.extent = {inputs.input_size.width, inputs.input_size.height, 1};
        
        tags.push_back({sl::kBufferTypeMotionVectors, motionVectors});
        LOG_DEBUG(Render_Vulkan, "DLSS using motion vectors");
    }

    // Tag depth buffer if provided
    if (inputs.depth_buffer && inputs.depth_image) {
        sl::Resource depth{};
        depth.type = sl::ResourceType::eTex2d;
        depth.native = reinterpret_cast<void*>(static_cast<VkImage>(inputs.depth_image));
        depth.memory = reinterpret_cast<void*>(static_cast<VkDeviceMemory>(inputs.depth_memory));
        depth.view = reinterpret_cast<void*>(static_cast<VkImageView>(inputs.depth_buffer));
        depth.state = sl::ResourceState::eTextureRead;
        depth.extent = {inputs.input_size.width, inputs.input_size.height, 1};
        
        tags.push_back({sl::kBufferTypeDepth, depth});
        LOG_DEBUG(Render_Vulkan, "DLSS using depth buffer");
    }

    // Tag all resources with Streamline
    result = slSetTag(viewport, tags.data(), static_cast<uint32_t>(tags.size()), 
                     VkCommandBuffer(cmdbuf));
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to tag resources for DLSS: {}", static_cast<int>(result));
        frame_index++;
        return inputs.color_input;
    }

    // Evaluate DLSS feature
    result = slEvaluateFeature(sl::kFeatureDLSS, *frame_token, viewport);
    if (result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Failed to evaluate DLSS feature: {}", static_cast<int>(result));
        frame_index++;
        return inputs.color_input;
    }

    LOG_DEBUG(Render_Vulkan, "DLSS evaluation successful: {}x{} -> {}x{}", 
              inputs.input_size.width, inputs.input_size.height,
              inputs.output_size.width, inputs.output_size.height);

    frame_index++;
    
    // Return the upscaled output image view
    return output_img.output_image_view.get();
#else
    PrepareOutputImage(inputs.output_size);
    frame_index++;

    // Non-Windows platforms don't support DLSS
    return inputs.color_input;
#endif
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

void DlssPass::PrepareOutputImage(const vk::Extent2D& output_size) {
    // Prepare output infrastructure
    if (output_size != cur_size) {
        ResizeAndInvalidate(output_size.width, output_size.height);
    }

    auto& img = available_imgs[cur_image];
    if (++cur_image >= available_imgs.size()) {
        cur_image = 0;
    }

    if (img.dirty) {
        CreateImages(img);
    }
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
