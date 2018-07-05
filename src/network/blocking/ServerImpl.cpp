#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <algorithm>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>
#include "./../../protocol/Parser.h"
#include <afina/execute/Command.h>
namespace Afina {
namespace Network {
namespace Blocking {

template <void (ServerImpl::*function_for_start)(int)>
void *ServerImpl::RunThreadProxy(void *p) {
    PthreadProxyStruct *srv = reinterpret_cast<PthreadProxyStruct *>(p);
    try {
        (srv->server->*function_for_start)(srv->fd);
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    delete srv;
    return 0;
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps)
    : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(vint res = read(fd, data, command_buffer);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunThreadProxy<&ServerImpl::RunAcceptor>,
           new PthreadProxyStruct{this,0}) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
    shutdown(server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);
    while (true) {
        {
            std::unique_lock<std::mutex> lk(connections_mutex);
            connections_cv.wait(lk);
            if (connections.empty()) {
                break;
            }
        }
    }
 
}

// See Server.h
void ServerImpl::RunAcceptor(int) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makesthe OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);
    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            if (running.load()){
                close(server_socket);
                std::cout << "accept failed" << std::endl;
                throw std::runtime_error("Socket accept() failed");
            }
        }

        // Start new thread and process data from/to connection
        {
            // std::string msg = "start new thread and process memcached";
            std::lock_guard<std::mutex> guard(connections_mutex); 
            if (connections.size() < max_workers) {
                std::cout << "network debug: New connection " << connections.size() << ":" << max_workers << std::endl;
                pthread_t thread;
                int res = pthread_create(&thread, NULL, ServerImpl::RunThreadProxy<&ServerImpl::RunConnection>,
                        new PthreadProxyStruct{this,client_socket});
                if (res != 0) {
                    throw std::runtime_error("pthread_create() failed");
                }
                connections.insert(thread);
                if (pthread_detach(thread)) {
                    throw std::runtime_error("pthread_detach() failed");
                }
            } else {         
                shutdown(client_socket, SHUT_RDWR);
                close(client_socket);
            }
        }
    }

    // Cleanup on exit...
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
}

// See Server.h
void ServerImpl::RunConnection(int fd) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // All connection work is here
    Afina::Protocol::Parser parser;
    char data[command_buffer+1];
    std::string full_data; 
    while (running.load()) {
        ssize_t readed;
        if((readed = recv(fd, data, command_buffer, 0)) <= 0 && full_data.size() == 0) {break;}
        data[readed] = '\0';
        full_data.append(data);
        size_t parsed = 0;
        bool command_parsed = false;
        try {
            command_parsed = parser.Parse(full_data, parsed);
        } 
        catch (std::exception &e) {
            std::string err = "SERVER_ERROR: ";
            err += e.what();
            err += "\r\n";
            if (send(fd, err.data(), err.size(), 0) < 0) {
                break;
            }
            parser.Reset();
            full_data = "";
            continue;
        }
        full_data = full_data.substr(parsed);

        if (!command_parsed) {
            continue;
        }
        sleep(10);
        uint32_t args_read = 0;
        auto command = parser.Build(args_read);
         
        std::string args = "";
        if (args_read != 0) {
            args_read += 2;
            
            args = full_data.substr(0, args_read);
            if (args_read >= full_data.size()) {
                full_data = "";
            } else  {
                full_data = full_data.substr(args_read);
            }

            while (args.size() < args_read) {
                if((readed = recv(fd, data, std::min(args_read - args.size(), command_buffer), 0)) <= 0) {
                    goto end;
                }
                data[readed] = '\0';
                args.append(data);
            }
            args = args.substr(0, args_read - 2);
        }
        
        std::string result;
        try {
            command->Execute(*pStorage, args, result);
        } catch (std::exception &e) {
            result = "SERVER_ERROR "; 
            result += e.what();
        }
        
        result += "\r\n";
        int tosend = result.size();
        auto cresult = result.c_str();
        while (tosend > 0) {
            int len_sended; 
            if ((len_sended = send(fd, cresult + (result.size() - tosend), tosend, 0)) < 0) {
                goto end;
            }
            tosend -= len_sended;
        }
        std::cout<<"full: "<<full_data<<std::endl;
        parser.Reset();
    }
    end:
    shutdown(fd, SHUT_RDWR);
    close(fd);
    std::cout << "network debug: Connection closed" << std::endl;
    
    std::lock_guard<std::mutex> guard(connections_mutex);
    connections.erase(connections.find(pthread_self()));
    connections_cv.notify_one();
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
