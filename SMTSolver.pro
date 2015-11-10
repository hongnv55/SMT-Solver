#-------------------------------------------------
#
# Project created by QtCreator 2015-10-21T00:08:28
#
#-------------------------------------------------

QT       += core

QT       -= gui

TARGET = SMTSolver
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += \
    main.cpp \
    minisat/core/Solver.cc \
    minisat/simp/SimpSolver.cc \
    minisat/utils/Options.cc \
    minisat/utils/System.cc \


HEADERS += \
    minisat/core/Dimacs.h \
    minisat/core/Solver.h \
    minisat/core/SolverTypes.h \
    minisat/mtl/Alg.h \
    minisat/mtl/Alloc.h \
    minisat/mtl/Heap.h \
    minisat/mtl/IntMap.h \
    minisat/mtl/IntTypes.h \
    minisat/mtl/Map.h \
    minisat/mtl/Queue.h \
    minisat/mtl/Rnd.h \
    minisat/mtl/Sort.h \
    minisat/mtl/Vec.h \
    minisat/mtl/XAlloc.h \
    minisat/simp/SimpSolver.h \
    minisat/utils/Options.h \
    minisat/utils/ParseUtils.h \
    minisat/utils/System.h \

macx: LIBS += -L$$PWD/lib/ -lz.1
INCLUDEPATH += /usr/include/
DEPENDPATH += /usr/include/
