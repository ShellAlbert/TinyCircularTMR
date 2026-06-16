#include "zuartparser.h"
#include <QPainter>
#include <QFile>
#include <QPoint>
#include <QDebug>
#include <QTextStream>
#include <QFontMetrics>
ZUartParser::ZUartParser(ZRingBuffer *buffer, QObject *parent)
    : QObject(parent), m_ringBuffer(buffer)
{
    // Poll the buffer every 10ms to check for complete frames
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &ZUartParser::parseLoop);
    m_timer.start();

    m_verbose=0;

    //only keeps 1024 history points.
    m_hisFrame=new ZHistoryFrame(1024);

    m_xScaleFactor=1.0f;
    m_yScaleFactor=1.0f;
}
ZUartParser::~ZUartParser()
{
    if(m_hisFrame)
    {
        delete m_hisFrame;
        m_hisFrame=nullptr;
    }
}
void ZUartParser::updateCanvasSize(QSize newCanvasSize)
{
    m_canvasSize=newCanvasSize;
}
void ZUartParser::parseLoop() {
    qint16 maxTMRCurrent=0;
    while (true) {
        // 1. Check if we have enough data for the minimum frame size
        // Min Frame: Header(1) + TMR-Current-High-Byte(1) + TMR-Current-Low-Byte(1) + Temperature(1) = 4 bytes
        // start process until we have 4*512=2048 frames in ringbuffer.
        if (m_ringBuffer->size() < (4*512)) {
            break;
        }

        // 2. Peek at the data to find the Header
        // We peek a small chunk to search for the header.
        // we continuously check 4 frames. 4*4=16.
        // 55 xx xx xx 55 xx xx xx 55 xx xx xx 55 xx xx xx
        QByteArray chunk = m_ringBuffer->peek(16);
        if (chunk.isEmpty() ||
            static_cast<unsigned char>(chunk.at(0)) != 0x55 ||
            static_cast<unsigned char>(chunk.at(4)) != 0x55 ||
            static_cast<unsigned char>(chunk.at(8)) != 0x55 ||
            static_cast<unsigned char>(chunk.at(12)) != 0x55) {
            // No header found at current read position, discard one byte
            m_ringBuffer->consume(1);
            continue;
        }

        //loop to process all frames.
        QPoint pt1,pt2;
        for(qint32 i=0,x_index=0;i<(512); i++,x_index++)
        {
            QByteArray baSingle=m_ringBuffer->peek(4);
            quint8 TMR_high_byte=static_cast<quint8>(baSingle.at(1));
            quint8 TMR_low_byte=static_cast<quint8>(baSingle.at(2));
            quint8 Temperature=static_cast<quint8>(baSingle.at(3));
            quint16 TMR_Current=(static_cast<quint16>(TMR_high_byte)<<8)|(static_cast<quint16>(TMR_low_byte)<<0);

            //scale Y axis.
            pt2=QPoint(x_index,TMR_Current*m_yScaleFactor);

            //record the maximum value.
            maxTMRCurrent=(TMR_Current>maxTMRCurrent)?(TMR_Current):(maxTMRCurrent);

            if(i){ //bypass 1st time.
                QLine& oldestLine=m_hisFrame->getOldest();
                oldestLine.setPoints(pt1,pt2);
                pt2=pt1;
            }
        }

        m_ringBuffer->consume(4*512);

        if(maxTMRCurrent!=m_maxY)
        {
            m_maxY=maxTMRCurrent;
            if(maxTMRCurrent>=m_canvasSize.width())
            {
                m_yScaleFactor=m_canvasSize.width()/(qreal)(maxTMRCurrent);
            }
        }

        //draw image.
        if(m_image.size()!=QSize(m_canvasSize.width()+100,m_canvasSize.height()+100))
        {
            m_image=QImage(m_canvasSize.width()+100,m_canvasSize.height()+100,QImage::Format_ARGB32); //extend +100 pixels.
        }
        m_image.fill(Qt::transparent);
        QPainter painter(&m_image);
        QList<QLine> &allLines=m_hisFrame->getAllLines();
        painter.drawLines(allLines);
        painter.end();
        emit newImage(m_image);
    }
}
