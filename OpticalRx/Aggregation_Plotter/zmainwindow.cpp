#include "zmainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QPainter>
#include <QTextCursor>
#include <QScrollBar>
#include <QDebug>
#include <QScrollArea>

ZMainWindow::ZMainWindow(QWidget *parent)
    : QWidget(parent)
{
    //create shared ring buffer.
    m_ringBufferA=new ZRingBuffer(4096);

    //create threads.
    m_workerThreadA=new QThread(this);
    m_uartWorkerA=new ZUartWorker(m_ringBufferA);
    m_uartWorkerA->moveToThread(m_workerThreadA);
    connect(m_workerThreadA,&QThread::finished,m_uartWorkerA,&ZUartWorker::deleteLater);
    connect(m_uartWorkerA,&ZUartWorker::errorMessage,this,&ZMainWindow::onMessage);
    connect(m_uartWorkerA,&ZUartWorker::statusMessage,this,&ZMainWindow::onMessage);

    //parse thread, runs in main thread for easy UI updates.
    m_parserThreadA=new QThread;
    m_uartParserA=new ZUartParser(m_ringBufferA,this);
    m_uartParserA->moveToThread(m_parserThreadA);
    connect(m_parserThreadA,&QThread::finished,m_uartParserA,&ZUartWorker::deleteLater);
    connect(m_uartParserA,&ZUartParser::errorMessage,this,&ZMainWindow::onMessage);
    connect(m_uartParserA,&ZUartParser::statusMessage,this,&ZMainWindow::onMessage);
    connect(m_uartParserA,&ZUartParser::newImage,this,&ZMainWindow::onNewImage);

    //start UART thread.
    m_parserThreadA->start();
    m_workerThreadA->start();

    // m_timer=new QTimer;
    // connect(m_timer,&QTimer::timeout,this,&ZMainWindow::onTimeout);
    // m_timer->start(5000);
    QTimer::singleShot(5000,this,&ZMainWindow::onTimeout);
}

ZMainWindow::~ZMainWindow()
{
    if(m_timer)
    {
        m_timer->stop();
        delete m_timer;
        m_timer=nullptr;
    }
    if(this->m_workerThreadA)
    {
        if(this->m_workerThreadA->isRunning())
        {
            this->m_workerThreadA->quit();
            this->m_workerThreadA->wait();
        }
        delete this->m_workerThreadA;
        this->m_workerThreadA=nullptr;
    }

}
void ZMainWindow::onTimeout()
{
    if(!m_uartWorkerA->isOpen())
    {
        bool bOkay=QMetaObject::invokeMethod(m_uartWorkerA,"initPort",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString,"ttyUSB0"),
                                  Q_ARG(qint32, 4000000));
        qDebug()<<"bOkay="<<bOkay;
        if(!bOkay)
        {
            QTimer::singleShot(5000,this,&ZMainWindow::onTimeout);
        }
    }
}
void ZMainWindow::onMessage(const QString &message)
{
    // QTextCursor cursor=m_teLog->textCursor();
    // cursor.movePosition(QTextCursor::Start);
    // cursor.insertText(message+"\n");
    // m_teLog->verticalScrollBar()->setValue(0);
    qDebug()<<message;
}
void ZMainWindow::onNewImage(const QImage &newImage)
{
    m_image=newImage;
    //force to redraw, will call paintEvent().
    update();
}

void ZMainWindow::resizeEvent(QResizeEvent *e)
{
    if(m_uartParserA)
    {
        m_uartParserA->updateCanvasSize(size());
    }
    m_backImg=m_backImg.scaled(width(),height(),Qt::IgnoreAspectRatio);
    QWidget::resizeEvent(e);
}
void ZMainWindow::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    QPainter painter(this);
    painter.drawImage(0,0,m_backImg);
    painter.drawImage(0,0,m_image);
}