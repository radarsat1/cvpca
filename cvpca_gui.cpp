
#include <QtGui>

#include "cvpca_web.h"
#include "cvpca_gui.h"
#include "cvpca.h"
#include "ui_window.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <memory>

static Ui::MainWindow w;
static std::map<std::string, std::shared_ptr<QGraphicsScene>> scene_ptrs;
static accel_data g_accel_data;

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

void get_stats(const accel_data &data, int *nAccel,
               float *min, float *max)
{
    // Find the minimum and maximum of all accelerometer data
    *nAccel = data.size();
    *min = -logf(0); // infinities
    *max = logf(0);

    for (auto d : data)
    {
        if (d.data[0] > *max) *max = d.data[0];
        if (d.data[1] > *max) *max = d.data[1];
        if (d.data[2] > *max) *max = d.data[2];

        if (d.data[0] < *min) *min = d.data[0];
        if (d.data[1] < *min) *min = d.data[1];
        if (d.data[2] < *min) *min = d.data[2];
    }
}

std::shared_ptr<QGraphicsScene> recordingsScene(
    const std::unordered_map<int, std::list<CvPCA_Item>> &records
    )
{
    std::shared_ptr<QGraphicsScene> scene(new QGraphicsScene());
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

std::shared_ptr<QGraphicsScene> acceldataScene(const accel_data &data)
{
    std::shared_ptr<QGraphicsScene> scene(new QGraphicsScene());
    int i = 0;

    int nAccel;
    float min, max;
    get_stats(data, &nAccel, &min, &max);

    // Axes
    scene->addLine(QLine(0, i*150,     0,   i*150+100));
    scene->addLine(QLine(0, i*150+100, 300, i*150+100));

    float top = i*150;
    float w = 300;
    float h = 100;

    // Data
    int n = 0;
    float x1 = log(0), y1 = log(0);
    for (auto d : data)
    {
        int x = n * w / nAccel;
        int y = (d.data[0]-min) / (max-min) * h + top;
        if (n == 0) {
            x1 = x; y1 = y;
            n ++;
            continue;
        }
        scene->addLine(QLine(x1, y1, x, y));
        x1 = x; y1 = y;
        n ++;
    }

    return scene;
}

std::shared_ptr<QGraphicsScene> pcScene(cv::Mat pcs)
{
    std::shared_ptr<QGraphicsScene> scene(new QGraphicsScene());

    double minX, maxX, minY, maxY;

    minMaxIdx(pcs.col(0), &minX, &maxX);
    minMaxIdx(pcs.col(1), &minY, &maxY);

    for (int r = 0; r < pcs.rows; r++)
    {
        double x = pcs.at<double>(r,0);
        double y = pcs.at<double>(r,1);
        scene->addRect(x*300/(maxX-minX),
                       y*300/(minY-maxY),
                       5, 5,
                       QPen(QColor(255,0,0)));
    }

    return scene;
}

void add_and_show_scene(const std::string &name,
                        const std::shared_ptr<QGraphicsScene> &scene)
{
    scene_ptrs[name] = scene;
    int i = w.vizModeComboBox->findText(name.c_str());
    if (i == -1) {
        w.vizModeComboBox->addItem(name.c_str());
        i = w.vizModeComboBox->findText(name.c_str());
        w.vizModeComboBox->setCurrentIndex(i);
    }
    else
        w.vizModeComboBox->setCurrentIndex(i);
}

accel_data load_dataset(const char *filename)
{
    std::cout << "Reading `" << filename << "'" << std::endl;

    accel_data data;

    std::ifstream in;
    in.open(filename);

    std::string line, field;
    accel_buffer_t d;
    int id = -1;
    while (std::getline(in, line)) {
        memset(&d, 0, sizeof(d));
        std::stringstream ss(line);
        int i = 0;
        while (std::getline(ss, field, ',')) {
            if (i==0) {
                if (atoi(field.c_str()) == CvPCA_Item::CVPCA_ACCEL)
                    i++;
                else
                    break;
            }
            else if (i==1) {
                if (id == -1) id = atoi(field.c_str());
                else if (id != atoi(field.c_str()))
                    goto done; // only load the first device id for now
                i++;
            }
            else if (i==2) {
                d.timestamp = atoll(field.c_str());
                i++;
            }
            else if (i==3) {
                d.gesture = atoi(field.c_str());
                i++;
            }
            else if (i > 3 && i < 7) {
                d.data[i-4] = atof(field.c_str());
                i++;
                if (i == 7)
                    data.push_back(d);
            }
        }
    }

done:
    return data;
}

void save_dataset(const char *filename,
                  const std::unordered_map<int, std::list<CvPCA_Item>> &records)
{
    std::cout << "Writing `" << filename << "'" << std::endl;

    std::stringstream infostr(";");

    for (auto kv : records) {
        for (auto item : kv.second) {
            if (item.type == CvPCA_Item::CVPCA_INFO) {
                infostr << " " << item.info;
                printf("got one infostr\n");
            }
        }
    }

    std::ofstream out;
    out.open(filename);

    if (infostr.str().size() > 1) {
        out << infostr << std::endl;
    }

    for (auto kv : records) {
        for (auto item : kv.second) {
            switch (item.type) {
            case CvPCA_Item::CVPCA_ACCEL:
                out << item.type
                    << "," << item.id
                    << "," << item.timestamp
                    << "," << item.gesture
                    << "," << item.accel[0]
                    << "," << item.accel[1]
                    << "," << item.accel[2]
                    << std::endl;
                break;
            case CvPCA_Item::CVPCA_ORIENT:
                out << item.type
                    << "," << item.id
                    << "," << item.timestamp
                    << "," << item.gesture
                    << "," << item.orient[0]
                    << "," << item.orient[1]
                    << "," << item.orient[2]
                    << std::endl;
                break;
            default:
                break;
            }
        }
    }
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
            add_and_show_scene("Recording", recordingsScene(records));
        });
    QObject::connect(w.buttonStopRecording, SIGNAL(clicked()),
                     &d, SLOT(call()));

    Lambda clear([&](){ records.clear(); w.labelCount->setNum(0); });
    QObject::connect(w.buttonClear, SIGNAL(clicked()), &clear, SLOT(call()));

    Lambda load([&](){
            QString fileName = QFileDialog::getOpenFileName(win, "Load Data Set",
                                                            nullptr, "*.csv");
            if (!fileName.isEmpty()) {
                g_accel_data = load_dataset(fileName.toAscii().data());
                printf("Loaded %d accel items.\n", g_accel_data.size());

                add_and_show_scene("Accel Data", acceldataScene(g_accel_data));

                w.currentDataset->setText(fileName);
                w.buttonPCA->setEnabled(true);
            }
        });

    LambdaItem loadds([&](QListWidgetItem* item){
            QString fileName = item->text();
            if (!fileName.isEmpty()) {
                g_accel_data = load_dataset(fileName.toAscii().data());
                printf("Loaded %d accel items.\n", g_accel_data.size());

                add_and_show_scene("Accel Data", acceldataScene(g_accel_data));

                w.currentDataset->setText(fileName);
                w.buttonPCA->setEnabled(true);
            }
        });

    Lambda save([&](){
            QString fileName = QFileDialog::getSaveFileName(win, "Save Data Set",
                                                            nullptr, "*.csv");
            if (!fileName.isEmpty()) {
                save_dataset(fileName.toAscii().data(), records);
                w.datasetList->addItem(fileName);

                g_accel_data = load_dataset(fileName.toAscii().data());
                w.currentDataset->setText(fileName);
                w.buttonPCA->setEnabled(true);
            }
        });

    QObject::connect(w.buttonLoad, SIGNAL(clicked()), &load, SLOT(call()));
    QObject::connect(w.buttonSave, SIGNAL(clicked()), &save, SLOT(call()));
    QObject::connect(w.datasetList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), &loadds, SLOT(call(QListWidgetItem*)));

    Lambda pca([&](){
            auto pcs = run_pca(g_accel_data);
            if (pcs.empty())
                return;

            // Update visualization
            add_and_show_scene("Principle Components", pcScene(pcs));
        });

    QObject::connect(w.buttonPCA, SIGNAL(clicked()), &pca, SLOT(call()));

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

    Lambda changeScene([&](){
            const QString &name = w.vizModeComboBox->currentText();
            auto scene = scene_ptrs.find(name.toAscii().data());
            if (scene != scene_ptrs.end())
                w.graphicsView->setScene(scene->second.get());
        });
    QObject::connect(w.vizModeComboBox, SIGNAL(currentIndexChanged(int)),
                     &changeScene, SLOT(call()));

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
