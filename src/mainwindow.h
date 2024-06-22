#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QBoxLayout>
#include <QPushButton>
#include <QWidget>

QT_BEGIN_NAMESPACE

class Vkview;

namespace Ui {
class KeyFrame;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Vkview *vkview, QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::KeyFrame *ui;
};
#endif // MAINWINDOW_H



