#include "zhistoryframe.h"

//only keeps the latest 10 single frames, each frame has maximum 700(1050nm-350nm) lines.
ZHistoryFrame::ZHistoryFrame(int capacity):
    m_capacity(capacity),m_index(0)
{
    m_list=new QList<QLine>;
    for(qint32 i=0;i<capacity;i++)
    {
        m_list->append(QLine(0,0,0,0));
    }
}
ZHistoryFrame::~ZHistoryFrame()
{
    if(m_list)
    {
        m_list->clear();
        delete m_list;
        m_list=nullptr;
    }
}
QLine& ZHistoryFrame::getOldest()
{
    QLine& line=(*m_list)[m_index];
    //move read pointer circularly.
    m_index=(m_index+1)%m_capacity;
    return line;
}
QLine& ZHistoryFrame::getFrameAt(qint32 i)
{
    if(i>=0 && i<m_capacity)
    {
        return (*m_list)[i];
    }
    return m_invalidLine;
}
qint32 ZHistoryFrame::count() const
{
    return m_list->size();
}
 QList<QLine>& ZHistoryFrame::getAllLines() const
{
     return (*m_list);
}