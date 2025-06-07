#include "LoaderThread.hpp"
#include "VoxLoader.hpp"
#include "VoxDataAdapter.hpp"
#include <spdlog/spdlog.h>

// 辅助函数：获取文件扩展名
static std::string get_file_extension(const char* filename) {
	
// LoaderThread 获取构建好的builder
std::shared_ptr<OctreeBuilder> LoaderThread::GetBuiltBuilder() const {
	return m_built_builder;
}
    if (!filename) return "";
    std::string fname(filename);
    size_t dot_pos = fname.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "";
    }
    std::string ext = fname.substr(dot_pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::shared_ptr<LoaderThread> LoaderThread::Create(const std::shared_ptr<Octree> &octree,
                                                   const std::shared_ptr<myvk::Queue> &loader_queue,
                                                   const std::shared_ptr<myvk::Queue> &main_queue) {
	std::shared_ptr<LoaderThread> ret = std::make_shared<LoaderThread>();
	ret->m_octree_ptr = octree;
	ret->m_loader_queue = loader_queue;
	ret->m_main_queue = main_queue;
	ret->m_notification = "Ready";  // 初始化notification避免空指针

	return ret;
}

void LoaderThread::Launch(const char *filename, uint32_t octree_level) {
	if (IsRunning())
		return;
	m_promise = std::promise<std::shared_ptr<OctreeBuilder>>();
	m_future = m_promise.get_future();
	m_thread = std::thread(&LoaderThread::thread_func, this, filename, octree_level);
}

bool LoaderThread::TryJoin() {
	if (!IsRunning())
		return false;

	if (m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return false;

	m_thread.join();

	m_built_builder = nullptr;
	std::shared_ptr<OctreeBuilder> builder = m_future.get();
	if (builder) {
		m_built_builder = builder; // 缓存builder供主线程使用
		std::shared_ptr<myvk::CommandPool> loader_command_pool = myvk::CommandPool::Create(m_loader_queue);
		m_main_queue->WaitIdle();
		m_octree_ptr->Update(loader_command_pool, builder);
		spdlog::info("Octree range: {} ({} MB)", m_octree_ptr->GetRange(), m_octree_ptr->GetRange() / 1000000.0f);
	}

	return true;
}

void LoaderThread::thread_func(const char *filename, uint32_t octree_level) {
	spdlog::info("Enter loader thread");
	m_notification = "";

	std::shared_ptr<myvk::Device> device = m_main_queue->GetDevicePtr();
	std::shared_ptr<myvk::CommandPool> main_command_pool = myvk::CommandPool::Create(m_main_queue);
	std::shared_ptr<myvk::CommandPool> loader_command_pool = myvk::CommandPool::Create(m_loader_queue);

	// 检测文件类型并选择不同的处理路径
	std::string extension = get_file_extension(filename);
	std::shared_ptr<OctreeBuilder> builder = nullptr;
	
	if (extension == ".vox") {
		spdlog::info("Processing .vox file: {}", filename);
		
		// .vox文件处理路径：跳过场景和体素化，直接从体素数据构建
		m_notification = "Loading .vox file";
		auto vox_data = VoxLoader::LoadVox(filename);
		
		if (vox_data && !vox_data->IsEmpty()) {
			spdlog::info("Loaded .vox file with {} voxels", vox_data->GetVoxelCount());
			
			// 创建VoxDataAdapter
			m_notification = "Creating VoxDataAdapter";
			auto vox_adapter = VoxDataAdapter::Create(*vox_data, device, loader_command_pool, octree_level);
			if (!vox_adapter) {
				spdlog::error("Failed to create VoxDataAdapter");
			} else {
				// 使用VoxDataAdapter创建OctreeBuilder
				m_notification = "Building Octree from .vox data";
				builder = OctreeBuilder::Create(vox_adapter, loader_command_pool);
				
				if (builder) {
					// 执行八叉树构建命令
					std::shared_ptr<myvk::Fence> fence = myvk::Fence::Create(device);
					std::shared_ptr<myvk::CommandBuffer> command_buffer = myvk::CommandBuffer::Create(loader_command_pool);
					command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
					
					m_notification = "Building Octree from .vox data";
					builder->CmdBuild(command_buffer);
					
					if (m_main_queue->GetFamilyIndex() != m_loader_queue->GetFamilyIndex()) {
						builder->CmdTransferOctreeOwnership(command_buffer, m_loader_queue->GetFamilyIndex(),
						                                    m_main_queue->GetFamilyIndex(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
					}
					
					command_buffer->End();
					command_buffer->Submit(fence);
					fence->Wait();
					
					spdlog::info("Octree building from .vox FINISHED");
					
					if (m_main_queue->GetFamilyIndex() != m_loader_queue->GetFamilyIndex()) {
						command_buffer = myvk::CommandBuffer::Create(main_command_pool);
						command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
						builder->CmdTransferOctreeOwnership(command_buffer, m_loader_queue->GetFamilyIndex(),
						                                    m_main_queue->GetFamilyIndex());
						command_buffer->End();
						
						fence->Reset();
						command_buffer->Submit(fence);
						fence->Wait();
					}
				} else {
					spdlog::error("Failed to create OctreeBuilder from VoxDataAdapter");
				}
			}
		} else {
			spdlog::error("Failed to load .vox file or file is empty");
		}
	} else {
		spdlog::info("Processing OBJ file: {}", filename);
		
		// 传统OBJ文件处理路径：场景 -> 体素化 -> 八叉树构建
		std::shared_ptr<Scene> scene;
		if ((scene = Scene::Create(m_loader_queue, filename, &m_notification))) {
			std::shared_ptr<Voxelizer> voxelizer = Voxelizer::Create(scene, loader_command_pool, octree_level);
			builder = OctreeBuilder::Create(voxelizer, loader_command_pool);

			std::shared_ptr<myvk::Fence> fence = myvk::Fence::Create(device);
			std::shared_ptr<myvk::QueryPool> query_pool = myvk::QueryPool::Create(device, VK_QUERY_TYPE_TIMESTAMP, 4);
			std::shared_ptr<myvk::CommandBuffer> command_buffer = myvk::CommandBuffer::Create(loader_command_pool);
			command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			command_buffer->CmdResetQueryPool(query_pool);

			command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);
			voxelizer->CmdVoxelize(command_buffer);
			command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);

			command_buffer->CmdPipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			                                   {},
			                                   {voxelizer->GetVoxelFragmentList()->GetMemoryBarrier(
			                                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)},
			                                   {});

			command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 2);
			builder->CmdBuild(command_buffer);
			command_buffer->CmdWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 3);

			if (m_main_queue->GetFamilyIndex() != m_loader_queue->GetFamilyIndex()) {
				builder->CmdTransferOctreeOwnership(command_buffer, m_loader_queue->GetFamilyIndex(),
				                                    m_main_queue->GetFamilyIndex(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
			}

			command_buffer->End();

			m_notification = "Voxelizing and Building Octree";
			spdlog::info("Voxelize and Octree building BEGIN");

			command_buffer->Submit(fence);
			fence->Wait();

			// time measurement
			uint64_t timestamps[4];
			query_pool->GetResults64(timestamps, VK_QUERY_RESULT_WAIT_BIT);
			spdlog::info("Voxelize and Octree building FINISHED in {} ms (Voxelize "
			             "{} ms, Octree building {} ms)",
			             double(timestamps[3] - timestamps[0]) * 0.000001, double(timestamps[1] - timestamps[0]) * 0.000001,
			             double(timestamps[3] - timestamps[2]) * 0.000001);

			if (m_main_queue->GetFamilyIndex() != m_loader_queue->GetFamilyIndex()) {
				command_buffer = myvk::CommandBuffer::Create(main_command_pool);
				command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
				builder->CmdTransferOctreeOwnership(command_buffer, m_loader_queue->GetFamilyIndex(),
				                                    m_main_queue->GetFamilyIndex());
				command_buffer->End();

				fence->Reset();
				command_buffer->Submit(fence);
				fence->Wait();
			}
		} else {
			spdlog::error("Failed to create Scene from OBJ file");
		}
	}

	// 公共的处理逻辑：无论是.vox还是OBJ文件，都在这里处理结果
	if (builder) {
		spdlog::info("OctreeBuilder created successfully");
		m_promise.set_value(builder);
	} else {
		spdlog::error("Failed to create OctreeBuilder");
		m_promise.set_value(nullptr);
	}

	spdlog::info("Quit loader thread");
}
