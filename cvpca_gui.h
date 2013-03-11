
#ifndef _CVPCA_GUI_H_
#define _CVPCA_GUI_H_

int run_gui(int argc, char *argv[]);

// Until Qt5, we must provide lambda glue
// http://www.silmor.de/qtstuff.lambda.php
#include <functional>
#include <QListWidgetItem>
#include <QObject>
class Lambda:public QObject
{
  Q_OBJECT
private:
    std::function<void()>m_ptr;
public:
    Lambda(std::function<void ()>l):m_ptr(l){}
public slots:
    void call(){if(m_ptr) m_ptr();}
};

class LambdaItem:public QObject
{
  Q_OBJECT
private:
    std::function<void(QListWidgetItem*)>m_ptr;
public:
    LambdaItem(std::function<void (QListWidgetItem*)>l):m_ptr(l){}
public slots:
    void call(QListWidgetItem*item){if(m_ptr) m_ptr(item);}
};

#endif // _CVPCA_GUI_H_
