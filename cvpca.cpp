
#include <stdio.h>
#include <vector>
#include <functional>
#include <memory>
#include <opencv2/core/core_c.h>
#include <opencv2/ml/ml.hpp>
#include <bzlib.h>

#include "cvpca_gui.h"
#include "cvpca.h"

void read_line(char *line, accel_data &data)
{
    char *s = &line[2], *p;
    accel_buffer_t buf;
    int i;
    if (line[0]=='G' && line[1]==':') {
        s = strtok_r(s, ",", &p);
        if (!s) return;
        buf.timestamp = strtoull(s, 0, 10);
        s = strtok_r(0, ",", &p);
        if (!s) return;
        buf.gesture = atoi(s);
        s = strtok_r(0, ",", &p);
        if (!s) return;
        i = 0;
        while (s && i < 3) {
            buf.data[i++] = atof(s);
            s = strtok_r(0, ",", &p);
            if (!s && i < 2) return;
        }
        data.push_back(buf);
    }
    else if (line[0]=='O' && line[1]==':') {
    }
}

accel_data load_data()
{
    int er, rd;
    char buffer[1024], *s, *r, *p;
    BZFILE *bz;
    accel_data accelbuf;
    FILE *f = fopen("../data/motion_gestures_galaxytab.log.bz2", "rb");
    if (!f) {
        fprintf(stderr, "Couldn\'t open input file.\n");
        exit(1);
    }

    bz = BZ2_bzReadOpen(&er, f, 0, 0, 0, 0);
    if (!bz || er!=BZ_OK) {
        fclose(f);
        fprintf(stderr, "Not a bzip2 file.\n");
        exit(1);
    }

    s = buffer;
    while (er!=BZ_STREAM_END) {
        rd = BZ2_bzRead(&er, bz, s, 1023-(s-buffer));
        if (rd == 0 || er==BZ_STREAM_END)
            break;
        buffer[1023] = 0;
        s = strtok_r(buffer, "\r\n", &p);
        while (s!=0) {
            r = s;

            // If end of buffer crosses a line, copy it to beginning
            // and restart the loop.
            if ( s+strlen(s) == buffer+1023 ) {
                s = buffer + (buffer+1023-r);
                memcpy(buffer, r, buffer+1023-r);
                break;
            }

            read_line(s, accelbuf);
            s = strtok_r(0, "\r\n", &p);
        }
        s = buffer;
    }

    BZ2_bzReadClose(&er, bz);
    fclose(f);

    return accelbuf;
}

void do_pca(double d[][3], int size)
{
    cv::Mat mat = cv::Mat(size, 3, CV_64F, (double*)d);

    int i;

    cv::Mat hanning = cv::Mat(size, 1, CV_64F);
    for (i=0; i < size; i++) {
        hanning.at<double>(i, 0) = 0.5*(1-cos(i*2*M_PI/size));
    }

    cv::Mat input[] = {mat.col(0), cv::Mat::zeros(size, 1, CV_64F)};
    cv::Mat complexInput;
    cv::merge(input, 2, complexInput);

    cv::Mat complexOutput = cv::Mat(size, 2, CV_64F);
    cv::Mat spec = cv::Mat(size, 1, CV_64F);

    printf("from pylab import *\n\n");

    printf("a = array([");
    for (i=0; i < complexInput.rows; i++) {
        printf("%f,\n", complexInput.at<double>(i, 0));
    }
    printf("])\n");

    cv::dft(complexInput, complexOutput, cv::DFT_COMPLEX_OUTPUT);

    cv::Mat outputPlanes[2];
    split(complexOutput, outputPlanes);
    magnitude(outputPlanes[0], outputPlanes[1], spec);

    spec += cv::Scalar::all(1);
    log(spec, spec);

    printf("b = array([");
    for (i=0; i < spec.rows/2; i++) {
        printf("%f,\n", spec.at<double>(i, 0));
        //printf("%f,\n", spec.at<double>(i, 1));
    }
    printf("])\n");
    printf("subplot(311); plot(a);\n");
    printf("subplot(312); plot(log(1+abs(fft(a)[:128])));\n");
    printf("subplot(313); plot(b);\n");
    printf("show()\n");

    exit(0);

    int numpc = 2;
    cv::PCA pca(mat, cv::Mat(), CV_PCA_DATA_AS_ROW, numpc);

    cv::Mat pcs = cv::Mat_<double>(mat.rows, numpc);

    for (i=0; i < mat.rows; i++) {
        cv::Mat pc = pcs.row(i);
        pca.project(mat.row(i), pc);
    }

    for (i=0; i < pcs.rows; i++) {
        printf("%f, %f\n",
               pcs.at<double>(i,0),
               pcs.at<double>(i,1));
    }

    cv::Mat eig = pca.eigenvectors;
    for (i=0; i < numpc; i++)
        fprintf(stderr, "%f, %f, %f\n",
                eig.at<double>(i,0),
                eig.at<double>(i,1),
                eig.at<double>(i,2));
}

