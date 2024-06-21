#include "shader.h"
#include <QtConcurrentRun>
#include <QFile>
#include <QVulkanDeviceFunctions>

Shader::Shader() {}

void Shader::load(QVulkanInstance *inst, VkDevice dev, const QString &fn){
    reset();
    maybeRunning = true;
    future=QtConcurrent::run([inst, dev, fn](){
        ShaderData sd;
        QFile infile(fn);
        if(!infile.open(QIODevice::ReadOnly)){
            qWarning("Failed to open %s", qPrintable(fn));
            return sd;
        }
        QByteArray blob = infile.readAll();
        VkShaderModuleCreateInfo shaderInfo;
        memset(&shaderInfo, 0, sizeof(shaderInfo));
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = blob.size();
        //reinterpret_cast only works when the bit mode are same
        shaderInfo.pCode = reinterpret_cast<const uint32_t *>(blob.constData());
        VkResult err = inst->deviceFunctions(dev)->vkCreateShaderModule(dev, &shaderInfo, nullptr, &sd.shaderModule);
        if(err!=VK_SUCCESS){
            qWarning("Failed to create shader module: %d", err);
            return sd;
        }
        return sd;
    });
}

ShaderData *Shader::data(){
    if(maybeRunning &&! shaderData.isValid()) shaderData = future.result();
    return &shaderData;
}

void Shader::reset(){
    *data() = ShaderData();
    maybeRunning = false;
}
