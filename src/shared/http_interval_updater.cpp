#include "http_interval_updater.h"

#include <curl/easy.h>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili
{

CURLcode curl_initialized = curl_global_init(CURL_GLOBAL_ALL);
const char* CURL_ACCEPT_ENCODING = "gzip, deflate, br";
const int CURL_MAX_SIZE = 128 * 1024;  // 128 K
http_interval_updater::http_interval_updater(int update_interval_min, int timeout_sec)
    : _guard(_context.get_executor()),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _update_interval_min(update_interval_min)
{
    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    _timer->expires_from_now(boost::posix_time::seconds(1));
    _timer->async_wait(
        boost::bind(&http_interval_updater::on_timer_tick, this,
                    boost::asio::placeholders::error));

    _curl = curl_easy_init();
    if (!_curl)
    {
        spdlog::critical(
            "[http_upd] Failed creating libcURL handle!");
        return;
    }
    curl_easy_setopt(_curl, CURLOPT_TIMEOUT, timeout_sec);
}

http_interval_updater::~http_interval_updater()
{
    curl_easy_cleanup(_curl);
    _curl = nullptr;

    boost::system::error_code nec;
    _timer->cancel(nec);  // ignore error_code
    try
    {
        _guard.reset();
        _context.stop();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::critical(
            "[http_upd] Failed shutting down session IO Context! err:{}:{}:{}",
            ex.code().value(), ex.code().message(), ex.what());
    }
}

void http_interval_updater::reschedule_timer()
{
    _timer->expires_from_now(boost::posix_time::minutes(_update_interval_min));
    _timer->async_wait(boost::bind(&http_interval_updater::on_timer_tick, this,
                                   boost::asio::placeholders::error));
}

void http_interval_updater::refresh()
{
    if (!_curl)
    {
        spdlog::critical(
            "[http_upd] NO libcURL available!");
        return;
    }

    setup_curl();
    std::string output;
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &output);

    auto result = curl_easy_perform(_curl);
    if (result != CURLE_OK)
    {
        char* url;
        curl_easy_getinfo(_curl, CURLINFO_EFFECTIVE_URL, &url);
        auto err = curl_easy_strerror(result);
        spdlog::error("[http_upd] Failed to update: url={} , msg={}", url, err);
        return;
    }

    long status_code = 400;
    curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (status_code != 200)
    {
        char* url;
        curl_easy_getinfo(_curl, CURLINFO_EFFECTIVE_URL, &url);
        spdlog::error("[http_upd] Failed to update: Http Status {} from URL {}, response={}", status_code, url, output);
    }
    on_updated(output);
}

void http_interval_updater::on_timer_tick(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
            spdlog::info("[http_upd] Shutting down http updater.");
        return;
    }

    refresh();
    reschedule_timer();
}

size_t http_interval_updater_receive_content_length(char* buf, size_t size, size_t nmemb, std::string* str)
{
    size_t realSize = size * nmemb;
    if (str->size() + realSize >= CURL_MAX_SIZE)
        return 0;
    str->append(buf, realSize);
    return realSize;
}

void http_interval_updater::setup_curl()
{
    //curl_easy_setopt(_curl, CURLOPT_VERBOSE, 1L);
    auto request_url = on_request_url();
    auto request_method = on_request_method();
    auto user_agent = on_user_agent();
    curl_easy_setopt(_curl, CURLOPT_URL, request_url);
    curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, request_method);
    curl_easy_setopt(_curl, CURLOPT_USERAGENT, user_agent);

    SPDLOG_DEBUG("[http_upd] Request URL={}, method={}, ua={}", request_url, request_method, user_agent);

    auto str = on_request_payload();
    if (str)
    {
        curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, str);
        curl_easy_getinfo(_curl, CURLINFO_CONTENT_TYPE, on_request_content_type());
    }

    struct curl_slist* chunk = NULL;
    // Disable Expect: 100-continue
    chunk = curl_slist_append(chunk, "Expect:");

    str = on_request_accept();
    if (str)
    {
        std::string accept_header = std::string("Accept:") + str;
        chunk = curl_slist_append(chunk, accept_header.c_str());
    }

    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, chunk);
    str = on_request_referer();
    if (str)
        curl_easy_setopt(_curl, CURLOPT_REFERER, str);
    curl_easy_setopt(_curl, CURLOPT_ACCEPT_ENCODING, CURL_ACCEPT_ENCODING);

    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, http_interval_updater_receive_content_length);
}
}