std::function<double(double)> highpass(double c)
{
    // Generate 2nd-order butterworth coefficients for a high-pass filter.
    // Algorithm taken from SciPy.
    const double t = tan(M_PI/2*c);
    const double a = 16*(sqrt(2))*t + 16*t*t;
    const double b0 = 16/(16 + a), b1 = -32/(16 + a), b2 = 16/(16 + a),
        a1 = -(32 - 32*t*t)/(16 + a), a2 = (16 - a)/(16 + a);
    double x0=0, x1=0, x2=0, y0=0, y1=0, y2=0;

    return [=](double x) mutable {
        x2 = x1; x1 = x0; x0 = x;
        y2 = y1; y1 = y0;
        y0 = x0*b0+x1*b1+x2*b2-y1*a1-y2*a2;
        return y0;
    };
}

std::function<void(double[3])>
sliding_window(int size, int hop,
               std::function<std::function<double(double)>(void)> h,
               std::function<void(double[][3],int)> f)
{
    typedef double window[3];
    std::shared_ptr<window> winbuf =
        std::shared_ptr<window>(new window[size*2]);
    int winpos = 0;
    std::function<double(double)> h1 = h(), h2 = h(), h3 = h();

    return [=](double d[3]) mutable {
        window *buf = winbuf.get();
        buf[winpos][0] = h1(d[0]);
        buf[winpos][1] = h2(d[1]);
        buf[winpos][2] = h3(d[2]);

        winpos = (winpos+1)%(size*2);
        if (winpos==0) {
            memcpy(buf, buf + size, size*sizeof(double));
            winpos = size;
        }

        // Pass a full window to the next stage of processing
        if (winpos >= size && (winpos-size)%hop == 0)
            f(&buf[winpos-size], size);
    };
}

void linear_interpolate(accel_data &accelbuf,
                        float freq,
                        std::function<void(double[3])> f)
{
    accel_data::iterator it1, it2 = accelbuf.begin();
    it1 = it2;
    unsigned long long t0 = it1->timestamp;
    double t = 0;
    unsigned int sample = 0;
    for (it2++; it2!=accelbuf.end(); it1++, it2++) {
        do {
            double r = (t - (it1->timestamp - t0))
                / (double)(it2->timestamp - it1->timestamp);

            double d[3];
            for (int i=0; i<3; i++)
                d[i] = it1->data[i] * (1-r) + it2->data[i] * r;

            f(d); // Pass each sample to the next stage of processing

            sample ++;
            t = sample * 1000.0/freq;
        } while (t < it2->timestamp-t0);
    }
}

int test()
{
    accel_data dat = load_data();
    linear_interpolate(dat, 25, sliding_window(256, 16,
                                               [](){return highpass(0.1);},
                                               do_pca));

    return 0;
}

int main(int argc, char *argv[])
{
    return run_gui(argc, argv);
}
