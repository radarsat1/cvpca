
/* Based on libwebsockets test server example. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <queue>
#include <string>
#include <mutex>
#include <array>
#include <thread>
#include <sstream>
#include <atomic>
#include <unordered_map>

#include "cvpca_web.h"

#include "libwebsockets/lib/libwebsockets.h"

class CvPCA_Server_Impl
{
  public:
    CvPCA_Server_Impl();
    virtual ~CvPCA_Server_Impl();

    bool start(int port);
    void stop();

    bool start_recording();
    void stop_recording();

    void update_params(const CvPCA_Params&);
    const CvPCA_Params get_params();

    std::queue<CvPCA_Item> &get_queue();
    std::list<CvPCA_Connection> get_connections();

  private:
    std::unique_ptr<std::thread> server_thread;
    std::queue<CvPCA_Item> q1, q2;
    bool done;

    std::atomic<bool> recording;

    CvPCA_Params params;
    int params_rev;

    std::queue<CvPCA_Item> *read_queue;
    std::mutex read_queue_mutex;
    std::mutex send_queue_mutex;

    std::unordered_map<int, CvPCA_Connection> connections;
    std::mutex connection_list_mutex;

    enum protocols {
        PROTOCOL_HTTP = 0, /* always first */
        PROTOCOL_PHONEPCA,
        PROTOCOL_COUNT
    };

    struct libwebsocket_protocols protocols[3];

    int callback_phonepca(struct libwebsocket_context *context,
                          struct libwebsocket *wsi,
                          enum libwebsocket_callback_reasons reason,
                          void *user, void *in, size_t len);

    int callback_http(struct libwebsocket_context *context,
                      struct libwebsocket *wsi,
                      enum libwebsocket_callback_reasons reason,
                      void *in, size_t len);

    friend CvPCA_Server;
};

class CvPCA_Session
{
  public:
    CvPCA_Session(libwebsocket *_wsi)
        : recording(false)
        , params_rev(-1)
        , wsi(_wsi)
    {
        id = unique_id++;
    }
    ~CvPCA_Session();

    int get_id() { return id; }

    bool recording;
    int params_rev;

  private:
    struct libwebsocket *wsi;
    int id;
    static int unique_id;
};
int CvPCA_Session::unique_id = 0;

CvPCA_Item::operator std::string ()
{
    char str[1024];
    switch (type) {
    case CVPCA_INFO:
        sprintf(str, "I.%d %s", id, info.c_str());
        break;
    case CVPCA_ACCEL:
        sprintf(str, "G.%d %llu %f,%f,%f", id, timestamp,
                accel[0], accel[1], accel[2]);
        break;
    case CVPCA_ORIENT:
        sprintf(str, "O.%d %llu %f,%f,%f", id, timestamp,
                orient[0], orient[1], orient[2]);
        break;
    default:
        sprintf(str, "?.%d", id);
    }
    return std::string(str);
}

CvPCA_Connection::operator std::string ()
{
    std::stringstream ss;
    ss << id << " " << hostname << " " << ip << " " << info;
    return ss.str();
}

CvPCA_Params::CvPCA_Params()
    : secondsPerGesture(30)
{
}

/* Handle serving files over HTTP */
int CvPCA_Server_Impl::callback_http(struct libwebsocket_context *context,
                                     struct libwebsocket *wsi,
                                     enum libwebsocket_callback_reasons reason,
                                     void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_HTTP:
		fprintf(stderr, "serving HTTP URI %s\n", (char *)in);

		if (in && strncmp((const char*)in, "/", len) == 0) {
			if (libwebsockets_serve_http_file(context, wsi,
                                              "./cvpca.html", "text/html"))
				fprintf(stderr, "Failed to send cvpca.html\n");
			break;
		}

	default:
		break;
	}

	return 0;
}

/* phonepca_protocol */

