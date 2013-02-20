
#ifndef _CVPCA_WEB_H_
#define _CVPCA_WEB_H_

#include <memory>
#include <queue>
#include <array>
#include <list>

struct CvPCA_Item
{
    int id;
    enum {
        CVPCA_INFO,
        CVPCA_ACCEL,
        CVPCA_ORIENT,
    } type;
    union {
        std::array<float,3> accel;
        std::array<float,3> orient;
    };
    std::string info;

    operator std::string ();
};

struct CvPCA_Connection
{
    int id;
    std::string ip;
    std::string hostname;
    std::string info;

    operator std::string ();
};

class CvPCA_Server_Impl;

class CvPCA_Server
{
  public:
    CvPCA_Server();
    virtual ~CvPCA_Server();

    bool start(int port);
    void stop();

    bool start_recording();
    void stop_recording();
    bool is_recording();

    std::queue<CvPCA_Item> &get_queue();

    std::list<CvPCA_Connection> get_connections();

  private:
    std::unique_ptr<CvPCA_Server_Impl> impl;
};

#endif // _CVPCA_WEB_H_
