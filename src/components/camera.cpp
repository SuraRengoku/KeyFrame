#include "camera.h"

Camera::Camera(const QVector3D &pos)
    : forward(0.0f, 0.0f, -1.0f),
    right(1.0f, 0.0f, 0.0f),
    up(0.0f, 1.0f, 0.0f),
    pos(pos),
    yawDegree(0.0f),
    pitchDegree(0.0f)
{}

static inline void clamp360(float *v){
    if (*v > 360.0f)
        *v -= 360.0f;
    if (*v < -360.0f)
        *v += 360.0f;
}

void Camera::yaw(float degree){
    yawDegree+=degree;
    clamp360(&yawDegree);
    yawMatrix.setToIdentity();
    yawMatrix.rotate(yawDegree,0,1,0);

    QMatrix4x4 rotMat=pitchMatrix*yawMatrix;
    forward = (QVector4D(0.0f, 0.0f, -1.0f, 0.0f) * rotMat).toVector3D();
    right = (QVector4D(1.0f, 0.0f, 0.0f, 0.0f) * rotMat).toVector3D();
}

void Camera::pitch(float degree){
    pitchDegree+=degree;
    clamp360(&pitchDegree);
    pitchMatrix.setToIdentity();
    pitchMatrix.rotate(pitchDegree, 1, 0, 0);

    QMatrix4x4 rotMat=pitchMatrix*yawMatrix;
    forward=(QVector4D(0.0f, 0.0f, -1.0f, 0.0f) * rotMat).toVector3D();
    up=(QVector4D(0.0f, 1.0f, 0.0f, 0.0f) * rotMat).toVector3D();
}

void Camera::walk(float amount){
    pos[0]+=amount*forward.x();
    pos[2]+=amount*forward.z();
}

void Camera::strafe(float amount){
    pos[0]+=amount*right.x();
    pos[2]+=amount*right.z();
}

QMatrix4x4 Camera::viewMatrix() const{
    QMatrix4x4 m=pitchMatrix*yawMatrix;
    m.translate(-pos);
    return m;
}

