#include "Connection.h"

#include <iostream>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {
    //    std::cout << "Start" << std::endl;
    _is_alive = true;
    _failed_to_write = false;
}

// See Connection.h
void Connection::OnError() {
    //    std::cout << "OnError" << std::endl;
    _is_alive = false;
}

// See Connection.h
void Connection::OnClose() {
    //    std::cout << "OnClose" << std::endl;
    _is_alive = false;
}

// See Connection.h
void Connection::DoRead() {
    //    std::cout << "DoRead" << std::endl;
    // читаем из сокета один раз.
    // если у нас после этого есть целые команды с аргументами:
    //   execut'им все целые команды, ставим интерес на райт.
    // иначе:
    //     оставляем интерес на рид
    // возможно в обоих случаях надо будет еще что-то поменять в epoll_ctl
    try {
        _written_bytes = _wbuf_len = 0;
        _readed_bytes = read(_socket, _rbuffer, sizeof(_rbuffer));
        if (_readed_bytes == 0) {
            _logger->debug("Connection closed");
            // V когда закрыть сокет?
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
                //                    sleep(2); // DEBUG
                _cmd_to_exec->Execute(*pStorage, _arg_for_cmd, result);
                std::size_t result_size = result.size();
                assert(_wbuf_len + result_size + 2 < sizeof(_wbuffer));
                std::copy(result.begin(), result.end(), _wbuffer + _wbuf_len);
                const char result_end[] = "\r\n";
                assert(sizeof(result_end) == 3);
                std::memcpy(_wbuffer + _wbuf_len + result_size, result_end, 2);
                _wbuf_len += result_size + 2;
                // Send response
                //                result += "\r\n";
                //                if (send(_socket, result.data(), result.size(), 0) <= 0) {
                //                    throw std::runtime_error("Failed to send response");
                //                }

                // Prepare for the next command
                _cmd_to_exec.reset();
                _arg_for_cmd.resize(0);
                _parser.Reset();
            }
        } // /while (_readed_bytes > 0)
        if (_wbuf_len) {
            _event.events = EPOLLOUT;
        }
    } catch (std::runtime_error &ex) {
        _is_alive = false;
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        std::string msg = "SERVER_ERROR ";
        msg += ex.what();
        std::size_t msg_size = msg.size();
        assert(_wbuf_len + msg_size + 2 < sizeof(_wbuffer));
        std::copy(msg.begin(), msg.end(), _wbuffer + _wbuf_len);
        const char msg_end[] = "\r\n";
        assert(sizeof(msg_end) == 3);
        std::memcpy(_wbuffer + _wbuf_len + msg_size, msg_end, 2);
        _wbuf_len += msg_size + 2;
        // V как то сообщить что пора закрывать сокет после того как опустошим _wbuffer
        //        if (send(_socket, msg.data(), msg.size(), 0) <= 0) {
        //            _logger->error("Failed to write response to client: {}", strerror(errno));
        //        }
    }

    // если случился эксепшн, мы сначала должны послать в сокет сообщение об ошибке, затем закрыть этот сокет.
    // возможно, для этого isalive и пригодится (и/или придется порождать bool поле в Connection)
    // We are done with this connection
    //    close(_socket);
}

// See Connection.h
void Connection::DoWrite() {
    //    std::cout << "DoWrite" << std::endl;
    try {
        _written_bytes = write(_socket, _wbuffer + _written_bytes, _wbuf_len);
        if (_written_bytes == -1 && errno != EINTR) {
            if (_failed_to_write) {
                // We failed to write message about previous writing failure
                _logger->error("Failed to write response to client: {}", strerror(errno));
            } else {
                // We failed to write command result
                _failed_to_write = true;
                throw std::runtime_error(std::string(strerror(errno)));
            }
        } else if (_written_bytes > 0) {
            _wbuf_len -= _written_bytes;
            if (_wbuf_len == 0) {
                _event.events = EPOLLIN;
            }
        }
    } catch (std::runtime_error &ex) {
        _is_alive = false;
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        if (_failed_to_write) {
            // Exception was caused by failure of writing of command result
            _written_bytes = _wbuf_len = 0;
        }
        // Exception wasn't caused by a writing failure
        std::string msg = "SERVER_ERROR ";
        msg += ex.what();
        std::size_t msg_size = msg.size();
        assert(_wbuf_len + msg_size + 2 < sizeof(_wbuffer));
        std::copy(msg.begin(), msg.end(), _wbuffer + _wbuf_len);
        const char msg_end[] = "\r\n";
        assert(sizeof(msg_end) == 3);
        std::memcpy(_wbuffer + _wbuf_len + msg_size, msg_end, 2);
        _wbuf_len += msg_size + 2;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
