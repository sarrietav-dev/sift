QT += core gui qml quick quickcontrols2 network multimedia webenginequick sql testlib
CONFIG += c++17 testcase
TARGET = sift_gui_smoke
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/extractor.h ../src/kokoro.h ../src/library.h ../src/theme.h ../src/translator.h ../src/ttsengine.h
SOURCES += gui_smoke.cpp ../src/backend.cpp ../src/extractor.cpp ../src/kokoro.cpp ../src/library.cpp ../src/theme.cpp ../src/translator.cpp ../src/ttsengine.cpp
RESOURCES += ../src/resources.qrc
