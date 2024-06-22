#include "renderer.h"
#include "qrandom.h"
#include <QVulkanFunctions>
#include <QtConcurrentRun>
#include <QTime>

static float quadVert[] = { // Y up, front = CW
    -1, -1, 0,
    -1,  1, 0,
    1, -1, 0,
    1,  1, 0
};

#define DBG Q_UNLIKELY(vkview->isDebugEnabled())

const int MAX_INSTANCES = 16384;
const VkDeviceSize PER_INSTANCE_DATA_SIZE = 6 * sizeof(float); // instTranslate, instDiffuseAdjust

static inline VkDeviceSize aligned(VkDeviceSize v, VkDeviceSize byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

Renderer::Renderer(Vkview *w, int initialCount)
    : vkview(w),
    // Have the light positioned just behind the default camera position, looking forward.
    lightPos(0.0f, 0.0f, 25.0f),
    cam(QVector3D(0.0f, 0.0f, 20.0f)), // starting camera position
    instCount(initialCount)
{
    floorModel.translate(0, -5, 0);
    floorModel.rotate(-90, 1, 0, 0);
    floorModel.scale(20, 100, 1);

    blockMesh.load(QString(MESH_DIR)+"/block.buf");
    logoMesh.load(QString(MESH_DIR)+"/qt_logo.buf");

    QObject::connect(&frameWatcher, &QFutureWatcherBase::finished, vkview, [this] {
        if (framePending) {
            framePending = false;
            vkview->frameReady();
            vkview->requestUpdate();
        }
    });
}

void Renderer::preInitResources()
{
    const QList<int> sampleCounts = vkview->supportedSampleCounts();
    if (DBG)
        qDebug() << "Supported sample counts:" << sampleCounts;
    if (sampleCounts.contains(4)) {
        if (DBG)
            qDebug("Requesting 4x MSAA");
        vkview->setSampleCount(4);
    }
}

void Renderer::initResources()
{
    if (DBG)
        qDebug("Renderer init");

    animatingStatus = true;
    framePending = false;

    QVulkanInstance *inst = vkview->vulkanInstance();
    VkDevice dev = vkview->device();
    const VkPhysicalDeviceLimits *pdevLimits = &vkview->physicalDeviceProperties()->limits;
    const VkDeviceSize uniAlign = pdevLimits->minUniformBufferOffsetAlignment;

    devFuncs = inst->deviceFunctions(dev);

    // Note the std140 packing rules. A vec3 still has an alignment of 16,
    // while a mat3 is like 3 * vec3.
    itemMaterial.vertUniSize = aligned(2 * 64 + 48, uniAlign); // see color_phong.vert
    itemMaterial.fragUniSize = aligned(6 * 16 + 12 + 2 * 4, uniAlign); // see color_phong.frag

    if (!itemMaterial.vs.isValid())
        itemMaterial.vs.load(inst, dev, QString(SHADER_DIR)+"/color_phong_vert.spv");
    if (!itemMaterial.fs.isValid())
        itemMaterial.fs.load(inst, dev, QString(SHADER_DIR)+"/color_phong_frag.spv");
    if (!floorMaterial.vs.isValid())
        floorMaterial.vs.load(inst, dev, QString(SHADER_DIR)+"/color_vert.spv");
    if (!floorMaterial.fs.isValid())
        floorMaterial.fs.load(inst, dev, QString(SHADER_DIR)+"/color_frag.spv");

    pipelinesFuture = QtConcurrent::run(&Renderer::createPipelines, this);
}

void Renderer::createPipelines()
{
    VkDevice dev = vkview->device();

    VkPipelineCacheCreateInfo pipelineCacheInfo;
    memset(&pipelineCacheInfo, 0, sizeof(pipelineCacheInfo));
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VkResult err = devFuncs->vkCreatePipelineCache(dev, &pipelineCacheInfo, nullptr, &pipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    createItemPipeline();
    createFloorPipeline();
}

void Renderer::createItemPipeline()
{
    VkDevice dev = vkview->device();

    // Vertex layout.
    VkVertexInputBindingDescription vertexBindingDesc[] = {
        {
            0, // binding
            8 * sizeof(float),
            VK_VERTEX_INPUT_RATE_VERTEX
        },
        {
            1,
            6 * sizeof(float),
            VK_VERTEX_INPUT_RATE_INSTANCE
        }
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
        { // position
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            0 // offset
        },
        { // normal
            1,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            5 * sizeof(float)
        },
        { // instTranslate
            2,
            1,
            VK_FORMAT_R32G32B32_SFLOAT,
            0
        },
        { // instDiffuseAdjust
            3,
            1,
            VK_FORMAT_R32G32B32_SFLOAT,
            3 * sizeof(float)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = sizeof(vertexBindingDesc) / sizeof(vertexBindingDesc[0]);
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Descriptor set layout.
    VkDescriptorPoolSize descPoolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2 }
    };
    VkDescriptorPoolCreateInfo descPoolInfo;
    memset(&descPoolInfo, 0, sizeof(descPoolInfo));
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 1; // a single set is enough due to the dynamic uniform buffer
    descPoolInfo.poolSizeCount = sizeof(descPoolSizes) / sizeof(descPoolSizes[0]);
    descPoolInfo.pPoolSizes = descPoolSizes;
    VkResult err = devFuncs->vkCreateDescriptorPool(dev, &descPoolInfo, nullptr, &itemMaterial.descPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    VkDescriptorSetLayoutBinding layoutBindings[] =
        {
            {
                0, // binding
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                1, // descriptorCount
                VK_SHADER_STAGE_VERTEX_BIT,
                nullptr
            },
            {
                1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                1,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
            }
        };
    VkDescriptorSetLayoutCreateInfo descLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        sizeof(layoutBindings) / sizeof(layoutBindings[0]),
        layoutBindings
    };
    err = devFuncs->vkCreateDescriptorSetLayout(dev, &descLayoutInfo, nullptr, &itemMaterial.descSetLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    VkDescriptorSetAllocateInfo descSetAllocInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        itemMaterial.descPool,
        1,
        &itemMaterial.descSetLayout
    };
    err = devFuncs->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &itemMaterial.descSet);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate descriptor set: %d", err);

    // Graphics pipeline.
    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &itemMaterial.descSetLayout;

    err = devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &itemMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkGraphicsPipelineCreateInfo pipelineInfo;
    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            itemMaterial.vs.data()->shaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            itemMaterial.fs.data()->shaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.pInputAssemblyState = &ia;

    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = vkview->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState att;
    memset(&att, 0, sizeof(att));
    att.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = itemMaterial.pipelineLayout;
    pipelineInfo.renderPass = vkview->defaultRenderPass();

    err = devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &pipelineInfo, nullptr, &itemMaterial.pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);
}

