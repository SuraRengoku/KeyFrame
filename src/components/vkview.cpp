#include "vkview.h"
#include "renderer.h"
#include <QMouseEvent>
#include <QKeyEvent>

Vkview::Vkview(bool dbg):debug(dbg){}

QVulkanWindowRenderer *Vkview::createRenderer(){
    renderer = new Renderer(this, 128);
    return renderer;
}

void Vkview::addNew(){
    renderer->addNew();
}

void Vkview::togglePaused(){
    renderer->setAnimating(!renderer->animating());
}

void Vkview::meshSwitched(bool enable)
{
    renderer->setUseLogo(enable);
}

void Vkview::mousePressEvent(QMouseEvent *e)
{
    pressed = true;
    lastPos = e->position().toPoint();
}

void Vkview::mouseReleaseEvent(QMouseEvent *)
{
    pressed = false;
}

void Vkview::mouseMoveEvent(QMouseEvent *e)
{
    if (!pressed)
        return;

    int dx = e->position().toPoint().x() - lastPos.x();
    int dy = e->position().toPoint().y() - lastPos.y();

    if (dy)
        renderer->pitch(dy / 10.0f);

    if (dx)
        renderer->yaw(dx / 10.0f);

    lastPos = e->position().toPoint();
}

void Vkview::keyPressEvent(QKeyEvent *e)
{
    const float amount = e->modifiers().testFlag(Qt::ShiftModifier) ? 1.0f : 0.1f;
    switch (e->key()) {
    case Qt::Key_W:
        renderer->walk(amount);
        break;
    case Qt::Key_S:
        renderer->walk(-amount);
        break;
    case Qt::Key_A:
        renderer->strafe(-amount);
        break;
    case Qt::Key_D:
        renderer->strafe(amount);
        break;
    default:
        break;
    }
}

int Vkview::instanceCount() const
{
    return renderer->instanceCount();
}










