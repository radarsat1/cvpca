
#ifndef _CVPCA_H_
#define _CVPCA_H_

// Structure to hold accel data while reading it
struct accel_buffer_t
{
    unsigned long long timestamp;
    unsigned int gesture;
    double data[3];
};

typedef std::vector<accel_buffer_t> accel_data;

int test();

#endif // _CVPCA_H_
