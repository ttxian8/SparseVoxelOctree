# Product Context

这个文件提供了项目的高级概述和预期创建的产品。它基于 README.md 和工作目录中的其他可用项目相关信息。这个文件会随着项目发展而更新，用于为所有其他模式提供项目目标和上下文。

2025-05-29 19:33:15 - 基于README.md初始化内存银行

## Project Goal

开发一个基于Vulkan的稀疏体素八叉树(Sparse Voxel Octree, SVO)渲染系统，包含：
- GPU SVO构建器（使用光栅化管线）
- 高效的SVO光线追踪器
- 简单的SVO路径追踪器

## Key Features

- 使用Vulkan API进行GPU加速
- 异步模型加载
- 异步路径追踪
- 支持窗口大小调整
- 环境贴图支持
- 队列所有权转移机制
- 比OpenGL版本更快的性能（Vulkan版本在体素化构建方面快20-25倍）

## Overall Architecture

### 核心组件：
- **Voxelizer**: 体素化系统，将3D模型转换为体素数据
- **Octree Builder**: SVO构建器，使用GPU光栅化管线
- **Ray Marcher**: SVO光线追踪器
- **Path Tracer**: 路径追踪器
- **Loader System**: 异步模型加载系统

### 技术栈：
- Vulkan (图形API)
- GLFW (窗口管理)
- GLM (数学计算)
- TinyOBJLoader (OBJ文件加载)
- stb_image (图像加载)
- TinyEXR (EXR文件保存)
- meshoptimizer (网格优化)
- ImGui (UI渲染)
- tinyfiledialogs (文件对话框)
- spdlog (日志系统)

### 当前任务：
添加对.vox格式的支持，允许直接导入体素数据而无需进行体素化过程。