int CvPCA_Server_Impl::callback_phonepca(struct libwebsocket_context *context,
                                         struct libwebsocket *wsi,
                                         enum libwebsocket_callback_reasons reason,
                                         void *user, void *in, size_t len)
{
	int rc;

    const size_t pre = LWS_SEND_BUFFER_PRE_PADDING;
    const size_t post = LWS_SEND_BUFFER_POST_PADDING;

    CvPCA_Session *session = static_cast<CvPCA_Session*>(user);
    CvPCA_Connection conn;

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		fprintf(stderr, "callback_phonepca: "
						 "LWS_CALLBACK_ESTABLISHED\n");

        new (session) CvPCA_Session(wsi);

        {
            int fd = libwebsocket_get_socket_fd(wsi);
            char hostname[256];
            char ip[128];
            libwebsockets_get_peer_addresses(context, wsi, fd,
                                             hostname, 256, ip, 128);

            conn.id = session->get_id();
            conn.ip = ip;
            conn.hostname = hostname;
        }

        connection_list_mutex.lock();
        connections[conn.id] = conn;
        connection_list_mutex.unlock();

        libwebsocket_callback_on_writable(context, wsi);
		break;

    case LWS_CALLBACK_CLOSED:
        connections.erase(session->get_id());
        break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
        /* Set recording state for this session */
        if (session->recording != recording)
        {
            char msg[pre + post + 32];

            len = snprintf(msg+pre, pre+post+32,
                           "{\"cmd\": \"%s\"}",
                           recording ? "start" : "stop");

            rc = libwebsocket_write(wsi, (unsigned char*)(msg+pre),
                                    len, LWS_WRITE_TEXT);
            if (rc < 0)
                fprintf(stderr, "ERROR writing to socket (phonepca)");
            else
                session->recording = recording;
        }

        /* Check if params need updating for this session */
        if (session->params_rev != params_rev)
        {
            char msg[pre + post + 256];
            std::stringstream sgestures;
            for (auto g : params.gestureList) {
                sgestures << '"' << g << "\",";
            }
            std::string gestures = sgestures.str();
            gestures[gestures.size()-1] = 0;
            len = snprintf(msg + pre, pre + post + 256,
                           "{\"cmd\": \"params\","
                           "\"secondsPerGesture\": %d,"
                           "\"gestures\": [%s]}",
                           params.secondsPerGesture,
                           gestures.c_str());
            rc = libwebsocket_write(wsi, (unsigned char*)(msg + pre),
                                    len, LWS_WRITE_TEXT);
            if (rc < 0)
                fprintf(stderr, "ERROR writing to socket (phonepca)");
            else
                session->params_rev = params_rev;
        }

		break;

	case LWS_CALLBACK_RECEIVE:
        {
            CvPCA_Item item;
            item.id = session->get_id();
            bool ok = false;

            if (len < 1)
                break;

            switch (((char*)in)[0]) {
            case 'G':
                ok = sscanf((char*)in, "G %llu %f,%f,%f",
                            &item.timestamp,
                            &item.accel[0],
                            &item.accel[1],
                            &item.accel[2]) == 4;
                item.type = CvPCA_Item::CVPCA_ACCEL;
                break;
            case 'O':
                ok = sscanf((char*)in, "G %llu %f,%f,%f",
                            &item.timestamp,
                            &item.orient[0],
                            &item.orient[1],
                            &item.orient[2]) == 4;
                item.type = CvPCA_Item::CVPCA_ORIENT;
                break;
            case 'I':
                if (len>3) {
                    ok = true;
                    item.info = std::string(&((char*)in)[2], len-2);

                    // TODO: This lock may block things, maybe better
                    // to use queue for passing connection info and
                    // keep the map on the GUI side?
                    connection_list_mutex.lock();
                    connections[session->get_id()].info = &((char*)in)[2];
                    connection_list_mutex.unlock();
                }
                item.type = CvPCA_Item::CVPCA_INFO;
                break;
            }

            if (ok)
            {
                read_queue_mutex.lock();
                read_queue->push(item);
                read_queue_mutex.unlock();
            }
            else
                printf("Error on websocket input `%s'\n", (char*)in);
        }

	default:
		break;
	}

	return 0;
}

CvPCA_Server::CvPCA_Server()
    : impl(new CvPCA_Server_Impl)
{
}

CvPCA_Server::~CvPCA_Server()
{
}


