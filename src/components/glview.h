#ifndef GLVIEW_H
#define GLVIEW_H

#include<QOpenGLWidget>
#include<QOpenGLFunctions>
#include<QOpenGLFunctions_3_3_Core>
#include<QOpenGLFunctions_4_5_Core>
// #include<QOpenGLFunctions_4_1_Core>
#include<QOpenGLBuffer>
#include<QOpenGLTexture>
#include<QOpenGLShaderProgram>
#include<QKeyEvent>
#include<QPaintEvent>
#include<QStyleOption>
#include<QPainter>
#include<QMouseEvent>
#include<QFile>

// #include "opengllib_global.h"

class GLView:public QOpenGLWidget,
#ifdef PLATFORM_MAC
    public QOpenGLFunctions
#else
    public QOpenGLFunctions_4_5_Core
#endif
{
    Q_OBJECT
public:
    GLView(QWidget *parent=nullptr);
    ~GLView();
    void updatePoints(const QVector<QVector3D> &points);
    void loadCsvFile(const QString &path);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    virtual unsigned int drawMeshline(float size, int count);
    virtual void drawCooraxis(float length);
    virtual unsigned int drawPointdata(std::vector<float> &pointVertexs);

    QOpenGLShaderProgram m_shaderProgramMesh;
    QOpenGLShaderProgram m_shaderProgramAxis;
    QOpenGLShaderProgram m_shaderProgramPoint;

    unsigned int m_VBO_MeshLine;
    unsigned int m_VAO_MeshLine;

    unsigned int m_VBO_Axis;
    unsigned int m_VAO_Axis;

    unsigned int m_VBO_Point;
    unsigned int m_VAO_Point;

    std::vector<float> m_pointData;
    unsigned int m_pointCount;

    unsigned int m_vertexCount;

    float m_xRotate;
    float m_zRotate;
    float m_xTrans;
    float m_yTrans;
    float m_zoom;

    QPoint   lastPos;
private:
    // QOpenGLShaderProgram *program;
    // QOpenGLBuffer vbo;
    // QOpenGLTexture *textures[2];
    // GLfloat translate,xRot,yRot,zRot;
    // void paintEvent(QPaintEvent *event) override;
    // void updateImgSlot();
};

#endif // GLVIEW_H
