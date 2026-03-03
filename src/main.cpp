#include <QApplication>
#include <QStyleFactory>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Koyu tema
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window,          QColor(30, 30, 40));
    darkPalette.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Base,            QColor(25, 25, 35));
    darkPalette.setColor(QPalette::AlternateBase,   QColor(35, 35, 50));
    darkPalette.setColor(QPalette::ToolTipBase,     QColor(220, 220, 220));
    darkPalette.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Text,            QColor(220, 220, 220));
    darkPalette.setColor(QPalette::Button,          QColor(45, 45, 60));
    darkPalette.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    darkPalette.setColor(QPalette::BrightText,      QColor(255, 50, 50));
    darkPalette.setColor(QPalette::Link,            QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));

    app.setPalette(darkPalette);

    app.setStyleSheet(
        "QGroupBox { border: 1px solid #555; border-radius: 5px; "
        "            margin-top: 10px; padding-top: 15px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; "
        "                    padding: 0 5px; color: #88aaff; } "
        "QPushButton { padding: 6px 16px; border-radius: 3px; "
        "              background: #3a3a55; border: 1px solid #555; } "
        "QPushButton:hover { background: #4a4a70; } "
        "QPushButton:pressed { background: #2a2a40; } "
        "QPushButton:disabled { background: #252535; color: #666; } "
        "QTableWidget { gridline-color: #444; } "
        "QHeaderView::section { background: #2a2a3a; border: 1px solid #444; "
        "                        padding: 4px; } "
        "QProgressBar { border: 1px solid #555; border-radius: 3px; "
        "               background: #252535; text-align: center; } "
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "                      stop:0 #2a8a2a, stop:1 #44cc44); } "
    );

    MainWindow window;
    window.show();

    return app.exec();
}
