
#ifndef _CVPCA_WEB_H_
#define _CVPCA_WEB_H_

#include <thread>
#include <memory>
#include <atomic>
#include <queue>

class CvPCA_Server_Impl;

class CvPCA_Server
{
  public:
    CvPCA_Server();
    virtual ~CvPCA_Server();

    bool start(int port);
    void stop();

    std::queue<std::string> &get_queue();

  private:
    std::unique_ptr<CvPCA_Server_Impl> impl;
};

#endif // _CVPCA_WEB_H_