CvPCA_Server_Impl::CvPCA_Server_Impl()
    : done(true)
    , recording(false)
    , params_rev(0)
    , read_queue(&q1)
    , protocols({
      /* first protocol must always be HTTP handler */
      {
          "http-only",		/* name */
          [](struct libwebsocket_context *context,
             struct libwebsocket *wsi,
             enum libwebsocket_callback_reasons reason,
             void *, void *in, size_t len)->int
          {
              CvPCA_Server_Impl* impl =
                  (CvPCA_Server_Impl*)libwebsocket_context_user(context);
              return impl->callback_http(context, wsi, reason, in, len);
          },
          0,          /* per_session_data_size */
          0,          /* max frame size / rx buffer */
          0, 0,
      },
      {
          "phonepca",
          [](struct libwebsocket_context *context,
             struct libwebsocket *wsi,
             enum libwebsocket_callback_reasons reason,
             void *user, void *in, size_t len)->int
          {
              CvPCA_Server_Impl* impl =
                  (CvPCA_Server_Impl*)libwebsocket_context_user(context);
              return impl->callback_phonepca(context, wsi, reason,
                                             user, in, len);
          },
          sizeof(CvPCA_Session),
          1024,
          0, 0,
      },
      {
          NULL, NULL, 0, 0, 0, 0  /* End of list */
      }
      })
{
}

CvPCA_Server_Impl::~CvPCA_Server_Impl()
{
    stop();
}

bool CvPCA_Server::start(int port)
{
    return impl->start(port);
}

bool CvPCA_Server_Impl::start(int port)
{
    if (!done)
        return false;

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof info);
	info.port = port;

	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = libwebsocket_get_internal_extensions();
#endif
	info.gid = -1;
	info.uid = -1;

    info.user = this;

	libwebsocket_context *context = libwebsocket_create_context(&info);
	if (context == NULL) {
		fprintf(stderr, "libwebsocket init failed\n");
		return true;
	}

    done = false;
    server_thread = std::unique_ptr<std::thread>(new std::thread([=](){
                printf("Thread started on %d\n", port);
                while (!done) {
                    libwebsocket_service(context, 50);
                } 
                printf("Thread ended\n");
                libwebsocket_context_destroy(context);
            }));

    return false;
}

void CvPCA_Server::stop()
{
    impl->stop();
}

void CvPCA_Server_Impl::stop()
{
    done = true;
    if (server_thread) {
        server_thread->join();
        server_thread = nullptr;
        connections.clear();
    }
}

std::queue<CvPCA_Item> &CvPCA_Server::get_queue()
{
    return impl->get_queue();
}

std::queue<CvPCA_Item> &CvPCA_Server_Impl::get_queue()
{
    auto mtq = &q1;
    if (read_queue == &q1)
        mtq = &q2;
    while (!mtq->empty())
        mtq->pop();

    auto q = read_queue;
    if (read_queue == &q1) {
        read_queue_mutex.lock();
        read_queue = &q2;
        read_queue_mutex.unlock();
    } else {
        read_queue_mutex.lock();
        read_queue = &q1;
        read_queue_mutex.unlock();
    }

    return *q;
}

std::list<CvPCA_Connection> CvPCA_Server::get_connections()
{
    return impl->get_connections();
}

std::list<CvPCA_Connection> CvPCA_Server_Impl::get_connections()
{
    std::list<CvPCA_Connection> list;
    connection_list_mutex.lock();
    for (auto kv : connections)
        list.push_back(kv.second);
    connection_list_mutex.unlock();
    return list;
}

bool CvPCA_Server::start_recording()
{
    return impl->start_recording();
}

bool CvPCA_Server_Impl::start_recording()
{
    recording = true;
    if (!done)
        libwebsocket_callback_on_writable_all_protocol(&protocols[1]);
    return false;
}

void CvPCA_Server::stop_recording()
{
    impl->stop_recording();
}

void CvPCA_Server_Impl::stop_recording()
{
    recording = false;
    if (!done)
        libwebsocket_callback_on_writable_all_protocol(&protocols[1]);
}

bool CvPCA_Server::is_recording()
{
    return impl->recording;
}

void CvPCA_Server::update_params(const CvPCA_Params &params)
{
    impl->update_params(params);
}

void CvPCA_Server_Impl::update_params(const CvPCA_Params &_params)
{
    params = _params;
    params_rev ++;
    if (!done)
        libwebsocket_callback_on_writable_all_protocol(&protocols[1]);
}

const CvPCA_Params CvPCA_Server::get_params()
{
    return impl->get_params();
}

const CvPCA_Params CvPCA_Server_Impl::get_params()
{
    return params;
}