void Renderer::createFloorPipeline()
{
    VkDevice dev = vkview->device();

    // Vertex layout.
    VkVertexInputBindingDescription vertexBindingDesc = {
        0, // binding
        3 * sizeof(float),
        VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
                                                          { // position
                                                              0, // location
                                                              0, // binding
                                                              VK_FORMAT_R32G32B32_SFLOAT,
                                                              0 // offset
                                                          },
                                                          };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Do not bother with uniform buffers and descriptors, all the data fits
    // into the spec mandated minimum of 128 bytes for push constants.
    VkPushConstantRange pcr[] = {
        // mvp
        {
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            64
        },
        // color
        {
            VK_SHADER_STAGE_FRAGMENT_BIT,
            64,
            12
        }
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = sizeof(pcr) / sizeof(pcr[0]);
    pipelineLayoutInfo.pPushConstantRanges = pcr;

    VkResult err = devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &floorMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkGraphicsPipelineCreateInfo pipelineInfo;
    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            floorMaterial.vs.data()->shaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            floorMaterial.fs.data()->shaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelineInfo.pInputAssemblyState = &ia;

    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = vkview->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState att;
    memset(&att, 0, sizeof(att));
    att.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = floorMaterial.pipelineLayout;
    pipelineInfo.renderPass = vkview->defaultRenderPass();

    err = devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &pipelineInfo, nullptr, &floorMaterial.pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);
}

void Renderer::initSwapChainResources()
{
    proj = vkview->clipCorrectionMatrix();
    const QSize sz = vkview->swapChainImageSize();
    proj.perspective(45.0f, sz.width() / (float) sz.height(), 0.01f, 1000.0f);
    markViewProjDirty();
}

void Renderer::releaseSwapChainResources()
{
    // It is important to finish the pending frame right here since this is the
    // last opportunity to act with all resources intact.
    frameWatcher.waitForFinished();
    // Cannot count on the finished() signal being emitted before returning
    // from here.
    if (framePending) {
        framePending = false;
        vkview->frameReady();
    }
}

