
#ifndef _CVPCA_H_
#define _CVPCA_H_

#include <opencv2/core/core_c.h>
#include <opencv2/ml/ml.hpp>

// Structure to hold accel data while reading it
struct accel_buffer_t
{
    unsigned long long timestamp;
    unsigned int gesture;
    double data[3];
};

typedef std::vector<accel_buffer_t> accel_data;

cv::Mat run_pca(accel_data &dat);

#endif // _CVPCA_H_
