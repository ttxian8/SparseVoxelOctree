# Decision Log

这个文件使用列表格式记录架构和实现决策。
2025-05-29 19:33:51 - 初始化决策日志

## Decision

### 2025-05-29 19:33:51 - 添加.vox格式支持的架构决策

**决策内容：** 为SparseVoxelOctree项目添加对.vox格式的直接支持，绕过体素化过程

## Rationale 

**理由：**
- 当前系统只支持OBJ文件，需要先体素化才能使用
- .vox格式本身就是体素数据，可以直接使用，提高效率
- 减少处理步骤，提升用户体验
- 扩展项目的文件格式兼容性

## Implementation Details

**实现细节：**
- 需要研究.vox文件格式规范（MagicaVoxel格式）
- 在现有加载系统中添加.vox文件解析器
- 修改UI文件选择对话框支持.vox扩展名
- 将.vox数据转换为项目内部的体素表示格式
- 集成到现有的SVO构建流水线中