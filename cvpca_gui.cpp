
#include <QtGui>

#include "cvpca_web.h"
#include "cvpca_gui.h"
#include "cvpca.h"
#include "ui_window.h"

#include <iostream>
 
int run_gui(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow *win = new QMainWindow;

    Ui::MainWindow w;
    w.setupUi(win);

    w.portEdit->setText("8080");

    // Set up the server
    CvPCA_Server server;

    Lambda a([&](){ printf("Start\n"); server.start(w.portEdit->text().toInt()); });
    QObject::connect(w.startButton, SIGNAL(clicked()), &a, SLOT(call()));

    Lambda b([&](){ printf("Stop\n"); server.stop(); });
    QObject::connect(w.stopButton, SIGNAL(clicked()), &b, SLOT(call()));

    Lambda c([&](){ server.start_recording(); });
    QObject::connect(w.buttonStartRecording, SIGNAL(clicked()),
                     &c, SLOT(call()));

    Lambda d([&](){ server.stop_recording(); });
    QObject::connect(w.buttonStopRecording, SIGNAL(clicked()),
                     &d, SLOT(call()));

    w.labelCount->setText("0");

    // Set up the timer
    QTimer *timer = new QTimer(win);
    timer->start(1000);

    Lambda t([&](){
            auto q=server.get_queue();
            if (!q.empty())
                std::cout << "get_queue: " << q.size() << std::endl;
            while (!q.empty()) {
                auto i = q.front();
                std::cout << (std::string)i << std::endl;
                q.pop();
            }
        });
    QObject::connect(timer, SIGNAL(timeout()), &t, SLOT(call()));

    win->show();

    return app.exec();
}
