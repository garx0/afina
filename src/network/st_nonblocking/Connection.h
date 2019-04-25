#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <memory>
#include <string>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace STnonblock {

// Forward declaration, see ServerImpl.h
class ServerImpl;

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> plogger)
        : pStorage(ps), _socket(s), _logger(plogger) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    inline bool isAlive() const { return _is_alive; }

    void Start();

protected:
    /**
     * Instance of backing storeage on which current server should execute
     * each command
     */
    std::shared_ptr<Afina::Storage> pStorage;

    /**
     * Logging service to be used in order to report application progress
     */

    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    bool _is_alive;

    // If failed writing to socket.
    // For avoiding endless loop of writing fails and writing of messages about writing fails
    bool _failed_to_write;

    std::shared_ptr<spdlog::logger> _logger;

    int _readed_bytes;
    char _rbuffer[4096];
    std::size_t _arg_remains;
    Protocol::Parser _parser;
    std::string _arg_for_cmd;
    std::unique_ptr<Execute::Command> _cmd_to_exec;

    int _written_bytes;
    char _wbuffer[4096];
    std::size_t _wbuf_len;
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
