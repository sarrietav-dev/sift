QT += core gui qml quick quickcontrols2 network multimedia webenginequick sql

CONFIG += c++17 release
TARGET = sift
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/extractor.h \
    src/kokoro.h \
    src/library.h \
    src/theme.h \
    src/translator.h \
    src/ttsengine.h \
    src/windowwatcher.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/extractor.cpp \
    src/kokoro.cpp \
    src/library.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/ttsengine.cpp \
    src/windowwatcher.cpp

RESOURCES += src/resources.qrc
