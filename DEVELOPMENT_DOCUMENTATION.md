# Sparse Voxel Octree (SVO) - Comprehensive Development Documentation

## Table of Contents
1. [Introduction and Overview](#1-introduction-and-overview)
2. [System Architecture](#2-system-architecture)
3. [Core Components](#3-core-components)
4. [Data Flow and Processing Pipeline](#4-data-flow-and-processing-pipeline)
5. [Algorithm Implementations](#5-algorithm-implementations)
6. [Performance Considerations](#6-performance-considerations)
7. [Extension Points](#7-extension-points)
8. [Developer Guidelines](#8-developer-guidelines)
9. [Appendix: Configuration Parameters](#9-appendix-configuration-parameters)

## 1. Introduction and Overview

The Sparse Voxel Octree (SVO) project is a high-performance GPU-accelerated voxel rendering system implemented in Vulkan. It provides efficient algorithms for:

- Voxelizing triangle meshes using GPU rasterization
- Building sparse voxel octrees from voxelized data
- Real-time ray marching visualization
- Path tracing with global illumination

The project represents a significant improvement over previous implementations, with up to 25x performance gains compared to the OpenGL version. It supports features such as asynchronous model loading, interactive octree visualization, and offline path tracing with progressive refinement.

### 1.1 Key Features

- **GPU-accelerated SVO Construction**: Fast octree building on the GPU
- **Real-time Ray Marching**: Interactive visualization with multiple view modes
- **Path Tracing**: High-quality offline rendering with global illumination
- **Asynchronous Processing**: Non-blocking model loading and rendering
- **Multiple Input Formats**: Support for OBJ models and VOX data
- **DLSS Integration**: NVIDIA DLSS for enhanced performance

## 2. System Architecture

The SVO project is structured around several key components that work together to form a complete rendering pipeline:

### 2.1 Core Architecture Diagram

```
Application
├── Scene Management
│   ├── Scene Loading (OBJ)
│   └── Voxel Data Loading (VOX)
├── Voxelization Pipeline
│   ├── Conservative Rasterization
│   └── Fragment Collection
├── Octree Construction
│   ├── Tag Node Pass
│   ├── Init Node Pass
│   ├── Alloc Node Pass
│   └── Modify Arg Pass
├── Rendering Systems
│   ├── Octree Tracing (Real-time)
│   └── Path Tracing (Offline)
└── User Interface
    ├── Camera Control
    ├── Lighting Settings
    ├── Tracing Options
    └── Model Loading
```

### 2.2 Key Architectural Components

1. **Application Core**: Main window and application loop management
2. **Scene Management**: Loading and managing 3D models
3. **Voxelization Pipeline**: Converting triangle meshes to voxels
4. **Octree Construction**: Building SVO from voxel data
5. **Rendering Systems**: Real-time and offline rendering
6. **Multi-threading**: Asynchronous loading and rendering

### 2.3 Vulkan Integration

The project uses a custom Vulkan wrapper library (MyVK) that abstracts common Vulkan patterns:

- RAII-style resource management
- Descriptor set abstraction
- Compute and graphics pipeline creation
- Command buffer management
- Multi-queue coordination

### 2.4 Multi-threading Architecture

The application employs multiple threads for different tasks:

- **Main Thread**: UI rendering and user interaction
- **Loader Thread**: Asynchronous model loading and octree construction
- **Path Tracer Thread**: Background path tracing with periodic updates

## 3. Core Components

### 3.1 Application Class

The central orchestrating component that:
- Initializes Vulkan resources and window
- Creates and manages rendering passes
- Handles window and input events
- Coordinates between different rendering systems
- Manages UI state and interactions

Key methods:
- `Application::Run()`: Main application loop
- `Application::Load()`: Loads model and constructs octree
- `Application::draw_frame()`: Renders a single frame

### 3.2 Scene Management

#### 3.2.1 Scene Class

The Scene class handles:
- Loading 3D mesh data via TinyOBJLoader
- Managing vertex/index buffers
- Texture loading and management
- Drawing mesh data during voxelization

Key methods:
- `Scene::Create()`: Factory method for scene creation
- `Scene::CmdDraw()`: Draw command for rendering
- `Scene::GetVertexBindingDescriptions()`: Vertex layout information

#### 3.2.2 Alternative Input

- **VoxLoader**: Loads voxel data directly from VOX files
  - Parses VOX file format
  - Extracts voxel positions and colors

- **VoxDataAdapter**: Adapts voxel data for octree construction
  - Converts VOX data to voxel fragment list
  - Provides consistent interface with Voxelizer output

### 3.3 Voxelization System

#### 3.3.1 Voxelizer Class

Implements:
- GPU-accelerated mesh voxelization
- Conservative rasterization for watertight voxelization
- Multi-pass approach from three orthogonal directions
- Atomic counter-based fragment collection
- Configurable resolution via octree level parameter

Key methods:
- `Voxelizer::Create()`: Factory method with scene and octree level
- `Voxelizer::CmdVoxelize()`: Command buffer recording for voxelization
- `Voxelizer::GetVoxelFragmentList()`: Access to voxelized data

#### 3.3.2 Voxelization Process

1. Setup render pass for voxelization
2. Configure viewport to voxel grid dimensions
3. Enable conservative rasterization if available
4. Render from X, Y, and Z directions
5. Collect fragments in a buffer

### 3.4 Octree Structure

#### 3.4.1 Octree Class

Manages:
- GPU buffer for storing the octree data
- Descriptor sets for shader access
- Level and range information
- Queue ownership transfer functionality

Key methods:
- `Octree::Create()`: Factory method
- `Octree::Update()`: Updates octree from builder
- `Octree::GetDescriptorSet()`: Provides shader access to octree

#### 3.4.2 Octree Node Format

Each node in the octree is represented by a 32-bit integer with the following layout:
- Bit 31 (0x80000000): Node exists flag
- Bit 30 (0x40000000): Leaf node flag
- Bits 0-29 (0x3FFFFFFF): 
  - For internal nodes: Child pointer
  - For leaf nodes: Color data (RGB)

### 3.5 Octree Construction

#### 3.5.1 OctreeBuilder Class

Implements a multi-pass algorithm:
- Tag Node: Mark nodes containing voxels
- Init Node: Create octree hierarchy
- Alloc Node: Allocate memory for nodes
- Modify Arg: Finalize octree structure

Key methods:
- `OctreeBuilder::Create()`: Factory method with voxelizer or vox adapter
- `OctreeBuilder::CmdBuild()`: Command buffer recording for octree building
- `OctreeBuilder::GetOctreeRange()`: Size information for the built octree

#### 3.5.2 Construction Process Details

1. **Tag Node Pass**:
   - Process voxel fragments
   - Mark nodes that contain voxels
   - Use atomic operations for thread safety

2. **Init Node Pass**:
   - Create parent-child relationships
   - Initialize node hierarchy
   - Set up node flags

3. **Alloc Node Pass**:
   - Allocate memory for child nodes
   - Use compact addressing scheme
   - Update node pointers

4. **Modify Arg Pass**:
   - Set leaf node colors
   - Finalize node flags
   - Update node hierarchy

### 3.6 Rendering Systems

#### 3.6.1 OctreeTracer Class

Real-time visualization using:
- Fragment shader-based ray marching
- Multiple view modes:
  - Diffuse: Surface color
  - Normal: Surface normals
  - Position: 3D position
  - Iteration: Traversal steps

Key methods:
- `OctreeTracer::Create()`: Factory method
- `OctreeTracer::CmdDrawPipeline()`: Main rendering command
- `OctreeTracer::CmdBeamRenderPass()`: Beam optimization rendering

#### 3.6.2 PathTracer Class

Offline rendering with:
- Monte Carlo integration with importance sampling
- Global illumination effects
- Progressive refinement
- Configurable bounce depth

Key methods:
- `PathTracer::Create()`: Factory method
- `PathTracer::CmdRender()`: Path tracing command
- `PathTracer::Reset()`: Reset accumulation buffer
- `PathTracer::Extract*Image()`: Extract rendered results

#### 3.6.3 PathTracerViewer Class

Displays path traced results:
- Manages render targets
- Provides post-processing options
- Handles presentation to screen

### 3.7 Multi-threading Components

#### 3.7.1 LoaderThread Class

- Asynchronous model loading and octree construction
- Provides status updates to UI
- Safely transfers data between threads

#### 3.7.2 PathTracerThread Class

- Background path tracing
- Progressive refinement
- Periodic UI updates
- Safe queue synchronization

## 4. Data Flow and Processing Pipeline

### 4.1 Input Processing

The data flow begins with loading 3D content:

1. **OBJ Processing Path**:
   - Load OBJ file into Scene object
   - Extract vertices, indices, and materials
   - Prepare for voxelization

2. **VOX Processing Path**:
   - Load VOX file via VoxLoader
   - Convert to fragment data via VoxDataAdapter
   - Bypass voxelization step

### 4.2 Voxelization Pipeline

For mesh data, the voxelization process:

1. Create framebuffer with dimensions based on octree level
2. Render scene from three orthogonal directions
3. Generate voxel fragments for each rasterized fragment
4. Count fragments and allocate buffer
5. Perform final voxelization to fill the buffer

Output: Buffer containing voxel fragments with positions and colors

### 4.3 Octree Construction Pipeline

Using the voxel fragment data:

1. **Tag Node Pass**:
   - Process each voxel fragment
   - Identify which nodes contain voxels
   - Set existence flags

2. **Init Node Pass**:
   - Create octree hierarchy
   - Initialize parent-child relationships
   - Set up node structure

3. **Alloc Node Pass**:
   - Allocate memory for child nodes
   - Assign addresses to nodes
   - Update node pointers

4. **Modify Arg Pass**:
   - Set final node properties
   - Update leaf node colors
   - Finalize octree structure

Output: GPU buffer containing complete octree structure

### 4.4 Rendering Pipeline

Two main rendering paths:

1. **Real-time Visualization**:
   - Setup render pass
   - Bind octree descriptor set
   - Execute beam optimization pass (if enabled)
   - Perform ray marching in fragment shader
   - Display results to screen

2. **Path Tracing**:
   - Initialize random number generator (Sobol)
   - Dispatch compute shader for path tracing
   - Accumulate results over multiple frames
   - Display results via PathTracerViewer
   - Save output if requested

## 5. Algorithm Implementations

### 5.1 Ray Marching Algorithm

The octree traversal algorithm (octree.glsl) uses a stack-based approach:

#### 5.1.1 Core Operations

- **PUSH**: Descend to a child node when a ray intersects it
- **POP**: Return to the parent node when exiting a subtree
- **ADVANCE**: Move to the next voxel along the ray

#### 5.1.2 Algorithm Steps

1. Initialize ray parameters and stack
2. For each step:
   - If current node is empty, load from octree buffer
   - Compute intersection with current node
   - If node exists and intersects ray:
     - If leaf node, return intersection data
     - Otherwise, PUSH to child node
   - If no intersection, ADVANCE to next potential node
   - If child bits flip disagree with ray direction, POP to parent

#### 5.1.3 Optimizations

- Fixed-size stack (STACK_SIZE=23) to avoid dynamic allocation
- Bit manipulation for efficient child index calculation
- Early termination for occlusion tests
- Special handling for near-zero ray components
- Beam optimization for coherent rays

### 5.2 Path Tracing Implementation

#### 5.2.1 Monte Carlo Integration

The path tracer implements:
- Recursive path tracing with Russian roulette termination
- Multiple importance sampling for efficient light sampling
- Environment map sampling for image-based lighting
- Material sampling for BRDF evaluation

#### 5.2.2 Sampling Strategies

- **Direct Light Sampling**: Sample light sources directly
- **BRDF Sampling**: Sample directions based on BRDF
- **MIS Weighting**: Balance between light and BRDF sampling
- **Quasi-Monte Carlo**: Use Sobol sequences for better sampling

#### 5.2.3 Progressive Refinement

- Accumulate samples over multiple frames
- Calculate running average for display
- Reset on camera or scene changes
- Extract results when desired quality is reached

### 5.3 Voxelization Algorithm

#### 5.3.1 Conservative Rasterization

- Use hardware conservative rasterization when available
- Otherwise, implement geometry shader expansion for conservativeness
- Render from three orthogonal directions for watertight voxelization

#### 5.3.2 Fragment Collection

- Atomic counter for thread-safe fragment counting
- Two-pass approach:
  1. Count needed fragments
  2. Allocate and fill buffer with exact size
- Use of shader storage buffer objects for efficient data collection

## 6. Performance Considerations

### 6.1 Memory Usage

- Octree depth affects memory consumption (O(8^depth) in worst case)
- Sparse representation significantly reduces memory requirements
- Node allocation is limited by configuration parameters:
  - kOctreeNodeNumMin: Minimum nodes to allocate
  - kOctreeNodeNumMax: Maximum nodes to allocate

### 6.2 GPU Optimization

#### 6.2.1 Compute Shader Efficiency

- Optimal work group size for compute shaders
- Minimize atomic operation contention
- Batch processing of voxel fragments

#### 6.2.2 Queue Management

- Separate queues for graphics and compute operations
- Queue ownership transfers for cross-queue operations
- Careful synchronization between queues

#### 6.2.3 Memory Access Patterns

- Coalesced memory access for better throughput
- Minimize divergent execution
- Strategic data layout for cache efficiency

### 6.3 Multi-threading Considerations

- Thread safety for shared resources
- Lock-free design where possible
- Semaphore-based synchronization for cross-thread operations
- Command buffer recycling for reduced overhead

### 6.4 Beam Optimization

- Improves coherency for neighboring rays
- Reduces divergence in ray traversal
- Configurable beam size (kBeamSize)
- Pre-computation of traversal data

### 6.5 DLSS Integration

- Resolution upscaling for improved performance
- Motion vector generation for temporal stability
- Exposure control for consistent results
- Multiple quality settings for performance/quality tradeoff

## 7. Extension Points

### 7.1 Immediate Extensions (Mentioned in TODOs)

- **Voxel Editor**: Interactive modification of voxel data
  - Direct octree manipulation
  - Brush-based editing tools
  - Undo/redo functionality

- **Gradient-Domain Path Tracing**: Improved noise reduction
  - Compute gradients between neighboring pixels
  - Solve Poisson equation for reconstruction
  - Reduced noise for same sample count

- **SVO Contour Building**: Surface extraction from volumetric data
  - Dual contouring algorithm
  - Sharp feature preservation
  - Level-of-detail extraction

### 7.2 Potential Future Extensions

#### 7.2.1 Input and Data Extensions

- **Additional Input Formats**: Support for more voxel data formats
  - Point cloud data
  - Volumetric data (medical, scientific)
  - Procedural generation

- **Animation Support**: Animated voxel scenes
  - Time-varying octree structures
  - Keyframe interpolation
  - Skeletal animation adaptation

#### 7.2.2 Rendering Extensions

- **Advanced Rendering Effects**:
  - Volumetric lighting and fog
  - Subsurface scattering
  - Dynamic lighting and shadows
  - Ambient occlusion improvements

- **Denoising Integration**: Post-processing for path traced results
  - Temporal denoising
  - Feature-based denoising
  - Machine learning approaches

#### 7.2.3 System Extensions

- **Physics Integration**: Collision detection using the octree structure
  - Rigid body dynamics
  - Particle systems
  - Fluid simulation

- **Level of Detail**: Dynamic octree simplification
  - View-dependent simplification
  - Progressive loading
  - Streaming for large scenes

## 8. Developer Guidelines

### 8.1 Adding New Features

#### 8.1.1 New Input Format

1. Create a new loader class in `src/`
2. Implement parsing logic for the format
3. Create an adapter class to convert to voxel fragments
4. Update UI to support the new format
5. Connect to OctreeBuilder

Example:
```cpp
// Step 1: Create loader
class NewFormatLoader {
public:
    static bool Load(const char* filename, NewFormatData* data);
};

// Step 2: Create adapter
class NewFormatAdapter {
public:
    static std::shared_ptr<NewFormatAdapter> Create(
        const NewFormatData& data,
        const std::shared_ptr<myvk::Device>& device,
        const std::shared_ptr<myvk::CommandPool>& command_pool,
        uint32_t octree_level);
        
    uint32_t GetVoxelFragmentCount() const;
    const std::shared_ptr<myvk::Buffer>& GetVoxelFragmentList() const;
};

// Step 3: Update OctreeBuilder to accept the new adapter
```

#### 8.1.2 New Rendering Technique

1. Add new shader files in `shader/` directory
2. Create a new renderer class in `src/`
3. Implement descriptor set and pipeline creation
4. Add UI controls for the new renderer
5. Integrate with Application class

Example:
```cpp
// Step 1: Create renderer class
class NewRenderer {
public:
    static std::shared_ptr<NewRenderer> Create(
        const std::shared_ptr<Octree>& octree,
        const std::shared_ptr<Camera>& camera,
        const std::shared_ptr<Lighting>& lighting,
        const std::shared_ptr<myvk::RenderPass>& render_pass,
        uint32_t subpass,
        uint32_t frame_count);
        
    void CmdDrawPipeline(
        const std::shared_ptr<myvk::CommandBuffer>& command_buffer,
        uint32_t current_frame) const;
};

// Step 2: Create UI class
class UINewRenderer {
public:
    void Render(NewRenderer* renderer);
};

// Step 3: Update Application to integrate new renderer
```

#### 8.1.3 Octree Manipulation

1. Extend OctreeBuilder with modification operations
2. Implement compute shaders for octree editing
3. Create UI components for interaction
4. Add undo/redo functionality

### 8.2 Performance Tuning

#### 8.2.1 Key Parameters

- **Octree Level**: Balance between detail and performance
  - Higher levels provide more detail but consume more memory
  - Recommended range: kOctreeLevelMin to kOctreeLevelMax (1-12)

- **Path Tracing Bounces**: Affects quality and rendering time
  - More bounces capture more indirect lighting but increase render time
  - Recommended range: kMinBounce to kMaxBounce (2-16)

- **Beam Size**: Impacts ray coherency optimization
  - Larger sizes can improve performance for coherent rays
  - Default: kBeamSize (8)

#### 8.2.2 Profiling Guidelines

1. Use Vulkan debug markers for GPU profiling
2. Monitor memory usage with VkPhysicalDeviceMemoryProperties
3. Use timestamps for performance measurements
4. Profile different octree levels for optimal quality/performance

### 8.3 Code Organization

- **Core Components**: `src/` directory contains main C++ classes
- **Shader Code**: `shader/` directory contains GLSL code
- **UI Components**: UI* classes handle different aspects of the interface
- **Vulkan Wrapper**: MyVK/ directory contains Vulkan abstractions

### 8.4 Debugging Tips

1. Enable Vulkan validation layers in debug builds
2. Use `spdlog` for logging at appropriate levels
3. Check atomic counter values for unexpected results
4. Verify octree structure with debug visualization modes
5. Use renderdoc or similar tools for graphics debugging

## 9. Appendix: Configuration Parameters

### 9.1 Application Configuration (Config.hpp)

| Parameter | Value | Description |
|-----------|-------|-------------|
| kAppName | "Vulkan SVO" | Application name |
| kDefaultWidth | 1280 | Default window width |
| kDefaultHeight | 720 | Default window height |
| kMinWidth | 256 | Minimum window width |
| kMaxWidth | 3840 | Maximum window width |
| kFrameCount | 3 | Number of frames in flight |
| kCamNear | 1.0f/512.0f | Camera near plane |
| kCamFar | 4.0f | Camera far plane |
| kFilenameBufSize | 512 | Filename buffer size |
| kLogLimit | 256 | Log message limit |

### 9.2 Octree Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| kOctreeLevelMin | 1 | Minimum octree level |
| kOctreeLevelMax | 12 | Maximum octree level |
| kOctreeNodeNumMin | 1000000 | Minimum node allocation |
| kOctreeNodeNumMax | 500000000 | Maximum node allocation |
| kBeamSize | 8 | Beam optimization size |

### 9.3 Path Tracing Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| kMinBounce | 2 | Minimum ray bounce depth |
| kDefaultBounce | 4 | Default ray bounce depth |
| kMaxBounce | 16 | Maximum ray bounce depth |
| kDefaultConstantColor | 5.0f | Default lighting intensity |
| kMaxConstantColor | 100.0f | Maximum lighting intensity |
| kPTResultUpdateInterval | 10 | Update interval for path tracing |

### 9.4 Shader Constants (octree.glsl)

| Parameter | Value | Description |
|-----------|-------|-------------|
| STACK_SIZE | 23 | Octree traversal stack size |
| EPS | 3.552713678800501e-15 | Epsilon for floating point comparison |
| OCTREE_SET | 0 | Descriptor set binding for octree |
