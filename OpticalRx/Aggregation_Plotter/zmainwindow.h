#ifndef ZMAINWINDOW_H
#define ZMAINWINDOW_H

#include <QWidget>
#include <QThread>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QToolButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QSplitter>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include "zringbuffer.h"
#include "zuartparser.h"
#include "zuartworker.h"

class ZMainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ZMainWindow(QWidget *parent = nullptr);
    ~ZMainWindow() override;

signals:
    void sendCommand(const QByteArray &command);
public slots:
    void onMessage(const QString &message);
    void onNewImage(const QImage &newImage);
    void onTimeout();
protected:
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
private:
    //for TMR Phase-A.
    ZRingBuffer *m_ringBufferA;
    //Thread-1.
    QThread *m_workerThreadA;
    ZUartWorker *m_uartWorkerA;
    //Thread-2.
    QThread *m_parserThreadA;
    ZUartParser *m_uartParserA;

    //for TMR Phase-B.
    //for TMR Phase-C.

private:

    QImage m_backImg;
    QTimer *m_timer;

    //using Reference to avoid deep-copy.
    QImage m_image;
};
#endif // ZMAINWINDOW_H
