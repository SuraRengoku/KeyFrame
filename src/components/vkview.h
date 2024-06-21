#ifndef VKVIEW_H
#define VKVIEW_H

#include <QVulkanWindow>

class Renderer;

class Vkview:public QVulkanWindow{
public:
    Vkview(bool dbg);
    QVulkanWindowRenderer *createRenderer() override;

    bool isDebugEnabled() const { return debug;}
    int instanceCount() const;

public slots:
    void addNew();
    void togglePaused();
    void meshSwitched(bool enable);

private:
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

    bool debug;
    Renderer *renderer;
    bool pressed=false;
    QPoint lastPos;
};

#endif // VKVIEW_H
