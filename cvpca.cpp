
#include <stdio.h>
#include <vector>
#include <functional>
#include <memory>
#include <opencv2/core/core_c.h>
#include <opencv2/ml/ml.hpp>
#include <bzlib.h>

#include "cvpca_gui.h"
#include "cvpca.h"

static std::list<cv::Mat> feature_list;

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

void calc_features(double d[][3], int size)
{
    cv::Mat mat = cv::Mat(size, 3, CV_64F, (double*)d);


    cv::Mat hanning = cv::Mat(size, 1, CV_64F);
    int i;
    for (i=0; i < size; i++) {
        hanning.at<double>(i, 0) = 0.5*(1-cos(i*2*M_PI/size));
    }

    cv::Mat spec[3];

    for (i=0; i<3; i++)
    {
        cv::Mat input[] = {mat.col(i), cv::Mat::zeros(size, 1, CV_64F)};
        cv::Mat complexInput;
        cv::merge(input, 2, complexInput);

        cv::Mat complexOutput = cv::Mat(size, 2, CV_64F);
        spec[i] = cv::Mat(size, 1, CV_64F);

        cv::dft(complexInput, complexOutput, cv::DFT_COMPLEX_OUTPUT);

        cv::Mat outputPlanes[2];
        split(complexOutput, outputPlanes);
        magnitude(outputPlanes[0], outputPlanes[1], spec[i]);

        // TODO: we only need the first half of spec[i]
    }

    cv::Mat mult[3];

    mult[0] = cv::Mat(size, 1, CV_64F);
    mult[1] = cv::Mat(size, 1, CV_64F);
    mult[2] = cv::Mat(size, 1, CV_64F);

    mult[0] = spec[0].mul(spec[1]) + 1;
    mult[1] = spec[0].mul(spec[2]) + 1;
    mult[2] = spec[1].mul(spec[2]) + 1;

    log(mult[0], mult[0]);
    log(mult[1], mult[1]);
    log(mult[2], mult[2]);

    auto result = (mult[0] + mult[1] + mult[2]) / 3.0;

    feature_list.push_back( result - mean(result) );
}

std::pair<cv::Mat, cv::Mat> do_pca(const cv::Mat &mat)
{
    int numpc = 2;
    cv::PCA pca(mat, cv::Mat(), CV_PCA_DATA_AS_ROW, numpc);

    cv::Mat pcs = cv::Mat_<double>(mat.rows, numpc);

    int i;
    for (i=0; i < mat.rows; i++) {
        cv::Mat pc = pcs.row(i);
        pca.project(mat.row(i), pc);
    }

    cv::Mat eig = pca.eigenvectors;

    return std::pair<cv::Mat, cv::Mat>(pcs, eig);
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
        } while (t < (it2->timestamp - t0));
    }
}

cv::Mat run_pca(accel_data &dat)
{
    printf("Calculating features...\n");

    feature_list.clear();
    linear_interpolate(dat, 25, sliding_window(256, 16,
                                               [](){return highpass(0.1);},
                                               calc_features));

    printf("Features calculated: %d\n", feature_list.size());

    if (feature_list.empty())
        return cv::Mat();

    unsigned int size = feature_list.front().rows;
    printf("Feature size: %d\n", size);

    printf("Performing PCA...\n");

    cv::Mat features = cv::Mat_<double>(feature_list.size(), size);
    unsigned int i = 0, j = 0;
    for (auto f : feature_list) {
        // TODO: why doesn't this work?
        // features.row(i) = f;

        for (j = 0; j < size; j++)
            features.at<double>(i,j) = f.at<double>(j);

        i ++;
    }

    auto result = do_pca(features);

    printf("Done.\n");

    auto pcs = result.first;
    auto eig = result.second;

    std::cout << "pcs: " << pcs << std::endl;
    std::cout << "eig: " << eig << std::endl;
    return pcs;
}

int test()
{
    accel_data dat = load_data();
    run_pca(dat);

    return 0;
}

int main(int argc, char *argv[])
{
    return run_gui(argc, argv);
}
