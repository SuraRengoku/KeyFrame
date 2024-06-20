#include "glview.h"
#include<QDebug>

GLView::GLView(QWidget *parent):QOpenGLWidget(parent){
    m_xRotate = -30.0;
    m_zRotate = 100.0;
    m_xTrans = 0.0;
    m_yTrans = 0.0;
    m_zoom = 45.0;
}

GLView::~GLView(){
    makeCurrent();
    glDeleteBuffers(1, &m_VBO_MeshLine);
    glDeleteVertexArrays(1, &m_VAO_MeshLine);

    glDeleteBuffers(1, &m_VBO_Axis);
    glDeleteVertexArrays(1, &m_VAO_Axis);

    glDeleteBuffers(1, &m_VBO_Point);
    glDeleteVertexArrays(1, &m_VAO_Point);

    m_shaderProgramMesh.release();
    m_shaderProgramAxis.release();
    m_shaderProgramPoint.release();

    doneCurrent();
    qDebug() << __FUNCTION__;
}


void GLView::updatePoints(const QVector<QVector3D> &points){
    m_pointData.clear();
    for(auto vector3D : points)
    {
        m_pointData.push_back(vector3D.x());
        m_pointData.push_back(vector3D.y());
        m_pointData.push_back(vector3D.z());
        m_pointData.push_back(1);
    }
}

void GLView::loadCsvFile(const QString &path)
{
    m_pointData.clear();
    // QFile inFile(path);
    QFile inFile(path);
    if (inFile.open(QIODevice::ReadOnly))
    {
        qDebug()<<"successfully read\n";
        QTextStream stream_text(&inFile);
        while (!stream_text.atEnd())
        {
            QString line = stream_text.readLine();
            QStringList strSplit = line.split(",");

            double x = strSplit.value(0).toDouble();
            double y = strSplit.value(1).toDouble();
            double z = strSplit.value(2).toDouble();
            m_pointData.push_back(x);
            m_pointData.push_back(y);
            m_pointData.push_back(z);
            m_pointData.push_back(1);
        }
        inFile.close();
    }
}

void GLView::initializeGL(){
    loadCsvFile("E:/Downloads/marketplacefeldkirch_station1_intensity_rgb/marketplacefeldkirch_station1_intensity_rgb.csv");
    initializeOpenGLFunctions();

    // enable depth_test
    glEnable(GL_DEPTH_TEST);

    // link meshline shaders   vs文件为顶点着色器  fs为片段着色器
    m_shaderProgramMesh.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                                QString(SHADER_DIR)+"/mesh.vert");
    m_shaderProgramMesh.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                                QString(SHADER_DIR)+"/mesh.frag");
    m_shaderProgramMesh.link();

    // link coordinate axis shaders
    m_shaderProgramAxis.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                                QString(SHADER_DIR)+"/axis.vert");
    m_shaderProgramAxis.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                                QString(SHADER_DIR)+"/axis.frag");
    m_shaderProgramAxis.link();

    // link pointcloud shaders
    m_shaderProgramPoint.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                                 QString(SHADER_DIR)+"/point.vert");
    m_shaderProgramPoint.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                                 QString(SHADER_DIR)+"/point.frag");
    m_shaderProgramPoint.link();

    m_vertexCount = drawMeshline(2.0, 16);
    m_pointCount = drawPointdata(m_pointData);
    qDebug() << "point_count" << m_pointCount;
    drawCooraxis(4.0);
}

void GLView::resizeGL(int w, int h)
{
    // 设置视口大小
    glViewport(0, 0, w, h);
}

void GLView::paintGL(){
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /*
       为了将坐标从一个坐标系转换到另一个坐标系，需要用到几个转换矩阵，
       分别是模型(Model)、视图(View)、投影(Projection)三个矩阵。
    */
    QMatrix4x4 projection, view, model;
    //透视矩阵变换
    projection.perspective(m_zoom, (float)width() / (float)height(), 1.0f, 100.0f);

    // eye：摄像机位置  center：摄像机看的点位 up：摄像机上方的朝向
    view.lookAt(QVector3D(0.0, 0.0, 50.0), QVector3D(0.0, 0.0, 1.0), QVector3D(0.0, 1.0, 0.0));

    model.translate(m_xTrans, m_yTrans, 0.0);
    model.rotate(m_xRotate, 1.0, 0.0, 0.0);
    model.rotate(m_zRotate, 0.0, 0.0, 1.0);

    m_shaderProgramMesh.bind();
    m_shaderProgramMesh.setUniformValue("projection", projection);
    m_shaderProgramMesh.setUniformValue("view", view);
    m_shaderProgramMesh.setUniformValue("model", model);

    m_shaderProgramAxis.bind();
    m_shaderProgramAxis.setUniformValue("projection", projection);
    m_shaderProgramAxis.setUniformValue("view", view);
    m_shaderProgramAxis.setUniformValue("model", model);

    m_shaderProgramPoint.bind();
    m_shaderProgramPoint.setUniformValue("projection", projection);
    m_shaderProgramPoint.setUniformValue("view", view);
    m_shaderProgramPoint.setUniformValue("model", model);

    //画网格
    m_shaderProgramMesh.bind();
    glBindVertexArray(m_VAO_MeshLine);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, m_vertexCount);

    //画坐标轴
    m_shaderProgramAxis.bind();
    glBindVertexArray(m_VAO_Axis);
    glLineWidth(5.0f);
    glDrawArrays(GL_LINES, 0, 6);

    //画点云
    m_shaderProgramPoint.bind();
    glBindVertexArray(m_VAO_Point);
    glPointSize(1.0f);
    glDrawArrays(GL_POINTS, 0, m_pointCount);
}


