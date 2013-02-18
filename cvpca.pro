
TEMPLATE = app
TARGET = 

FORMS = window.ui

*-g++* {
  QMAKE_CXXFLAGS += -std=c++0x
}
QMAKE_LIBS += -lopencv_core -lopencv_ml -lbz2 libwebsockets/lib/.libs/libwebsockets.a

# Input
HEADERS += cvpca_gui.h cvpca_web.h
SOURCES += cvpca.cpp \
           cvpca_gui.cpp \
           cvpca_web.cpp
