QT += core gui sql testlib

CONFIG += c++17 testcase
TARGET = sift_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/library.h ../src/theme.h ../src/translator.h

SOURCES += sift_tests.cpp ../src/library.cpp ../src/theme.cpp ../src/translator.cpp
