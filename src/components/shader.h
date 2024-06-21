#ifndef SHADER_H
#define SHADER_H

#include <QVulkanInstance>
#include <QFuture>

struct ShaderData{
    bool isValid() const{return shaderModule!=VK_NULL_HANDLE;}
    VkShaderModule shaderModule=VK_NULL_HANDLE;
};

class Shader
{
public:
    Shader();
    void load(QVulkanInstance *inst, VkDevice dev, const QString &fn);
    ShaderData *data();
    bool isValid() {return data()->isValid();}
    void reset();
private:
    bool maybeRunning=false;
    QFuture<ShaderData> future;
    ShaderData shaderData;
};

#endif // SHADER_H
