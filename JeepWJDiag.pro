QT += core network widgets
QT += bluetooth
CONFIG += c++17
TARGET = JeepWJDiag
TEMPLATE = app
INCLUDEPATH += include

HEADERS += \
    include/elm327connection.h \
    include/kwp2000handler.h \
    include/tcmdiagnostics.h \
    include/mainwindow.h \
    include/livedata.h

SOURCES += \
    src/main.cpp \
    src/elm327connection.cpp \
    src/kwp2000handler.cpp \
    src/tcmdiagnostics.cpp \
    src/mainwindow.cpp \
    src/livedata.cpp

ios {
    QMAKE_INFO_PLIST = ios/Info.plist
}

macx {
    QMAKE_INFO_PLIST = macos/Info.plist
}

android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_MIN_SDK_VERSION = 24
    ANDROID_TARGET_SDK_VERSION = 34
}

DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/res/values/libs.xml \
    android/res/xml/qtprovider_paths.xml \
    ios/Info.plist \
    macos/Info.plist