void GLView::mousePressEvent(QMouseEvent *event){
    lastPos = event->pos();
}


void GLView::mouseMoveEvent(QMouseEvent *event){
    int dx = event->pos().x() - lastPos.x();
    int dy = event->pos().y() - lastPos.y();
    if (event->buttons() & Qt::LeftButton)
    {
        m_xRotate = m_xRotate + 0.3 * dy;
        m_zRotate = m_zRotate + 0.3 * dx;

        if (m_xRotate > 30.0f)
        {
            m_xRotate = 30.0f;
        }
        if (m_xRotate < -120.0f)
        {
            m_xRotate = -120.0f;
        }
        update();
    }
    else if (event->buttons() & Qt::MiddleButton)
    {
        m_xTrans = m_xTrans + 0.1 * dx;
        m_yTrans = m_yTrans - 0.1 * dy;
        update();
    }
    lastPos = event->pos();
}

void GLView::wheelEvent(QWheelEvent *event){
    auto scroll_offest = event->angleDelta().y() / 120;
    m_zoom = m_zoom - (float)scroll_offest;

    if (m_zoom < 1.0f)    /* 放大限制 */
    {
        m_zoom = 1.0f;
    }

    if (m_zoom > 80.0f)
    {
        m_zoom = 80.0f;
    }

    update();
}

unsigned int GLView::drawMeshline(float size, int count){
    std::vector<float> mesh_vertexs;
    unsigned int vertex_count = 0;

    float start = count * (size / 2);
    float posX = start, posZ = start;

    for (int i = 0; i <= count; ++i)
    {
        mesh_vertexs.push_back(posX);
        mesh_vertexs.push_back(start);
        mesh_vertexs.push_back(0);

        mesh_vertexs.push_back(posX);
        mesh_vertexs.push_back(-start);
        mesh_vertexs.push_back(0);

        mesh_vertexs.push_back(start);
        mesh_vertexs.push_back(posZ);
        mesh_vertexs.push_back(0);

        mesh_vertexs.push_back(-start);
        mesh_vertexs.push_back(posZ);
        mesh_vertexs.push_back(0);

        posX = posX - size;
        posZ = posZ - size;
    }

    glGenVertexArrays(1, &m_VAO_MeshLine);
    glGenBuffers(1, &m_VBO_MeshLine);

    glBindVertexArray(m_VAO_MeshLine);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_MeshLine);

    glBufferData(GL_ARRAY_BUFFER, mesh_vertexs.size() * sizeof(float), &mesh_vertexs[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    vertex_count = (int)mesh_vertexs.size() / 3;

    return vertex_count;
}

void GLView::drawCooraxis(float length){
    std::vector<float> axis_vertexs =
        {
            //x,y ,z ,r, g, b
            0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
            length, 0.0, 0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
            0.0, length, 0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 0.0, 0.0, 1.0,
            0.0, 0.0, length, 0.0, 0.0, 1.0,
        };

    glGenVertexArrays(1, &m_VAO_Axis);
    glGenBuffers(1, &m_VBO_Axis);

    glBindVertexArray(m_VAO_Axis);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Axis);
    glBufferData(GL_ARRAY_BUFFER, axis_vertexs.size() * sizeof(float), &axis_vertexs[0], GL_STATIC_DRAW);

    // 位置属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // 颜色属性
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);
}

unsigned int GLView::drawPointdata(std::vector<float> &pointVertexs){
    unsigned int point_count = 0;

    glGenVertexArrays(1, &m_VAO_Point);
    glGenBuffers(1, &m_VBO_Point);

    glBindVertexArray(m_VAO_Point);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Point);
    glBufferData(GL_ARRAY_BUFFER, pointVertexs.size() * sizeof(float), &pointVertexs[0], GL_STATIC_DRAW);

    // 位置属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // 颜色属性
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    point_count = (unsigned int)pointVertexs.size() / 4;

    return point_count;
}
