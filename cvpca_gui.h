
#ifndef _CVPCA_GUI_H_
#define _CVPCA_GUI_H_

int run_gui(int argc, char *argv[]);

// Until Qt5, we must provide lambda glue
// http://www.silmor.de/qtstuff.lambda.php
#include <functional>
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

#endif // _CVPCA_GUI_H_
