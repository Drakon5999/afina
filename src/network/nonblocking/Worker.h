#ifndef AFINA_NETWORK_NONBLOCKING_WORKER_H
#define AFINA_NETWORK_NONBLOCKING_WORKER_H

#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <protocol/Parser.h>

namespace Afina {

// Forward declaration, see afina/Storage.h
class Storage;

namespace Network {
namespace NonBlocking {

/**
 * # Thread running epoll
 * On Start spaws background thread that is doing epoll on the given server
 * socket and process incoming connections and its data
 */
class Worker {
public:
    Worker(std::shared_ptr<Afina::Storage> ps);
    Worker(Worker && w);
    ~Worker();

    /**
     * Spaws new background thread that is doing epoll on the given server
     * socket. Once connection accepted it must be registered and being processed
     * on this thread
     */
    void Start(int server_socket);

    /**
     * Signal background thread to stop. After that signal thread must stop to
     * accept new connections and must stop read new commands from existing. Once
     * all readed commands are executed and results are send back to client, thread
     * must stop
     */
    void Stop();

    /**
     * Blocks calling thread until background one for this worker is actually
     * been destoryed
     */
    void Join();

protected:
    /**
     * Method executing by background thread
     */
     void OnRun();

    /**
     * Method add descriptor to epoll in background thread
     */
    void EpollAdd(int epoll_fd, int add_fd);

    /**
     * Method close descriptor and delete some information abaut it
     */
    void EpollDelete (int del_fd);

    /**
     * Method process connection in background thread
     */
    void ProcessConnection(int conn_fd);


    std::shared_ptr<Afina::Storage> storage;
    int servsocket = -1;
    std::atomic_bool Stoped;
    int EpoolCount = 10;

    std::unordered_map<int, std::string> full_data;
    std::unordered_map<int, std::string> args;
    std::unordered_map<int, std::unique_ptr<Execute::Command>> commands;
    std::unordered_map<int, bool> command_parsed;
    std::unordered_map<int, unsigned> args_read;
    std::unordered_map<int, std::unique_ptr<Afina::Protocol::Parser>> parsers;

private:
    std::thread thread;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
