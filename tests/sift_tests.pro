QT += core gui sql testlib network multimedia

CONFIG += c++17 testcase
TARGET = sift_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/library.h ../src/speech.h ../src/ttsengine.h ../src/kokoro.h ../src/theme.h ../src/translator.h

SOURCES += sift_tests.cpp ../src/library.cpp ../src/speech.cpp ../src/ttsengine.cpp ../src/kokoro.cpp ../src/theme.cpp ../src/translator.cpp
