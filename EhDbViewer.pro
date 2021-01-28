QT     += core gui widgets sql network
CONFIG += c++17

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += src/

SOURCES += \
    src/data/DataStore.cpp \
    src/widget/AspectRatioLabel.cpp \
    src/data/DataImporter.cpp \
    src/data/EhentaiApi.cpp \
    src/FuzzSearcher.cpp \
    src/ui/MainWindow.cpp \
    src/ui/SettingsDialog.cpp \
    src/main.cpp

HEADERS += \
    src/data/DataStore.h \
    src/data/DatabaseSchema.h \
    src/widget/AspectRatioLabel.h \
    src/data/DataImporter.h \
    src/data/EhentaiApi.h \
    src/FuzzSearcher.h \
    src/ui/MainWindow.h \
    src/widget/SearchResultItem.h \
    src/ui/SettingsDialog.h

FORMS += \
    src/ui/MainWindow.ui \
    src/ui/SettingsDialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    DatabaseDesign.md
