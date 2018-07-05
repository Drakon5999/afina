#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <unistd.h>
#include <algorithm>
#include <protocol/Parser.h>
#include <netinet/in.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <cstring>
#include "Utils.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) : storage{ps}, Stoped{false}{}
Worker::Worker(Worker && w) : Stoped{w.Stoped.load()} {
    thread = std::move(w.thread);
    storage = std::move(w.storage);
    servsocket = w.servsocket;
    EpoolCount = w.EpoolCount;
    full_data = std::move(w.full_data);
}

// See Worker.h
Worker::~Worker() {
    Join();
}

// See Worker.h
void Worker::Start(int server_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    servsocket = server_socket;
    Stoped.store(false);
    thread = std::move(std::thread(&Worker::OnRun, this));
};

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    Stoped.store(true);
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    if (thread.joinable())
        thread.join();
}

// See Worker.h
void Worker::EpollAdd (int epoll_fd, int add_fd) {
    int e = epoll_create(EpoolCount);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP ;
    // ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLEXCLUSIVE
    // use SO_REUSEPORT
    ev.data.fd = add_fd;
    int res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, add_fd, &ev);
    if (res != 0) {
        throw std::runtime_error("Epoll add failed");
    }
}

// See Worker.h
void Worker::EpollDelete (int del_fd) {
    std::cout<< "END!!!" << std::endl;
    full_data.erase(del_fd);
    parsers.erase(del_fd);
    command_parsed.erase(del_fd);
    args.erase(del_fd);
    commands.erase(del_fd);
    args_read.erase(del_fd);
    shutdown(del_fd, SHUT_RDWR);
    close(del_fd);
    std::cout << "network debug: Connection closed" << std::endl;
}

// See Worker.h
void Worker::OnRun() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    try {
        int client_socket;
        struct sockaddr_in client_addr;
        socklen_t sinSize = sizeof(struct sockaddr_in);

        int e = epoll_create(EpoolCount);
        EpollAdd(e, servsocket);
        struct epoll_event event;
        int event_count = 1;
        int timeout = 100;
        while (!Stoped.load()) {
            int res = epoll_wait(e, &event, event_count, timeout);
            if (res == event_count) {
                if (event.data.fd == servsocket) {
                    // process server socket
                    if ((client_socket = accept4(servsocket, (struct sockaddr *)&client_addr, &sinSize, SOCK_NONBLOCK)) == -1) {
                        throw std::runtime_error("Socket accept() failed");
                    }
                    EpollAdd(e, client_socket);
                } else {
                        if ((event.events & EPOLLERR) != 0 || (event.events & EPOLLHUP) != 0) {
                            std::cout << "Good end!" << std::endl;
                            EpollDelete(event.data.fd);
                        } else {
                            // process client socket
                            std::cout << event.data.fd << std::endl;
                            ProcessConnection(event.data.fd);
                        }
                }
            } else if (res == -1) {
                throw std::runtime_error("Epoll get failed");
            }

        }

        close(e);
    } catch (const std::runtime_error & err) {
        std::cout << err.what() << std::endl;
    }

}

void Worker::ProcessConnection(int fd){
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // All connection work is here
    unsigned long tosend = 0;
    const char *cresult = nullptr;
    std::string result;
    size_t parsed = 0;
    sleep(10);
    unsigned command_buffer = 4095;
    char data[command_buffer+1];
    ssize_t readed;
    if(full_data.find(fd) == full_data.end()) {
        command_parsed[fd] = false;
        full_data[fd] = "";
        args[fd] = "";
        args_read[fd] = 0;
        parsers[fd] = std::unique_ptr<Afina::Protocol::Parser>(new Afina::Protocol::Parser());
    }

    if (!command_parsed[fd]) {
        if((readed = recv(fd, data, command_buffer, 0)) <= 0 && full_data[fd].size() == 0) {goto end;}
        data[readed] = '\0';
        full_data[fd].append(data);
        try {
            command_parsed[fd] = parsers[fd]->Parse(full_data[fd], parsed);
        } catch (std::exception &e) {
            std::string err = "Parse ERROR: ";
            err += e.what();
            err += "\r\n";
            if (send(fd, err.data(), err.size(), 0) < 0) {
                goto end;
            }
            parsers[fd]->Reset();
            full_data[fd] = "";
            return;
        }
        full_data[fd] = full_data[fd].substr(parsed);
        if (command_parsed[fd]) {
            commands[fd] = parsers[fd]->Build(args_read[fd]);

            if (args_read[fd] != 0) {
                args_read[fd] += 2;
            }
        } else {
            return;
        }
    }

    if (args_read[fd] != 0) {
        args[fd] = full_data[fd].substr(0, args_read[fd]);
        if (args_read[fd] >= full_data[fd].size()) {
            full_data[fd] = "";
        } else  {
            full_data[fd] = full_data[fd].substr(args_read[fd]);
        }
        if (args[fd].size() < args_read[fd]) {
            if((readed = recv(fd, data, std::min(args_read[fd] - (unsigned)args[fd].size(), command_buffer), 0)) <= 0) {
                goto end;
            }
            data[readed] = '\0';
            args[fd].append(data);
        }
        if (args[fd].size() < args_read[fd]) {
            return;
        }
        args[fd] = args[fd].substr(0, args_read[fd] - 2);
    }

    try {
        commands[fd]->Execute(*storage, args[fd], result);
    } catch (std::exception &e) {
        result = "SERVER_ERROR ";
        result += e.what();
    }

    result += "\r\n";
    tosend = result.size();
    cresult = result.c_str();
    while (tosend > 0) {
        ssize_t len_sended;
        if ((len_sended = send(fd, cresult + (result.size() - tosend), tosend, 0)) < 0) {
            goto end;
        }
        tosend -= len_sended;
    }

    std::cout << "full: " << full_data[fd] << std::endl;
    parsers[fd]->Reset();
    command_parsed[fd] = false;
    args[fd] = "";
    args_read[fd] = 0;
    return;
end:
    EpollDelete(fd);
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
