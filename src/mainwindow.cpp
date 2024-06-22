#include "mainwindow.h"
#include "vkview.h"
#include "./ui_mainwindow.h"
#include "glview.h"

// MainWindow::MainWindow(QWidget *parent)
//     : QMainWindow(parent)
//     , ui(new Ui::KeyFrame)
// {
//     ui->setupUi(this);
//     // GLView *glview=new GLView();
//     // this->ui->openGLWidget=glview;
// }

MainWindow::MainWindow(Vkview *vkview, QWidget *parent):
    QMainWindow(parent),
    ui(new Ui::KeyFrame)
{

    ui->setupUi(this);

    QWidget *wrapper = QWidget::createWindowContainer(vkview);
    wrapper->setFocusPolicy(Qt::StrongFocus);
    wrapper->setFocus();

    QVBoxLayout *layout = new QVBoxLayout(ui->vkcontainer);

    // vulkan
    // layout->addWidget(wrapper);

    // opengl
    // GLView *glview = new GLView();
    // layout->addWidget(glview);
}

MainWindow::~MainWindow()
{
    delete ui;
}

