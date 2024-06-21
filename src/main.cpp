#include "mainwindow.h"
#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QLoggingCategory>
#include "vkview.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "KeyFrame_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    const bool dbg = qEnvironmentVariableIntValue("QT_VK_DEBUG");

    QVulkanInstance inst;

    if (dbg) {
        QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));
        inst.setLayers({ "VK_LAYER_KHRONOS_validation" });
    }

    if (!inst.create())
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());

    Vkview *vkwindow = new Vkview(dbg);
    vkwindow->setVulkanInstance(&inst);

    MainWindow mainWindow(vkwindow);
    mainWindow.show();
    return a.exec();
}
