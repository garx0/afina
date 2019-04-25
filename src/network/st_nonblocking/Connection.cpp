#include "Connection.h"

#include <iostream>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {
    //    std::cout << "Start" << std::endl;
    _is_alive = true;
    _shutdown = false;
    _readed_bytes = -1;
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
}

// See Connection.h
void Connection::OnError() {
    //    std::cout << "OnError" << std::endl;
    _is_alive = false;
    _shutdown = true;
    _event.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    shutdown(_socket, SHUT_RD);
}

// See Connection.h
void Connection::OnClose() {
    //    std::cout << "OnClose" << std::endl;
    _is_alive = false;
    _shutdown = true;
    _event.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    shutdown(_socket, SHUT_RD);
}

// See Connection.h
void Connection::OnStop() {
    //    std::cout << "OnStop" << std::endl;
    _shutdown = true;
    _event.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    shutdown(_socket, SHUT_RD);
}

// See Connection.h
void Connection::DoRead() {
    //    std::cout << "DoRead" << std::endl;
    // логика:
    // читаем из сокета один раз.
    // если получили в итоге хотя бы одну целую команду с аргументами:
    //     выполняем все полученные целые команды с аргументами
    //     и не читаем ничего нового пока не отправим клиенту ответы обо всех выполненных командах
    // иначе:
    //    ждем след. раза чтобы прочитать еще
    try {
        _written_bytes = _wbuf_len = 0;
        _readed_bytes = read(_socket, _rbuffer, sizeof(_rbuffer));
        if (_readed_bytes == 0) {
            _logger->debug("Connection closed");
            OnClose();
            return;
        } else if (_readed_bytes < 0) {
            throw std::runtime_error(std::string(strerror(errno)));
        }
        _logger->debug("Got {} bytes from socket", _readed_bytes);

        // Single block of data readed from the socket could trigger inside actions a multiple times,
        // for example:
        // - read#0: [<command1 start>]
        // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
        // If we have, for example, many commands read, and server stopped,
        // we'll finish executing current command and no more
        while (_readed_bytes > 0 && !_shutdown) {
            _logger->debug("Process {} bytes", _readed_bytes);
            // There is no command yet
            if (!_cmd_to_exec) {
                std::size_t parsed = 0;
                if (_parser.Parse(_rbuffer, _readed_bytes, parsed)) {
                    // There is no command to be launched, continue to parse input stream
                    // Here we are, current chunk finished some command, process it
                    _logger->debug("Found new command: {} in {} bytes", _parser.Name(), parsed);
                    _cmd_to_exec = _parser.Build(_arg_remains);
                    if (_arg_remains > 0) {
                        _arg_remains += 2;
                    }
                }

                // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                if (parsed == 0) {
                    break;
                } else {
                    std::memmove(_rbuffer, _rbuffer + parsed, _readed_bytes - parsed);
                    _readed_bytes -= parsed;
                }
            }
            // There is command, but we still wait for argument to arrive...
            if (_cmd_to_exec && _arg_remains > 0) {
                _logger->debug("Fill argument: {} bytes of {}", _readed_bytes, _arg_remains);
                // There is some parsed command, and now we are reading argument
                std::size_t to_read = std::min(_arg_remains, std::size_t(_readed_bytes));
                _arg_for_cmd.append(_rbuffer, to_read);

                std::memmove(_rbuffer, _rbuffer + to_read, _readed_bytes - to_read);
                _arg_remains -= to_read;
                _readed_bytes -= to_read;
            }
            // There is command & argument - RUN!
            if (_cmd_to_exec && _arg_remains == 0) {
                _logger->debug("Start command execution");

                std::string result;
                sleep(2); // DEBUG
                _cmd_to_exec->Execute(*pStorage, _arg_for_cmd, result);
                _AppendResponse(result);

                // Prepare for the next command
                _cmd_to_exec.reset();
                _arg_for_cmd.resize(0);
                _parser.Reset();
            }
        } // /while (_readed_bytes > 0)
        if (_wbuf_len) {
            _event.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        _cmd_to_exec.reset();
        _arg_for_cmd.resize(0);
        _parser.Reset();
    }
}

// See Connection.h
void Connection::DoWrite() {
    //    std::cout << "DoWrite" << std::endl;
    try {
        _written_bytes = write(_socket, _wbuffer + _written_bytes, _wbuf_len);
        if (_written_bytes == -1 && errno != EINTR) {
            shutdown(_socket, SHUT_RDWR);
            throw std::runtime_error(std::string(strerror(errno)));
        } else if (_written_bytes > 0) {
            _wbuf_len -= _written_bytes;
            if (_wbuf_len == 0) {
                _event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
            }
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::_AppendResponse(std::string s) {
    std::size_t size = s.size();
    assert(_wbuf_len + size + 2 < sizeof(_wbuffer));
    std::copy(s.begin(), s.end(), _wbuffer + _wbuf_len);
    static const char end[] = "\r\n";
    std::memcpy(_wbuffer + _wbuf_len + size, end, 2);
    _wbuf_len += size + 2;
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
