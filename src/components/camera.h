#ifndef CAMERA_H
#define CAMERA_H

#include <QVector3D>
#include <QMatrix4x4>

class Camera
{
public:
    Camera(const QVector3D &pos);

    void yaw(float degrees);
    void pitch(float degrees);
    void walk(float amount);
    void strafe(float amount);
    QMatrix4x4 viewMatrix() const;

private:
    QVector3D forward;
    QVector3D right;
    QVector3D up;
    QVector3D pos;
    float yawDegree;
    float pitchDegree;
    QMatrix4x4 yawMatrix;
    QMatrix4x4 pitchMatrix;
};

#endif // CAMERA_H
