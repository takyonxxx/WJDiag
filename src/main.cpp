#include <QApplication>
#include <QStyleFactory>
#include <QScreen>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Dark tech theme
    app.setStyle(QStyleFactory::create("Fusion"));

    // Palette: deep navy base with cyan/teal accents
    QPalette p;
    p.setColor(QPalette::Window,          QColor(12, 18, 28));    // deep navy
    p.setColor(QPalette::WindowText,      QColor(200, 210, 220)); // cool white
    p.setColor(QPalette::Base,            QColor(16, 22, 34));    // slightly lighter navy
    p.setColor(QPalette::AlternateBase,   QColor(22, 30, 44));    // row alternate
    p.setColor(QPalette::ToolTipBase,     QColor(30, 40, 56));
    p.setColor(QPalette::ToolTipText,     QColor(200, 215, 230));
    p.setColor(QPalette::Text,            QColor(200, 210, 220));
    p.setColor(QPalette::Button,          QColor(24, 34, 52));
    p.setColor(QPalette::ButtonText,      QColor(180, 200, 220));
    p.setColor(QPalette::BrightText,      QColor(0, 210, 180));   // teal accent
    p.setColor(QPalette::Link,            QColor(0, 180, 220));   // cyan links
    p.setColor(QPalette::Highlight,       QColor(0, 140, 180));   // selection
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(80, 90, 105));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(80, 90, 105));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(70, 80, 95));
    app.setPalette(p);

    app.setStyleSheet(
        // --- Global font scale for mobile ---
        "* { font-size: 13px; }"

        // --- Group boxes ---
        "QGroupBox { border: 1px solid #1a3050; border-radius: 6px; "
        "            margin-top: 12px; padding-top: 18px; "
        "            background: rgba(14,22,36,180); } "
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; "
        "                    padding: 0 6px; color: #00c8b4; "
        "                    font-weight: bold; font-size: 13px; } "

        // --- Buttons ---
        "QPushButton { padding: 8px 18px; border-radius: 5px; font-size: 13px; "
        "              background: qlineargradient(x1:0,y1:0,x2:0,y2:1, "
        "                stop:0 #1a3050, stop:1 #142440); "
        "              border: 1px solid #1a4060; color: #c0dce8; } "
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, "
        "                    stop:0 #1e3a5e, stop:1 #1a3050); "
        "                    border-color: #00a8a0; } "
        "QPushButton:pressed { background: #0c1a2c; border-color: #00d4b4; } "
        "QPushButton:disabled { background: #0e1520; color: #3a4a5a; "
        "                       border-color: #1a2030; } "

        // --- Tables ---
        "QTableWidget { gridline-color: #1a2a40; background: #0e1624; "
        "               font-size: 12px; } "
        "QHeaderView::section { background: #12203a; border: 1px solid #1a3050; "
        "                        padding: 6px; color: #60a0c0; "
        "                        font-weight: bold; font-size: 12px; } "

        // --- Combo boxes ---
        "QComboBox { padding: 6px 10px; border-radius: 4px; font-size: 13px; "
        "            background: #142440; border: 1px solid #1a4060; "
        "            color: #c0dce8; } "
        "QComboBox::drop-down { border: none; width: 24px; } "
        "QComboBox QAbstractItemView { background: #142440; "
        "           selection-background-color: #1a4060; color: #c0dce8; } "

        // --- Progress bar ---
        "QProgressBar { border: 1px solid #1a3050; border-radius: 4px; "
        "               background: #0e1624; text-align: center; "
        "               color: #60c0b0; font-size: 12px; } "
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "                      stop:0 #006858, stop:1 #00c8a0); "
        "                      border-radius: 3px; } "

        // --- Scroll bars ---
        "QScrollBar:vertical { background: #0c1420; width: 6px; border: none; } "
        "QScrollBar::handle:vertical { background: #1a3050; border-radius: 3px; "
        "                              min-height: 30px; } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "

        // --- Text edits ---
        "QTextEdit, QPlainTextEdit { background: #0a1220; color: #90b0c8; "
        "           border: 1px solid #1a2a40; border-radius: 4px; "
        "           font-size: 12px; } "
        "QLineEdit { background: #0e1828; color: #c0dce8; "
        "            border: 1px solid #1a3050; border-radius: 4px; "
        "            padding: 6px; font-size: 13px; } "

        // --- Status bar ---
        "QStatusBar { background: #0a1018; color: #4a8090; font-size: 12px; "
        "             border-top: 1px solid #1a2a3a; } "
    );

    MainWindow window;
    window.show();

    return app.exec();
}
