#ifndef ZHISTORYFRAME_H
#define ZHISTORYFRAME_H
#include <QList>
#include <QLine>

class ZHistoryFrame
{
public:
    //only keeps the latest 10 single frames.
    ZHistoryFrame(int capacity=1024);
    ~ZHistoryFrame();

    QLine& getOldest();
    QLine& getFrameAt(qint32 i);
    qint32 count() const;

    QList<QLine> &getAllLines() const;
private:
    QList<QLine> *m_list;
    int m_capacity;
    int m_index;

    QLine m_invalidLine;
};

#endif // ZHISTORYFRAME_H
