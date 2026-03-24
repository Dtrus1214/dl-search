QT       += core gui multimedia concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    searchengine.cpp \
    waveformview.cpp

HEADERS += \
    diarizationengine.h \
    mainwindow.h \
    searchengine.h \
    waveformview.h

DISTFILES += \
    config/ui_config.json

# Optional embedded whisper.cpp linkage.
# Set WHISPER_CPP_DIR to your whisper.cpp build/install root that contains include/ and lib/.
WHISPER_CPP_DIR = C:\whisper.cpp\release
!isEmpty(WHISPER_CPP_DIR) {
    message(Using embedded whisper.cpp from $$WHISPER_CPP_DIR)
    INCLUDEPATH += $$WHISPER_CPP_DIR/include
    win32 {
        LIBS += -L$$WHISPER_CPP_DIR/lib -lwhisper
    } else {
        LIBS += -L$$WHISPER_CPP_DIR/lib -lwhisper
    }
    DEFINES += USE_WHISPER_EMBEDDED
} else {
    message(WHISPER_CPP_DIR not set; embedded whisper engine disabled)
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
