
#include <QtGui>

#include "cvpca_web.h"
#include "cvpca_gui.h"
#include "cvpca.h"
#include "ui_window.h"

#include <iostream>
#include <fstream>
#include <unordered_map>
 
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

    std::unordered_map<int, std::list<CvPCA_Item>> records;

    Lambda clear([&](){ records.clear(); w.labelCount->setNum(0); });
    QObject::connect(w.buttonClear, SIGNAL(clicked()), &clear, SLOT(call()));

    Lambda save([&](){
            std::ofstream out;
            out.open("data.txt");
            for (auto kv : records) {
                out << kv.first << std::endl;
                for (auto item : kv.second) {
                    out << (std::string)item << std::endl;
                }
            }
        });
    QObject::connect(w.buttonSave, SIGNAL(clicked()), &save, SLOT(call()));

    w.labelCount->setNum(0);

    // Set up the timer
    QTimer *timer = new QTimer(win);
    timer->start(1000);

    Lambda t([&](){
            auto q=server.get_queue();
            while (!q.empty()) {
                auto item = q.front();
                if (item.type == CvPCA_Item::CVPCA_ACCEL)
                    records[item.id].push_back(item);
                w.labelCount->setNum((int)records[item.id].size());
                q.pop();
            }

            auto conns = server.get_connections();
            w.connectionList->clear();
            for (auto c : conns) {
                QListWidgetItem *item = new QListWidgetItem(w.connectionList);
                item->setText(((std::string)c).c_str());
            }
            QString n;
            n.setNum(conns.size());
            w.connectedLabel->setText("Connected: " + n);
        });
    QObject::connect(timer, SIGNAL(timeout()), &t, SLOT(call()));

    // Editing gesture list
    int gesture_count = 0;
    Lambda add_gesture([&](){
            QListWidgetItem *item = new QListWidgetItem(w.gestureList);
            QString n;
            n.setNum(gesture_count++);
            item->setText("Gesture " + n);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        });
    QObject::connect(w.addGestureButton, SIGNAL(clicked()),
                     &add_gesture, SLOT(call()));

    Lambda remove_gesture([&](){
            QList<QListWidgetItem*> items = w.gestureList->selectedItems();
            for (int i = 0; i < items.size(); i++)
                delete items[i];
        });
    QObject::connect(w.removeGestureButton, SIGNAL(clicked()),
                     &remove_gesture, SLOT(call()));

    win->show();

    return app.exec();
}
