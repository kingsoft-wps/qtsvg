TARGET = tst_qsvgrenderer
CONFIG += testcase
QT += svg testlib widgets gui-private

SOURCES += tst_qsvgrenderer.cpp
RESOURCES += resources.qrc

TESTDATA += heart.svg heart.svgz large.svg large.svgz