void Renderer::releaseResources()
{
    if (DBG)
        qDebug("Renderer release");

    pipelinesFuture.waitForFinished();

    VkDevice dev = vkview->device();

    if (itemMaterial.descSetLayout) {
        devFuncs->vkDestroyDescriptorSetLayout(dev, itemMaterial.descSetLayout, nullptr);
        itemMaterial.descSetLayout = VK_NULL_HANDLE;
    }

    if (itemMaterial.descPool) {
        devFuncs->vkDestroyDescriptorPool(dev, itemMaterial.descPool, nullptr);
        itemMaterial.descPool = VK_NULL_HANDLE;
    }

    if (itemMaterial.pipeline) {
        devFuncs->vkDestroyPipeline(dev, itemMaterial.pipeline, nullptr);
        itemMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (itemMaterial.pipelineLayout) {
        devFuncs->vkDestroyPipelineLayout(dev, itemMaterial.pipelineLayout, nullptr);
        itemMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (floorMaterial.pipeline) {
        devFuncs->vkDestroyPipeline(dev, floorMaterial.pipeline, nullptr);
        floorMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (floorMaterial.pipelineLayout) {
        devFuncs->vkDestroyPipelineLayout(dev, floorMaterial.pipelineLayout, nullptr);
        floorMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelineCache) {
        devFuncs->vkDestroyPipelineCache(dev, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
    }

    if (blockVertexBuf) {
        devFuncs->vkDestroyBuffer(dev, blockVertexBuf, nullptr);
        blockVertexBuf = VK_NULL_HANDLE;
    }

    if (logoVertexBuf) {
        devFuncs->vkDestroyBuffer(dev, logoVertexBuf, nullptr);
        logoVertexBuf = VK_NULL_HANDLE;
    }

    if (floorVertexBuf) {
        devFuncs->vkDestroyBuffer(dev, floorVertexBuf, nullptr);
        floorVertexBuf = VK_NULL_HANDLE;
    }

    if (uniBuf) {
        devFuncs->vkDestroyBuffer(dev, uniBuf, nullptr);
        uniBuf = VK_NULL_HANDLE;
    }

    if (bufMem) {
        devFuncs->vkFreeMemory(dev, bufMem, nullptr);
        bufMem = VK_NULL_HANDLE;
    }

    if (instBuf) {
        devFuncs->vkDestroyBuffer(dev, instBuf, nullptr);
        instBuf = VK_NULL_HANDLE;
    }

    if (instBufMem) {
        devFuncs->vkFreeMemory(dev, instBufMem, nullptr);
        instBufMem = VK_NULL_HANDLE;
    }

    if (itemMaterial.vs.isValid()) {
        devFuncs->vkDestroyShaderModule(dev, itemMaterial.vs.data()->shaderModule, nullptr);
        itemMaterial.vs.reset();
    }
    if (itemMaterial.fs.isValid()) {
        devFuncs->vkDestroyShaderModule(dev, itemMaterial.fs.data()->shaderModule, nullptr);
        itemMaterial.fs.reset();
    }

    if (floorMaterial.vs.isValid()) {
        devFuncs->vkDestroyShaderModule(dev, floorMaterial.vs.data()->shaderModule, nullptr);
        floorMaterial.vs.reset();
    }
    if (floorMaterial.fs.isValid()) {
        devFuncs->vkDestroyShaderModule(dev, floorMaterial.fs.data()->shaderModule, nullptr);
        floorMaterial.fs.reset();
    }
}

void Renderer::ensureBuffers()
{
    if (blockVertexBuf)
        return;

    VkDevice dev = vkview->device();
    const int concurrentFrameCount = vkview->concurrentFrameCount();

    // Vertex buffer for the block.
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    const int blockMeshByteCount = blockMesh.data()->vertexCount * 8 * sizeof(float);
    bufInfo.size = blockMeshByteCount;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkResult err = devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &blockVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements blockVertMemReq;
    devFuncs->vkGetBufferMemoryRequirements(dev, blockVertexBuf, &blockVertMemReq);

    // Vertex buffer for the logo.
    const int logoMeshByteCount = logoMesh.data()->vertexCount * 8 * sizeof(float);
    bufInfo.size = logoMeshByteCount;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    err = devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &logoVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements logoVertMemReq;
    devFuncs->vkGetBufferMemoryRequirements(dev, logoVertexBuf, &logoVertMemReq);

    // Vertex buffer for the floor.
    bufInfo.size = sizeof(quadVert);
    err = devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &floorVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements floorVertMemReq;
    devFuncs->vkGetBufferMemoryRequirements(dev, floorVertexBuf, &floorVertMemReq);

    // Uniform buffer. Instead of using multiple descriptor sets, we take a
    // different approach: have a single dynamic uniform buffer and specify the
    // active-frame-specific offset at the time of binding the descriptor set.
    bufInfo.size = (itemMaterial.vertUniSize + itemMaterial.fragUniSize) * concurrentFrameCount;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    err = devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &uniBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create uniform buffer: %d", err);

    VkMemoryRequirements uniMemReq;
    devFuncs->vkGetBufferMemoryRequirements(dev, uniBuf, &uniMemReq);

    // Allocate memory for everything at once.
    VkDeviceSize logoVertStartOffset = aligned(0 + blockVertMemReq.size, logoVertMemReq.alignment);
    VkDeviceSize floorVertStartOffset = aligned(logoVertStartOffset + logoVertMemReq.size, floorVertMemReq.alignment);
    itemMaterial.uniMemStartOffset = aligned(floorVertStartOffset + floorVertMemReq.size, uniMemReq.alignment);
    VkMemoryAllocateInfo memAllocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        itemMaterial.uniMemStartOffset + uniMemReq.size,
        vkview->hostVisibleMemoryIndex()
    };
    err = devFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &bufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate memory: %d", err);

    err = devFuncs->vkBindBufferMemory(dev, blockVertexBuf, bufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = devFuncs->vkBindBufferMemory(dev, logoVertexBuf, bufMem, logoVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = devFuncs->vkBindBufferMemory(dev, floorVertexBuf, bufMem, floorVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = devFuncs->vkBindBufferMemory(dev, uniBuf, bufMem, itemMaterial.uniMemStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind uniform buffer memory: %d", err);

    // Copy vertex data.
    quint8 *p;
    err = devFuncs->vkMapMemory(dev, bufMem, 0, itemMaterial.uniMemStartOffset, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, blockMesh.data()->geom.constData(), blockMeshByteCount);
    memcpy(p + logoVertStartOffset, logoMesh.data()->geom.constData(), logoMeshByteCount);
    memcpy(p + floorVertStartOffset, quadVert, sizeof(quadVert));
    devFuncs->vkUnmapMemory(dev, bufMem);

    // Write descriptors for the uniform buffers in the vertex and fragment shaders.
    VkDescriptorBufferInfo vertUni = { uniBuf, 0, itemMaterial.vertUniSize };
    VkDescriptorBufferInfo fragUni = { uniBuf, itemMaterial.vertUniSize, itemMaterial.fragUniSize };

    VkWriteDescriptorSet descWrite[2];
    memset(descWrite, 0, sizeof(descWrite));
    descWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite[0].dstSet = itemMaterial.descSet;
    descWrite[0].dstBinding = 0;
    descWrite[0].descriptorCount = 1;
    descWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrite[0].pBufferInfo = &vertUni;

    descWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite[1].dstSet = itemMaterial.descSet;
    descWrite[1].dstBinding = 1;
    descWrite[1].descriptorCount = 1;
    descWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrite[1].pBufferInfo = &fragUni;

    devFuncs->vkUpdateDescriptorSets(dev, 2, descWrite, 0, nullptr);
}

void Renderer::ensureInstanceBuffer()
{
    if (instCount == preparedInstCount && instBuf)
        return;

    Q_ASSERT(instCount <= MAX_INSTANCES);

    VkDevice dev = vkview->device();

    // allocate only once, for the maximum instance count
    if (!instBuf) {
        VkBufferCreateInfo bufInfo;
        memset(&bufInfo, 0, sizeof(bufInfo));
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = MAX_INSTANCES * PER_INSTANCE_DATA_SIZE;
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // Keep a copy of the data since we may lose all graphics resources on
        // unexpose, and reinitializing to new random positions afterwards
        // would not be nice.
        instData.resize(bufInfo.size);

        VkResult err = devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &instBuf);
        if (err != VK_SUCCESS)
            qFatal("Failed to create instance buffer: %d", err);

        VkMemoryRequirements memReq;
        devFuncs->vkGetBufferMemoryRequirements(dev, instBuf, &memReq);
        if (DBG)
            qDebug("Allocating %u bytes for instance data", uint32_t(memReq.size));

        VkMemoryAllocateInfo memAllocInfo = {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            memReq.size,
            vkview->hostVisibleMemoryIndex()
        };
        err = devFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &instBufMem);
        if (err != VK_SUCCESS)
            qFatal("Failed to allocate memory: %d", err);

        err = devFuncs->vkBindBufferMemory(dev, instBuf, instBufMem, 0);
        if (err != VK_SUCCESS)
            qFatal("Failed to bind instance buffer memory: %d", err);
    }

    if (instCount != preparedInstCount) {
        if (DBG)
            qDebug("Preparing instances %d..%d", preparedInstCount, instCount - 1);
        char *p = instData.data();
        p += preparedInstCount * PER_INSTANCE_DATA_SIZE;
        auto gen = [](int a, int b) {
            return float(QRandomGenerator::global()->bounded(double(b - a)) + a);
        };
        for (int i = preparedInstCount; i < instCount; ++i) {
            // Apply a random translation to each instance of the mesh.
            float t[] = { gen(-5, 5), gen(-4, 6), gen(-30, 5) };
            memcpy(p, t, 12);
            // Apply a random adjustment to the diffuse color for each instance. (default is 0.7)
            float d[] = { gen(-6, 3) / 10.0f, gen(-6, 3) / 10.0f, gen(-6, 3) / 10.0f };
            memcpy(p + 12, d, 12);
            p += PER_INSTANCE_DATA_SIZE;
        }
        preparedInstCount = instCount;
    }

    quint8 *p;
    VkResult err = devFuncs->vkMapMemory(dev, instBufMem, 0, instCount * PER_INSTANCE_DATA_SIZE, 0,
                                           reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, instData.constData(), instData.size());
    devFuncs->vkUnmapMemory(dev, instBufMem);
}

void Renderer::getMatrices(QMatrix4x4 *vp, QMatrix4x4 *model, QMatrix3x3 *modelNormal, QVector3D *eyePos)
{
    model->setToIdentity();
    if (useLogo)
        model->rotate(90, 1, 0, 0);
    model->rotate(rotation, 1, 1, 0);

    *modelNormal = model->normalMatrix();

    QMatrix4x4 view = cam.viewMatrix();
    *vp = proj * view;

    *eyePos = view.inverted().column(3).toVector3D();
}

void Renderer::writeFragUni(quint8 *p, const QVector3D &eyePos)
{
    float ECCameraPosition[] = { eyePos.x(), eyePos.y(), eyePos.z() };
    memcpy(p, ECCameraPosition, 12);
    p += 16;

    // Material
    float ka[] = { 0.05f, 0.05f, 0.05f };
    memcpy(p, ka, 12);
    p += 16;

    float kd[] = { 0.7f, 0.7f, 0.7f };
    memcpy(p, kd, 12);
    p += 16;

    float ks[] = { 0.66f, 0.66f, 0.66f };
    memcpy(p, ks, 12);
    p += 16;

    // Light parameters
    float ECLightPosition[] = { lightPos.x(), lightPos.y(), lightPos.z() };
    memcpy(p, ECLightPosition, 12);
    p += 16;

    float att[] = { 1, 0, 0 };
    memcpy(p, att, 12);
    p += 16;

    float color[] = { 1.0f, 1.0f, 1.0f };
    memcpy(p, color, 12);
    p += 12; // next we have two floats which have an alignment of 4, hence 12 only

    float intensity = 0.8f;
    memcpy(p, &intensity, 4);
    p += 4;

    float specularExp = 150.0f;
    memcpy(p, &specularExp, 4);
    p += 4;
}

void Renderer::startNextFrame()
{
    // For demonstration purposes offload the command buffer generation onto a
    // worker thread and continue with the frame submission only when it has
    // finished.
    Q_ASSERT(!framePending);
    framePending = true;
    QFuture<void> future = QtConcurrent::run(&Renderer::buildFrame, this);
    frameWatcher.setFuture(future);
}

void Renderer::buildFrame()
{
    QMutexLocker locker(&guiMutex);

    ensureBuffers();
    ensureInstanceBuffer();
    pipelinesFuture.waitForFinished();

    VkCommandBuffer cb = vkview->currentCommandBuffer();
    const QSize sz = vkview->swapChainImageSize();

    VkClearColorValue clearColor = {{ 0.67f, 0.84f, 0.9f, 1.0f }};
    VkClearDepthStencilValue clearDS = { 1, 0 };
    VkClearValue clearValues[3];
    memset(clearValues, 0, sizeof(clearValues));
    clearValues[0].color = clearValues[2].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo;
    memset(&rpBeginInfo, 0, sizeof(rpBeginInfo));
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = vkview->defaultRenderPass();
    rpBeginInfo.framebuffer = vkview->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = vkview->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rpBeginInfo.pClearValues = clearValues;
    VkCommandBuffer cmdBuf = vkview->currentCommandBuffer();
    devFuncs->vkCmdBeginRenderPass(cmdBuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        0, 0,
        float(sz.width()), float(sz.height()),
        0, 1
    };
    devFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {
        { 0, 0 },
        { uint32_t(sz.width()), uint32_t(sz.height()) }
    };
    devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    buildDrawCallsForFloor();
    buildDrawCallsForItems();

    devFuncs->vkCmdEndRenderPass(cmdBuf);
}

void Renderer::buildDrawCallsForItems()
{
    VkDevice dev = vkview->device();
    VkCommandBuffer cb = vkview->currentCommandBuffer();

    devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, itemMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, useLogo ? &logoVertexBuf : &blockVertexBuf, &vbOffset);
    devFuncs->vkCmdBindVertexBuffers(cb, 1, 1, &instBuf, &vbOffset);

    // Now provide offsets so that the two dynamic buffers point to the
    // beginning of the vertex and fragment uniform data for the current frame.
    uint32_t frameUniOffset = vkview->currentFrame() * (itemMaterial.vertUniSize + itemMaterial.fragUniSize);
    uint32_t frameUniOffsets[] = { frameUniOffset, frameUniOffset };
    devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, itemMaterial.pipelineLayout, 0, 1,
                                        &itemMaterial.descSet, 2, frameUniOffsets);

    if (animatingStatus)
        rotation += 0.5;

    if (animatingStatus || vpDirty) {
        if (vpDirty)
            --vpDirty;
        QMatrix4x4 vp, model;
        QMatrix3x3 modelNormal;
        QVector3D eyePos;
        getMatrices(&vp, &model, &modelNormal, &eyePos);

        // Map the uniform data for the current frame, ignore the geometry data at
        // the beginning and the uniforms for other frames.
        quint8 *p;
        VkResult err = devFuncs->vkMapMemory(dev, bufMem,
                                               itemMaterial.uniMemStartOffset + frameUniOffset,
                                               itemMaterial.vertUniSize + itemMaterial.fragUniSize,
                                               0, reinterpret_cast<void **>(&p));
        if (err != VK_SUCCESS)
            qFatal("Failed to map memory: %d", err);

        // Vertex shader uniforms
        memcpy(p, vp.constData(), 64);
        memcpy(p + 64, model.constData(), 64);
        const float *mnp = modelNormal.constData();
        memcpy(p + 128, mnp, 12);
        memcpy(p + 128 + 16, mnp + 3, 12);
        memcpy(p + 128 + 32, mnp + 6, 12);

        // Fragment shader uniforms
        p += itemMaterial.vertUniSize;
        writeFragUni(p, eyePos);

        devFuncs->vkUnmapMemory(dev, bufMem);
    }

    devFuncs->vkCmdDraw(cb, (useLogo ? logoMesh.data() : blockMesh.data())->vertexCount, instCount, 0, 0);
}

void Renderer::buildDrawCallsForFloor()
{
    VkCommandBuffer cb = vkview->currentCommandBuffer();

    devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, floorMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &floorVertexBuf, &vbOffset);

    QMatrix4x4 mvp = proj * cam.viewMatrix() * floorModel;
    devFuncs->vkCmdPushConstants(cb, floorMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp.constData());
    float color[] = { 0.67f, 1.0f, 0.2f };
    devFuncs->vkCmdPushConstants(cb, floorMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 12, color);

    devFuncs->vkCmdDraw(cb, 4, 1, 0, 0);
}

void Renderer::addNew()
{
    QMutexLocker locker(&guiMutex);
    instCount = qMin(instCount + 16, MAX_INSTANCES);
}

void Renderer::yaw(float degrees)
{
    QMutexLocker locker(&guiMutex);
    cam.yaw(degrees);
    markViewProjDirty();
}

void Renderer::pitch(float degrees)
{
    QMutexLocker locker(&guiMutex);
    cam.pitch(degrees);
    markViewProjDirty();
}

void Renderer::walk(float amount)
{
    QMutexLocker locker(&guiMutex);
    cam.walk(amount);
    markViewProjDirty();
}

void Renderer::strafe(float amount)
{
    QMutexLocker locker(&guiMutex);
    cam.strafe(amount);
    markViewProjDirty();
}

void Renderer::setUseLogo(bool b)
{
    QMutexLocker locker(&guiMutex);
    useLogo = b;
    if (!animatingStatus)
        vkview->requestUpdate();
}











































