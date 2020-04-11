#include "supervisor_server_session.h"

namespace vNerve::bilibili::worker_supervisor
{
supervisor_server_session::
supervisor_server_session(
    const config::config_t config,
    const supervisor_buffer_handler buffer_handler,
    const supervisor_tick_handler tick_handler,
    const supervisor_new_worker_handler new_worker_handler)
    : _guard(_context.get_executor()),
      _config(config),
      _buffer_handler(buffer_handler),
      _tick_handler(tick_handler),
      _new_worker_handler(new_worker_handler),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _acceptor(_context),
      _rand_engine(_rand()),
      _timer_interval_ms((*config)["check-interval-ms"].as<int>())
{
    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    start_accept();
    reschedule_timer();
}

supervisor_server_session::
~supervisor_server_session()
{
    _context.stop();
    // TODO gracefully
}

void supervisor_server_session::
start_accept()
{
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    _acceptor.async_accept(
        *socket,
        boost::bind(&supervisor_server_session::on_accept, shared_from_this(),
                    boost::asio::placeholders::error, socket));
}

void supervisor_server_session::on_accept(
    const boost::system::error_code& err,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (err)
    {
        // todo log
        // NOTE: Don't return from this block!
    }
    else
    {
        uint64_t identifier = _rand_dist(_rand_engine);
        _sockets.emplace(identifier, socket);
        _new_worker_handler(identifier);
    }

    start_accept();
}

void supervisor_server_session::reschedule_timer()
{
    _timer->expires_from_now(
        boost::posix_time::milliseconds(_timer_interval_ms));
    _timer->async_wait(boost::bind(&supervisor_server_session::on_timer_tick,
                                   shared_from_this(),
                                   boost::asio::placeholders::error));
}

void supervisor_server_session::on_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            return; // closing socket.
        }
        // TODO logging
    }

    _tick_handler();

    reschedule_timer();
}

void supervisor_server_session::
send_message(uint64_t identifier, unsigned char* msg, size_t len)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;
    auto buf = new unsigned char[len + sizeof(unsigned int)];
    *reinterpret_cast<unsigned int*>(buf) =
        boost::asio::detail::socket_ops::host_to_network_long(
            static_cast<unsigned int>(len));
    std::memcpy(buf + sizeof(unsigned int), msg, len);

    auto& socket = *(socket_iter->second);
    boost::asio::async_write(socket, boost::asio::const_buffer(buf, len),
                             [msg](const boost::system::error_code&,
                                   std::size_t) -> void
                             {
                                 // todo log error here?
                                 delete[] msg;
                             });
}

void supervisor_server_session::disconnect_worker(uint64_t identifier)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;

    auto socket = socket_iter->second;
    _sockets.erase(socket_iter);
    auto ec = boost::system::error_code();
    socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket->close(ec);

    if (ec)
    {
        // log
    }
}
}