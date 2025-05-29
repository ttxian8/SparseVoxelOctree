# System Patterns

这个文件记录项目中使用的重复模式和标准。
虽然是可选的，但建议随着项目发展而更新。
2025-05-29 19:34:03 - 初始化系统模式文档

## Coding Patterns

* **异步加载模式**: 使用专门的LoaderThread进行异步模型加载
* **GPU计算模式**: 使用Vulkan计算着色器进行并行处理
* **RAII模式**: C++资源管理使用RAII原则
* **依赖注入**: 通过构造函数和方法参数传递依赖

## Architectural Patterns

* **分层架构**: 
  - UI层 (ImGui)
  - 应用层 (Application)
  - 渲染层 (PathTracer, OctreeTracer)
  - 数据层 (Scene, Octree)
* **组件模式**: 每个功能模块独立实现（相机、光照、场景等）
* **管线模式**: GPU渲染使用固定的管线流程
* **观察者模式**: UI组件监听数据变化

## Testing Patterns

* 待补充（项目中暂无明显的测试模式）

## File Loading Patterns

* **策略模式**: 不同文件格式使用不同的加载策略
  - 当前: OBJ文件 -> TinyOBJLoader -> 体素化
  - 计划: VOX文件 -> VoxLoader -> 直接体素数据
* **工厂模式**: 根据文件扩展名创建相应的加载器