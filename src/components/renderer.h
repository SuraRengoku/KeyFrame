#ifndef RENDERER_H
#define RENDERER_H

#include "vkview.h"
#include "mesh.h"
#include "shader.h"
#include "camera.h"
#include <QFutureWatcher>
#include <QMutex>

class Renderer:public QVulkanWindowRenderer
{
public:
    Renderer(Vkview *w,int initialCount);
    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

    bool animating() const {return animatingStatus;}
    void setAnimating(bool a) {animatingStatus=a;}

    int instanceCount() const { return instCount;}
    void addNew();

    void yaw(float degrees);
    void pitch(float degrees);
    void walk(float amount);
    void strafe(float amount);

    void setUseLogo(bool b);

private:
    void createPipelines();
    void createItemPipeline();
    void createFloorPipeline();
    void ensureBuffers();
    void ensureInstanceBuffer();
    void getMatrices(QMatrix4x4 *mvp, QMatrix4x4 *model, QMatrix3x3 *modelNormal, QVector3D *eyePos);
    void writeFragUni(quint8 *p, const QVector3D &eyePos);
    void buildFrame();
    void buildDrawCallsForItems();
    void buildDrawCallsForFloor();

    void markViewProjDirty(){vpDirty=vkview->concurrentFrameCount();}

    Vkview *vkview;
    QVulkanDeviceFunctions *devFuncs;

    bool useLogo=false;
    Mesh blockMesh;
    Mesh logoMesh;
    VkBuffer blockVertexBuf=VK_NULL_HANDLE;
    VkBuffer logoVertexBuf=VK_NULL_HANDLE;
    struct{
        VkDeviceSize vertUniSize;
        VkDeviceSize fragUniSize;
        VkDeviceSize uniMemStartOffset;
        Shader vs;
        Shader fs;
        VkDescriptorPool descPool=VK_NULL_HANDLE;
        VkDescriptorSetLayout descSetLayout=VK_NULL_HANDLE;
        VkDescriptorSet descSet;
        VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
        VkPipeline pipeline=VK_NULL_HANDLE;
    }itemMaterial;

    VkBuffer floorVertexBuf=VK_NULL_HANDLE;
    struct{
        Shader vs;
        Shader fs;
        VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
        VkPipeline pipeline=VK_NULL_HANDLE;
    }floorMaterial;

    VkDeviceMemory bufMem=VK_NULL_HANDLE;
    VkBuffer uniBuf=VK_NULL_HANDLE;
    VkPipelineCache pipelineCache=VK_NULL_HANDLE;
    QFuture<void> pipelinesFuture;

    QVector3D lightPos;
    Camera cam;

    QMatrix4x4 proj;
    int vpDirty=0;
    QMatrix4x4 floorModel;

    bool animatingStatus;
    float rotation=0.0f;

    int instCount;
    int preparedInstCount=0;
    QByteArray instData;
    VkBuffer instBuf=VK_NULL_HANDLE;
    VkDeviceMemory instBufMem=VK_NULL_HANDLE;

    QFutureWatcher<void> frameWatcher;
    bool framePending;

    QMutex guiMutex;
};

#endif // RENDERER_H
