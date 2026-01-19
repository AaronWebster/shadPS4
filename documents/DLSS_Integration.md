# DLSS 4.5 Integration Guide

## Overview

This document describes the integration of NVIDIA DLSS 4.5 (Deep Learning Super Sampling) into shadPS4 using the NVIDIA Streamline SDK. DLSS provides AI-powered upscaling and frame generation to improve performance while maintaining or enhancing visual quality.

## Current Implementation Status

### ✅ Completed

1. **Streamline SDK Integration**
   - Added NVIDIA Streamline SDK v2.10.0 as a git submodule in `externals/streamline`
   - Integrated into the CMake build system with conditional Windows support
   - Added as a dependency to the main shadPS4 binary

2. **DlssPass Structure**
   - Created `RenderInputs` structure to accept all necessary DLSS inputs:
     - Color input buffer
     - Motion vector buffer (optional)
     - Depth buffer (optional)
     - Input/output resolutions
     - Jitter offsets for TAA integration
     - Sharpness control (0.0-1.0)
     - Reset flag for accumulation
   - Implemented dual Render() interfaces (new and legacy) for backward compatibility
   - Added proper image creation and management infrastructure

3. **Initialization Framework**
   - Added `InitializeStreamline()` method with conditional Windows compilation
   - Added `ShutdownStreamline()` method for cleanup
   - Implemented proper device and allocator tracking
   - Added frame index tracking for temporal data

4. **Renderer Integration**
   - Updated `vk_presenter.cpp` to use the new RenderInputs interface
   - Added TODOs marking where motion vectors and depth buffers need to be extracted
   - Maintained fallback to FSR when DLSS is unavailable

### 🚧 Requires Binary Artifacts (Not in Git Repository)

