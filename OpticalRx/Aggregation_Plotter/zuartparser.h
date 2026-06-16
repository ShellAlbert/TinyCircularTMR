#ifndef ZUARTPARSER_H
#define ZUARTPARSER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QImage>
#include <QDebug>
#include "zringbuffer.h"
#include "zsingleframe.h"
#include "zhistoryframe.h"

class ZUartParser : public QObject {
    Q_OBJECT
public:
    explicit ZUartParser(ZRingBuffer *buffer, QObject *parent = nullptr);
    ~ZUartParser();

    void updateCanvasSize(QSize newCanvasSize);
private slots:
    void parseLoop();

signals:
    void errorMessage(const QString &message);
    void statusMessage(const QString &message);
    void newImage(const QImage &image);
private:
    ZRingBuffer *m_ringBuffer;
    QTimer m_timer;

    quint8 m_verbose;

    //canvas size changed.
    //scale image to adapt to new canvas.
    QSize m_canvasSize;

    //the mimum and maximum of X and Y in single image.
    qint32 m_maxX;
    qint32 m_maxY;


    //pre-rendered static background image, only draw once.
    QImage m_image;
    qreal m_xScaleFactor;
    qreal m_yScaleFactor;

    //curve history.
    ZHistoryFrame *m_hisFrame;
};

#endif // PROTOCOLPARSER_H