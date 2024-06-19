#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::KeyFrame)
{

    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}
