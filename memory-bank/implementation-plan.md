# .vox格式支持实现计划

**创建时间：** 2025-05-29 19:35:16  
**目标：** 为SparseVoxelOctree项目添加对MagicaVoxel .vox格式的直接支持

## 架构分析结果

### 现有加载流程
1. **UI触发** → UILoader.cpp (只支持*.obj过滤器)
2. **文件加载** → LoaderThread::Launch() → LoaderThread::thread_func()
3. **场景创建** → Scene::Create() → Scene::load_meshes() (使用tiny_obj_loader)
4. **体素化** → Voxelizer::Create() → 将网格转换为体素
5. **SVO构建** → OctreeBuilder::Create() → 构建稀疏体素八叉树

### 问题识别
- Scene::load_meshes()硬编码使用tiny_obj_loader
- UI文件过滤器只支持*.obj
- .vox文件已经是体素数据，不需要体素化步骤

## 实现方案

### 1. 创建VoxLoader模块
**文件：** `src/VoxLoader.hpp`, `src/VoxLoader.cpp`

**功能：**
- 解析MagicaVoxel .vox文件格式
- 提供与Scene兼容的接口
- 直接输出体素数据，跳过网格阶段

**核心类：**
```cpp
class VoxLoader {
public:
    static std::shared_ptr<VoxData> LoadVox(const char* filename);
    
private:
    struct VoxHeader { /* .vox文件头 */ };
    struct VoxChunk { /* 块结构 */ };
    // 解析方法
};

struct VoxData {
    std::vector<VoxelData> voxels;
    glm::ivec3 dimensions;
    std::vector<glm::vec3> palette; // 调色板
};
```

### 2. 修改Scene类
**文件：** `src/Scene.hpp`, `src/Scene.cpp`

**变更：**
- 添加工厂方法检测文件格式
- Scene::Create()根据文件扩展名选择加载器
- 为.vox文件创建专门的创建路径

**新增方法：**
```cpp
static std::shared_ptr<Scene> CreateFromVox(
    const std::shared_ptr<myvk::Queue> &graphics_queue, 
    const char *filename,
    std::atomic<const char *> *notification_ptr = nullptr
);
```

### 3. 修改LoaderThread
**文件：** `src/LoaderThread.cpp`

**变更：**
- 在thread_func()中检测文件格式
- 对于.vox文件，跳过Voxelizer创建，直接从体素数据构建OctreeBuilder
- 保持现有OBJ文件流程不变

**修改逻辑：**
```cpp
// 检测文件类型
std::string extension = get_file_extension(filename);
if (extension == ".vox") {
    // 直接从.vox数据创建OctreeBuilder
    auto vox_data = VoxLoader::LoadVox(filename);
    builder = OctreeBuilder::CreateFromVoxels(vox_data, loader_command_pool);
} else {
    // 现有OBJ流程
    scene = Scene::Create(m_loader_queue, filename, &m_notification);
    voxelizer = Voxelizer::Create(scene, loader_command_pool, octree_level);
    builder = OctreeBuilder::Create(voxelizer, loader_command_pool);
}
```

### 4. 扩展OctreeBuilder
**文件：** `src/OctreeBuilder.hpp`, `src/OctreeBuilder.cpp`

**新增：**
- 添加静态方法`CreateFromVoxels()`
- 接受体素数据直接构建八叉树，跳过体素化阶段

### 5. 修改UI
**文件：** `src/UILoader.cpp`

**变更：**
- 扩展文件过滤器支持.vox格式
- 修改第27行：`constexpr const char *kFilter[] = {"*.obj", "*.vox"};`
- 更新文件选择提示文本

### 6. 研究.vox格式规范
**任务：**
- 分析MagicaVoxel .vox文件格式
- 理解块结构、调色板、体素数据编码
- 确定坐标系转换需求

## 实现顺序

### 阶段1：格式研究和基础设施
1. 研究.vox文件格式规范
2. 创建VoxLoader基本结构
3. 实现.vox文件解析器
4. 单元测试VoxLoader

### 阶段2：核心集成
5. 修改Scene类添加.vox支持
6. 扩展OctreeBuilder支持直接体素输入
7. 修改LoaderThread逻辑分流

### 阶段3：UI和完善
8. 更新UI文件过滤器
9. 集成测试
10. 性能优化
11. 错误处理完善

## 技术挑战

### 1. .vox格式兼容性
- 需要支持不同版本的.vox格式
- 处理不同的块类型和扩展

### 2. 坐标系转换
- MagicaVoxel使用的坐标系可能与项目不同
- 需要适当的轴变换

### 3. 内存效率
- .vox文件可能包含大量体素数据
- 需要高效的内存管理和流式加载

### 4. 颜色处理
- .vox文件包含调色板信息
- 需要转换为项目使用的材质系统

## 预期成果

- 用户可以直接加载.vox文件而无需转换
- 加载性能提升（跳过体素化步骤）
- 保持现有OBJ文件支持
- UI提供统一的文件选择体验

## 风险评估

**低风险：**
- UI修改
- 文件格式检测

**中风险：**
- .vox解析器实现
- OctreeBuilder扩展

**高风险：**
- LoaderThread逻辑修改（可能影响现有功能）
- 内存管理和性能优化