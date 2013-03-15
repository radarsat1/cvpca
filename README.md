# CvPCA

This program can be used to perform PCA dimensionality reduction on
accelerometer data acquired from mobile smartphone clients.

The idea is that the smartphone connects to a websocket server which
is used to collect the accelerometer data through a web page.  Therefore,
this program must be used with a mobile browser that supports
retrieval of accelerometer data.  Some examples:

- Firefox Nightly (Android)
- Safari (iPhone)

## Usage: Recording

When the program is executed, a Qt GUI appears with several panels.  The
top panel allows to specify a port and start and stop the websocket
server. After starting the server, the browser should be pointed to
the IP address of the computer running CvPCA, with the port specified.  For
example, if the computer's IP address is 192.168.0.5, and the server
was started on port 8080, the following URL should be provided to the
phone:

    http://192.168.0.5:8080

A message will be posted to the phone indicating whether the websocket
was connected successfully.  If there are issues accessing
accelerometer data, an error message will also be displayed on the
phone.

The "Connections" panel lists connected clients, which can be used to
verify that the smartphone is correctly connected to the websocket
server.

The "Gestures" panel contains an editable list of gestures tags.  These
are the instructions to the user, and can be any description of
gestures that the user should record.  For example, "Move up and
down", or "tap 3 times a second."

On pressing "Start Recording," the phone will display a gesture
instruction, and start transmitting the accelerometer data to the
server, which will be stored in memory.  When all gestures have been
recorded, press the "Stop Recording" button, and the in-memory
recording will be displayed on the screen.   Press the "Save" button
to save the data to a comma-separated value (csv) file.

## Usage: Analysis

After recording and saving data, the data will be re-loaded into the
analysis portion of CvPCA.  Alternatively, previously-saved files can
be loaded by pressing the "Load" button in the "Data Sets" panel.

Pressing the "PCA" button will extract features from the data using a
sliding window and perform Principle Component Analysis.  The first
two principle components will then be displayed in the visualization
window on the right as a 2D scatter plot.

The features calculated are based on the accelerometer data re-sampled
to 25 Hz.  A sliding window is applied after a high-pass filter,
extracting windows of 256 points, such that each window analyses the
last 10 seconds of data.  Subsequently, the log-FFT is taken giving
amplitudes of frequencies present in each accelerometer axis.  Finally,
these axes are linearly combined to provide a feature unbiased by
device orientation and gravity.

Currently, the principle components as well as the eigenvectors are
additionally printed to the console.

## Building this program

This program uses the Qt4 build system.  It also depends on
libwebsockets, an excellent minimal C websocket and HTTP library.
Finally, it uses OpenCV2 to perform PCA.

On Ubuntu Linux, you should install the packages `libopencv-ml-dev`
and `libqt4-dev`.  Then, you must download and build libwebsockets,
and finally run Qt's `qmake` program.

Since libwebsockets is included as a git submodule, simply run,

    git submodule init
    git submodule update

to download the libwebsockets source code.  Then, `cd` to the
libwebsockets directory and build it:

    cd libwebsockets
    ./autogen.sh
    ./configure && make

Finally, build CvPCA:

    cd ..
    qmake-qt4
    make

On success, there should be an executable called `cvpca` in the
current directory.
