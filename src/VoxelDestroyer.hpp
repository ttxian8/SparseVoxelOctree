#ifndef VOXEL_DESTROYER_HPP
#define VOXEL_DESTROYER_HPP

#include "Camera.hpp"
#include "Octree.hpp"
#include "myvk/Buffer.hpp"
#include "myvk/ComputePipeline.hpp"
#include "myvk/DescriptorSet.hpp"
#include <glm/glm.hpp>

struct GLFWwindow;

class VoxelDestroyer {
private:
    std::shared_ptr<Octree> m_octree_ptr;
    std::shared_ptr<Camera> m_camera_ptr;
    
    std::shared_ptr<myvk::PipelineLayout> m_pipeline_layout;
    std::shared_ptr<myvk::ComputePipeline> m_destroy_pipeline;
    
    std::shared_ptr<myvk::DescriptorPool> m_descriptor_pool;
    std::shared_ptr<myvk::DescriptorSetLayout> m_descriptor_set_layout;
    std::shared_ptr<myvk::DescriptorSet> m_descriptor_set;
    
    std::shared_ptr<myvk::Buffer> m_ray_buffer;
    
    uint32_t m_screen_width{1920}, m_screen_height{1080};
    
    void create_descriptors(const std::shared_ptr<myvk::Device> &device);
    void create_pipeline(const std::shared_ptr<myvk::Device> &device);
    void create_buffers(const std::shared_ptr<myvk::Device> &device);
    
    glm::vec3 screen_to_world_ray(float screen_x, float screen_y) const;

public:
    static std::shared_ptr<VoxelDestroyer> Create(
        const std::shared_ptr<Octree> &octree,
        const std::shared_ptr<Camera> &camera,
        const std::shared_ptr<myvk::Device> &device
    );
    
    void HandleInput(GLFWwindow *window);
    void DestroyVoxelAtCursor(const std::shared_ptr<myvk::CommandBuffer> &command_buffer, 
                             float cursor_x, float cursor_y, uint32_t current_frame);
    
    void SetScreenSize(uint32_t width, uint32_t height) {
        m_screen_width = width;
        m_screen_height = height;
    }
};

#endif
