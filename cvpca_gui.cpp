
#include <QtGui>

#include "cvpca_web.h"
#include "cvpca_gui.h"
#include "cvpca.h"
#include "ui_window.h"

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <memory>

static Ui::MainWindow w;
static std::unique_ptr<QGraphicsScene> scene_ptr;

void load_default_gesture_list()
{
    // Load the previous gesture list
    try
    {
        std::ifstream file("gestures.txt");
        while (file.good()) {
            char g[256] = "";
            file.getline(g, 256);
            if (g[0] != 0) {
                auto item = new QListWidgetItem(w.gestureList);
                item->setText(g);
            }
        }
    }
    catch (...) {
        printf("exception\n");
    }
}

void store_default_gesture_list()
{
    std::ofstream file("gestures.txt");
    for (int row=0; row < w.gestureList->count(); row++)
    {
        QListWidgetItem *i = w.gestureList->item(row);
        const char *g = i->text().toAscii().constData();
        file << g << std::endl;
    }
}

std::list<std::string> get_gesture_list()
{
    std::list<std::string> gestures;
    for (int row=0; row < w.gestureList->count(); row++)
    {
        QListWidgetItem *i = w.gestureList->item(row);
        const char *g = i->text().toAscii().constData();
        gestures.push_back(g);
    }
    return gestures;
}

void get_stats(
    const std::unordered_map<int, std::list<CvPCA_Item>> &records,
    int *nAccel, int *nOrient, float *min, float *max
    )
{
    // Find the minimum and maximum of all accelerometer data
    *nAccel = 0;
    *nOrient = 0;
    *min = -logf(0); // infinities
    *max = logf(0);

    for (auto kv : records)
    {
        auto &list = kv.second;

        for (auto r : list) {
            switch (r.type) {
            case (CvPCA_Item::CVPCA_ACCEL):
                (*nAccel) ++;
                break;
            case (CvPCA_Item::CVPCA_ORIENT):
                (*nOrient) ++;
                continue;
            default:
                continue;
            }

            if (r.accel[0] > *max) *max = r.accel[0];
            if (r.accel[1] > *max) *max = r.accel[1];
            if (r.accel[2] > *max) *max = r.accel[2];

            if (r.accel[0] < *min) *min = r.accel[0];
            if (r.accel[1] < *min) *min = r.accel[1];
            if (r.accel[2] < *min) *min = r.accel[2];
        }
    }
}

std::unique_ptr<QGraphicsScene> recordingsScene(
    const std::unordered_map<int, std::list<CvPCA_Item>> &records
    )
{
    std::unique_ptr<QGraphicsScene> scene(new QGraphicsScene());
    int i = 0;

    int nAccel, nOrient;
    float min, max;
    get_stats(records, &nAccel, &nOrient, &min, &max);

    for (auto kv : records)
    {
        auto &list = kv.second;

        if (list.size() <= 0)
            continue;

        // Axes
        scene->addLine(QLine(0, i*150,     0,   i*150+100));
        scene->addLine(QLine(0, i*150+100, 300, i*150+100));

        float top = i*150;
        float w = 300;
        float h = 100;

        // Data
        int n = 0;
        float x1 = log(0), y1 = log(0);
        for (auto r : list) {
            int x = n * w / nAccel;
            int y = (r.accel[0]-min) / (max-min) * h + top;
            if (n == 0) {
                x1 = x; y1 = y;
                n ++;
                continue;
            }
            scene->addLine(QLine(x1, y1, x, y));
            x1 = x; y1 = y;
            n ++;
        }

        i ++;
    }
    return scene;
}

int run_gui(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow *win = new QMainWindow;

    w.setupUi(win);

    w.portEdit->setText("8080");

    // Set up the server
    CvPCA_Server server;
    std::unordered_map<int, std::list<CvPCA_Item>> records;

    Lambda a([&](){ printf("Start\n"); server.start(w.portEdit->text().toInt()); });
    QObject::connect(w.startButton, SIGNAL(clicked()), &a, SLOT(call()));

    Lambda b([&](){ printf("Stop\n"); server.stop(); });
    QObject::connect(w.stopButton, SIGNAL(clicked()), &b, SLOT(call()));

    Lambda c([&](){ server.start_recording(); });
    QObject::connect(w.buttonStartRecording, SIGNAL(clicked()),
                     &c, SLOT(call()));

    Lambda d([&](){ server.stop_recording();
            // Update visualization
            scene_ptr = recordingsScene(records);
            w.graphicsView->setScene(scene_ptr.get());
        });
    QObject::connect(w.buttonStopRecording, SIGNAL(clicked()),
                     &d, SLOT(call()));

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

    Lambda update_params([&](){
            QString s(w.editSecondsPerGesture->text());
            CvPCA_Params params;
            params.secondsPerGesture = s.toInt();
            params.gestureList = get_gesture_list();
            server.update_params(params);
        });
    QObject::connect(w.editSecondsPerGesture, SIGNAL(editingFinished()),
                     &update_params, SLOT(call()));

    w.editSecondsPerGesture->setText(
        QString::number(
            server.get_params().secondsPerGesture));

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

    load_default_gesture_list();
    update_params.call();

    win->show();

    int r = app.exec();

    if (r == 0)
        store_default_gesture_list();

    // Ensure Qt doesn't free the scene, or unique_ptr causes
    // double-free.
    w.graphicsView->setScene(nullptr);

    return r;
}
