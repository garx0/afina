#ifndef AFINA_NETWORK_ST_NONBLOCKING_SERVER_H
#define AFINA_NETWORK_ST_NONBLOCKING_SERVER_H

#include <set>
#include <thread>
//#include <vector>

#include <afina/network/Server.h>

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace STnonblock {

// Forward declaration, see Worker.h
class Worker;

// Forward declaration, see Connection.h (my)
class Connection;

/**
 * # Network resource manager implementation
 * Epoll based server
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl);
    ~ServerImpl();

    // See Server.h
    void Start(uint16_t port, uint32_t acceptors, uint32_t workers, std::chrono::microseconds) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    void OnRun();
    void OnNewConnection(int epoll_descr, bool to_accept = true);

private:
    friend class Connection;
    std::set<Connection *> connections;

    // logger to use
    std::shared_ptr<spdlog::logger> _logger;

    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint16_t listen_port;

    // Socket to accept new connection on, shared between acceptors
    int _server_socket;

    // Curstom event "device" used to wakeup workers
    int _event_fd;

    // IO thread
    std::thread _work_thread;
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_SERVER_H