The following features require the Streamline SDK binary artifacts which must be downloaded separately from [NVIDIA's GitHub releases](https://github.com/NVIDIA-RTX/Streamline/releases):

1. **Complete Streamline Initialization**
   ```cpp
   // In InitializeStreamline():
   // - Call sl::init() with Vulkan device context
   // - Register DLSS-SR and DLSS-G features
   // - Query available quality modes
   // - Set up preferences and application info
   ```

2. **DLSS Evaluation**
   ```cpp
   // In Render():
   // - Set up sl::Resource tags for input textures
   // - Configure DLSS constants (quality, sharpness, jitter)
   // - Call sl::evaluateFeature(sl::kFeatureDLSS)
   // - For frame generation, evaluate sl::kFeatureDLSS_G
   ```

3. **Required DLLs (Windows Only)**
   - `sl.interposer.dll` - Streamline interposer library
   - `sl.common.dll` - Common Streamline functionality
   - `sl.dlss.dll` - DLSS Super Resolution plugin
   - `sl.dlss_g.dll` - DLSS Frame Generation plugin
   - `nvngx_dlss.dll` - NVIDIA NGX DLSS runtime
   - `nvngx_dlssg.dll` - NVIDIA NGX DLSS-G runtime

### ⏳ To Be Implemented

1. **Motion Vector Generation**
   - **Location**: Rendering pipeline (likely in `vk_rasterizer.cpp`)
   - **Method**: Track camera/object transformations between frames
   - **Format**: 2-component float texture with screen-space velocity
   - **Alternative**: Use Streamline's internal motion estimation (lower quality)

2. **Depth Buffer Extraction**
   - **Location**: `vk_presenter.cpp` or `vk_rasterizer.cpp`
   - **Method**: Extract depth attachment from current render pass
   - **Format**: Single-channel depth texture
   - **Note**: PS4 depth buffers are already tracked in `regs.depth_buffer`

3. **TAA Jittering**
   - **Location**: Projection matrix generation
   - **Method**: Apply sub-pixel jitter offsets to projection matrix
   - **Pattern**: Halton or custom sequence for temporal stability
   - **Purpose**: Improves DLSS quality through temporal supersampling

4. **Platform Support**
   - Currently Windows-only due to Streamline SDK limitations
   - Linux support requires custom Vulkan integration without Streamline
   - macOS support not available (DLSS is NVIDIA RTX specific)

## Architecture

```
┌─────────────────┐
│   vk_presenter  │
│                 │
│  - Prepares     │
│    RenderInputs │
│  - Calls        │
│    DlssPass     │
└────────┬────────┘
         │
         ▼
┌─────────────────────┐
│     DlssPass        │
│                     │
│  - Initializes      │
│    Streamline SDK   │
│  - Manages output   │
│    images           │
│  - Evaluates DLSS   │
└────────┬────────────┘
         │
         ▼
┌──────────────────────┐
│  Streamline SDK      │
│                      │
│  - DLSS-SR (2.x-4.0) │
│  - DLSS-G (3.5-4.5)  │
│  - Resource tagging  │
│  - Quality modes     │
└──────────────────────┘
```

## Usage

### Configuration

DLSS settings are controlled through the existing configuration system:

```cpp
DlssPass::Settings dlss_settings;
dlss_settings.enable = true;  // Enable DLSS
dlss_settings.quality = DlssPass::Quality::Quality;  // Quality mode
dlss_settings.frame_generation = true;  // Enable DLSS 4.5 frame generation
```

### Quality Modes

- **Ultra Performance**: 9x faster (3x scaling in each dimension)
- **Performance**: 4x faster (2x scaling in each dimension)  
- **Balanced**: 2.25x faster (1.5x scaling in each dimension)
- **Quality**: 1.56x faster (1.25x scaling in each dimension)

### Rendering

```cpp
// New interface with full inputs
HostPasses::DlssPass::RenderInputs inputs;
inputs.color_input = image_view;
inputs.motion_vectors = motion_vector_view;  // Optional
inputs.depth_buffer = depth_view;            // Optional
inputs.input_size = {1920, 1080};
inputs.output_size = {3840, 2160};
inputs.hdr = true;
inputs.jitter_offset_x = jitter_x;
inputs.jitter_offset_y = jitter_y;
inputs.sharpness = 0.5f;
inputs.reset = false;

vk::ImageView output = dlss_pass.Render(cmdbuf, inputs, dlss_settings);
```

## Building with DLSS Support

### Prerequisites

1. Windows 10 20H1 or newer
2. NVIDIA RTX GPU with driver 512.15 or newer
3. Visual Studio 2019+ with Windows SDK 10.0.19041+
4. Downloaded Streamline SDK binary artifacts

### Steps

1. **Clone with Submodules**
   ```bash
   git clone --recursive https://github.com/shadps4-emu/shadPS4.git
   cd shadPS4
   ```

2. **Download Binary Artifacts**
   - Visit https://github.com/NVIDIA-RTX/Streamline/releases
   - Download `streamline-sdk-v2.10.0.zip`
   - Extract `bin/x64/` contents to `externals/streamline/bin/x64/`
   - Extract `lib/x64/` contents to `externals/streamline/lib/x64/`

3. **Configure CMake**
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```

4. **Build**
   ```bash
   cmake --build build --config Release
   ```

5. **Deploy DLLs**
   The Streamline DLLs will be automatically copied to the output directory during build.

## Performance Considerations

1. **DLSS Overhead**: ~1-2ms per frame for evaluation
2. **Frame Generation**: Additional latency of 1 frame
3. **Memory Usage**: Additional ~200MB for DLSS models
4. **Quality vs Performance**: Higher quality modes provide better visuals but less performance gain

## Debugging

### Enable Streamline Logging

Set environment variables:
```
SL_ENABLE_CONSOLE_LOGGING=1
SL_LOG_LEVEL=2
SL_LOG_PATH=C:\path\to\logs
```

### Common Issues

1. **DLSS not initializing**: Check driver version (must be 512.15+)
2. **Black screen**: Verify DLL signatures and ensure no modified binaries
3. **Artifacts**: Check motion vector format and depth buffer consistency
4. **Performance regression**: Verify GPU supports DLSS (RTX 20-series or newer)

## Future Enhancements

1. **Automatic Motion Vector Generation**: Generate from frame differences
2. **DLSS Ray Reconstruction (RR)**: Denoise ray-traced effects
3. **NVIDIA Reflex Integration**: Reduce input latency
4. **Multi-GPU Support**: Balance DLSS across multiple GPUs
5. **Preset Profiles**: Game-specific quality/performance presets

## References

- [NVIDIA Streamline SDK](https://github.com/NVIDIA-RTX/Streamline)
- [DLSS Integration Guide](https://developer.nvidia.com/rtx/streamline)
- [Programming Guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md)
- [Vulkan Sample](https://github.com/nvpro-samples/vk_streamline)

## License

The NVIDIA Streamline SDK is provided under NVIDIA's license terms. See `externals/streamline/license.txt` for details.

DLSS technology is proprietary to NVIDIA Corporation. Usage requires accepting NVIDIA's terms and conditions.
