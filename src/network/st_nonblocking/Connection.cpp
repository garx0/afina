#include "Connection.h"

#include <iostream>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// V сделать без define потом
#define READ_EVENT (EPOLLIN | EPOLLRDHUP)
#define WRITE_EVENT (EPOLLOUT | EPOLLRDHUP)
#define NO_EVENT (EPOLLRDHUP)

// See Connection.h
void Connection::Start() {
    //    std::cout << "Start" << std::endl;
    _is_alive = true;
    _readed_bytes = -1;
    _write_pos = 0;
    _event.events = READ_EVENT;
}

// See Connection.h
void Connection::OnError(bool shut_wr) {
    //    std::cout << "OnError" << std::endl;
    OnClose(shut_wr);
}

// See Connection.h
void Connection::OnClose(bool shut_wr) {
    //    std::cout << "OnClose" << std::endl;
    _is_alive = false;
    if (shut_wr) {
        shutdown(_socket, SHUT_RDWR);
        _event.events = NO_EVENT;
    } else {
        shutdown(_socket, SHUT_RD);
        _event.events = _event.events & EPOLLOUT ? WRITE_EVENT : NO_EVENT;
    }
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
        while (_readed_bytes > 0) {
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
                //                sleep(2); // DEBUG
                _cmd_to_exec->Execute(*pStorage, _arg_for_cmd, result);
                result += "\r\n";
                _responses.emplace_back(std::move(result));

                // Prepare for the next command
                _cmd_to_exec.reset();
                _arg_for_cmd.resize(0);
                _parser.Reset();
            }
        } // /while (_readed_bytes > 0)
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        std::string msg = "ERROR ";
        msg += ex.what();
        msg += "\r\n";
        // to pass all itests, replace std::move(msg) by "ERROR"
        _responses.emplace_back(std::move(msg));
        OnError();
    }
    if (!_responses.empty()) {
        _event.events |= WRITE_EVENT;
    }
}

// See Connection.h
void Connection::DoWrite() {
    //    std::cout << "DoWrite" << std::endl;
    assert(!_responses.empty());
    std::size_t q_size = _responses.size();
    iovec *q_iov = new iovec[q_size];
    try {
        std::size_t i = 0;
        for (auto it = _responses.begin(); it != _responses.end(); ++it, ++i) {
            q_iov[i].iov_base = (void *)(it->data());
            q_iov[i].iov_len = it->size();
        }
        q_iov[0].iov_base = static_cast<char *>(q_iov[0].iov_base) + _write_pos;
        q_iov[0].iov_len -= _write_pos;
        int _written_bytes = writev(_socket, q_iov, q_size);
        if (_written_bytes == -1 && errno != EINTR) {
            OnError(true);
            throw std::runtime_error(std::string(strerror(errno)));
        } else if (_written_bytes > 0) {
            for (i = 0; i < q_size; ++i) {
                int len = q_iov[i].iov_len;
                if (_written_bytes >= len) {
                    _written_bytes -= len;
                    _responses.pop_front();
                    _write_pos = 0;
                } else {
                    break;
                }
            }
            _write_pos += _written_bytes;
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
    delete[] q_iov;

    if (_responses.empty()) {
        _event.events = READ_EVENT;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
