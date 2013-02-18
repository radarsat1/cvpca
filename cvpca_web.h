
#ifndef _CVPCA_WEB_H_
#define _CVPCA_WEB_H_

#include <thread>
#include <memory>
#include <atomic>
#include <queue>

class CvPCA_Server
{
  public:
    ~CvPCA_Server();

    bool start(int port);
    void stop();

    std::shared_ptr<std::queue<std::string>> get_queue();

  private:
    std::unique_ptr<std::thread> server_thread;
    std::shared_ptr<std::queue<std::string>> q1, q2;
    bool done;
};

#endif // _CVPCA_WEB_H_
