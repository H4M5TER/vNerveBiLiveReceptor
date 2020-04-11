#pragma once

#include "config.h"
#include <memory>
#include <random>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <robin_hood.h>

namespace vNerve::bilibili::worker_supervisor
{
using supervisor_buffer_handler =
    std::function<void(unsigned long long ,
                       unsigned char* , size_t )>;
using supervisor_tick_handler = std::function<void()>;
using supervisor_new_worker_handler = std::function<void(uint64_t)>;

class supervisor_server_session
    : public std::enable_shared_from_this<supervisor_server_session>
{
private:
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        _guard;
    config::config_t _config;

    boost::asio::io_context _context;
    robin_hood::unordered_map<uint64_t,
                              std::shared_ptr<boost::asio::ip::tcp::socket>>
        _sockets;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::unique_ptr<boost::asio::deadline_timer> _timer;
    int _timer_interval_ms;

    boost::thread _thread;
    supervisor_buffer_handler _buffer_handler;
    supervisor_tick_handler _tick_handler;
    supervisor_new_worker_handler _new_worker_handler;

    void start_accept();
    void on_accept(const boost::system::error_code&,
                   std::shared_ptr<boost::asio::ip::tcp::socket>);

    std::random_device _rand;
    std::mt19937_64 _rand_engine;
    std::uniform_int_distribution<unsigned long long> _rand_dist;

    void reschedule_timer();
    void on_timer_tick(const boost::system::error_code& ec);

public:
    supervisor_server_session(config::config_t config,
                              supervisor_buffer_handler,
                              supervisor_tick_handler,
                              supervisor_new_worker_handler);
    ~supervisor_server_session();

    supervisor_server_session(supervisor_server_session& another) = delete;
    supervisor_server_session(supervisor_server_session&& another) = delete;
    supervisor_server_session& operator =(supervisor_server_session & another) = delete;
    supervisor_server_session& operator =(supervisor_server_session && another) = delete;

    /// @param msg Message to be sent. Taking ownership of msg
    void send_message(uint64_t identifier, unsigned char* msg, size_t len);
    void disconnect_worker(uint64_t identifier);
};
}  // namespace vNerve::bilibili::worker_supervisor