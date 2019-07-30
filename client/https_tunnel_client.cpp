#include "https_tunnel_client.h"
#include <ctime>
#include <chrono>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <iostream>

#define KK_PRT(fmt...)   \
    do {\
    time_t timep;\
    time(&timep);\
    tm* tt = localtime(&timep); \
    printf("[%d-%02d-%02d %02d:%02d:%02d][%s]-%d: ", tt->tm_year+1900,tt->tm_mon+1,tt->tm_mday,tt->tm_hour,tt->tm_min,tt->tm_sec, __FUNCTION__, __LINE__);\
    printf(fmt);\
    printf("\n");\
    }while(0)

static void set_socket_opt(TcpSocket& socket)
{
    //boost::asio::socket_base::keep_alive opt_keep_alive(true);
    //socket.set_option(opt_keep_alive);

    int flags = 1;
    int tcp_keepalive_time = 20;
    int tcp_keepalive_probes = 3;
    int tcp_keepalive_intvl = 3;

    int ret = 0;
    ret = setsockopt(socket.native_handle(), SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));
    if(ret < 0)
    {
        KK_PRT("setsockopt SO_KEEPALIVE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &tcp_keepalive_time, sizeof(tcp_keepalive_time));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPIDLE failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPINTVL, &tcp_keepalive_intvl, sizeof(tcp_keepalive_intvl));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPINTVL failed");
    }
    ret = setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_KEEPCNT, &tcp_keepalive_probes, sizeof(tcp_keepalive_probes));
    if(ret < 0)
    {
        KK_PRT("setsockopt TCP_KEEPCNT failed");
    }
}

HttpsTunnelClient::HttpsTunnelClient(IoContext& ioc, SslContext &ssl_cxt, bool verify) :
    m_ioc(ioc), m_timer(m_ioc), m_socket(m_ioc, ssl_cxt)
{
    if(!verify)
    {
        //不验证证书合法性,所以cxt不用调用load_verify_file add_verify_path或add_certificate_authority
        m_socket.set_verify_mode(boost::asio::ssl::verify_none);
    }
    else
    {
        m_socket.set_verify_mode(boost::asio::ssl::verify_peer);
        m_socket.set_verify_callback([](bool preverified, boost::asio::ssl::verify_context& ctx) {
            KK_PRT("preverified:%d", (int)preverified);
            char subject_name[256];
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
            KK_PRT("subject_name:%s", subject_name);
            return preverified;
        });
    }
}

HttpsTunnelClient::~HttpsTunnelClient()
{

}

void HttpsTunnelClient::start(string host, uint16_t port, string session_id)
{
    m_send_response_queue.clear();
    m_host = std::move(host);
    char tmp[8];
    sprintf(tmp, "%d", port);
    m_port = tmp;
    m_session_id = std::move(session_id);

    start_resolve_co();
    start_check_timer();
}

void HttpsTunnelClient::stop()
{
    m_send_response_queue.clear();
    stop_check_timer();
}

void HttpsTunnelClient::start_resolve_co()
{
    m_resolve_co = Coroutine();
    m_resolver.reset(new Resolver(m_ioc));
    loop_resolve({}, {});
}

void HttpsTunnelClient::start_conn_co()
{
    m_conn_co = Coroutine();
    if(m_socket.next_layer().is_open())
    {
        boost::system::error_code ec;
        m_socket.shutdown(ec);
        m_socket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.next_layer().close(ec);
    }
    loop_conn({});
}

void HttpsTunnelClient::start_recv_co()
{
    m_recv_co = Coroutine();
    loop_recv({});
}

void HttpsTunnelClient::start_http_co(string id, StrRequest& req)
{
    HttpCoInfoPtr co_info(new HttpCoInfo(m_ioc));
    co_info->id = std::move(id);
    co_info->req = std::move(req);

    loop_http({}, std::move(co_info));
}

void HttpsTunnelClient::start_send_co(HttpCoInfoPtr co_info)
{
    //r表示响应
    bool write_in_progress = !m_send_response_queue.empty();
    m_send_response_queue.push_back(std::move(co_info->res));
    if (!write_in_progress)
    {
        m_send_co = Coroutine();
        loop_send({});
    }
}

void HttpsTunnelClient::start_check_timer()
{
    m_timer_co = Coroutine();
    loop_check({});
}

void HttpsTunnelClient::stop_check_timer()
{
    m_timer.cancel();
}

#include <boost/asio/yield.hpp>
void HttpsTunnelClient::loop_resolve(boost::system::error_code ec, ResolverResult r)
{
    reenter(m_resolve_co)
    {
        m_socket_status = Resolving;
        KK_PRT("start resolve");
        yield m_resolver->async_resolve(m_host, m_port, [this](boost::system::error_code ec, ResolverResult r) {
            this->loop_resolve(ec, r);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            m_socket_status = Disconnected;
            yield break;
        }
        m_resolve_result = std::move(r);
        start_conn_co();
    }
}

void HttpsTunnelClient::loop_conn(boost::system::error_code ec)
{
    reenter(m_conn_co)
    {
        m_socket_status = Connecting;
        KK_PRT("start conn");
        yield m_socket.next_layer().async_connect((*m_resolve_result.begin()).endpoint(), [this](boost::system::error_code ec) {
            this->loop_conn(ec);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            m_socket_status = Disconnected;
            yield break;
        }
        set_socket_opt(m_socket.next_layer());
        yield m_socket.async_handshake(boost::asio::ssl::stream_base::client, [this](boost::system::error_code ec) {
            this->loop_conn(ec);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            m_socket_status = Disconnected;
            yield break;
        }

        m_req = {};
        m_req.method(http::verb::connect);
        m_req.keep_alive(true);
        m_req.set(SESSION_ID, m_session_id);
        m_req.target("setup.tunnel");
        m_req.content_length(0);
        KK_PRT("start setup");
        yield http::async_write(m_socket, m_req, [this](boost::system::error_code ec, size_t) {
            this->loop_conn(ec);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            m_socket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            m_socket_status = Disconnected;
            yield break;
        }
        yield http::async_read(m_socket, m_read_buffer, m_res, [this](boost::system::error_code ec, size_t) {
            this->loop_conn(ec);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            m_socket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            m_socket_status = Disconnected;
            yield break;
        }
        {
            if((int)m_res.result() >= 300 || (int)m_res.result() < 200)
            {
                KK_PRT("setup error:%d", (int)m_res.result());
                m_socket.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                m_socket_status = Disconnected;
                yield break;
            }
        }

        m_socket_status = Connected;
        KK_PRT("setup success");
        if(m_conn_cb)
        {
            m_conn_cb(m_socket_status);
        }
        start_recv_co();
    }
}

void HttpsTunnelClient::loop_recv(boost::system::error_code ec)
{
    reenter(m_recv_co)
    {
        while(1)
        {
            yield http::async_read(m_socket, m_read_buffer, m_req, [this](boost::system::error_code ec, size_t) {
                this->loop_recv(ec);
            });
            if(ec)
            {
                KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
                if(m_socket_status != Disconnected)
                {
                    m_socket_status = Disconnected;
                    if(m_conn_cb)
                    {
                        m_conn_cb(m_socket_status);
                    }
                }
                yield break;
            }
            std::cout << "recv req:" << m_req << "\n";
            //检查请求是否合法
            auto it = m_req.find(TID);
            if(it == m_req.end())
            {
                KK_PRT("tid not exist");
                continue;
            }
            else
            {
                //请求
                boost::string_view tid = (*it).value();
                m_req.erase(it);
                start_http_co(tid.to_string(), m_req);
            }
        }
    }
}

void HttpsTunnelClient::loop_http(boost::system::error_code ec, HttpCoInfoPtr co_info)
{
    reenter(co_info->http_co)
    {
        yield co_info->http_socket.async_connect(Endpoint{boost::asio::ip::make_address(m_local_ip, ec), m_local_port}, [co_info, this](boost::system::error_code ec) {
            this->loop_http(ec, co_info);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            co_info->res.result(http::status::service_unavailable);
            co_info->res.set(TID, co_info->id);
            start_send_co(co_info);
            yield break;
        }
        //使用短连接
        co_info->req.keep_alive(false);
        co_info->req.content_length(co_info->req.body().size());
        std::cout << "send req: " << co_info->req << "\n";
        yield http::async_write(co_info->http_socket, co_info->req, [co_info, this](boost::system::error_code ec, size_t){
            this->loop_http(ec, co_info);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            co_info->res.result(http::status::service_unavailable);
            co_info->res.set(TID, co_info->id);
            start_send_co(co_info);
            yield break;
        }
        yield http::async_read(co_info->http_socket, co_info->buffer, co_info->res, [co_info, this](boost::system::error_code ec, size_t){
            this->loop_http(ec, co_info);
        });
        if(ec)
        {
            KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
            co_info->res.result(http::status::service_unavailable);
            co_info->res.set(TID, co_info->id);
            start_send_co(co_info);
            yield break;
        }
        co_info->res.set(TID, co_info->id);
        std::cout << "recv res: " << co_info->res << "\n";
        start_send_co(co_info);
    }
}

void HttpsTunnelClient::loop_send(boost::system::error_code ec)
{
    reenter(m_send_co)
    {
        while(!m_send_response_queue.empty())
        {
            yield
            {
                StrResponse& res = m_send_response_queue.front();
                //转换成长连接
                res.keep_alive(true);
                res.content_length(res.body().size());
                std::cout << "send res:" << res << "\n";
                http::async_write(m_socket, res, [this](boost::system::error_code ec, std::size_t) {
                    this->loop_send(ec);
                });
            }
            //不管是否成功,总是删除
            m_send_response_queue.pop_front();
            if(ec)
            {
                KK_PRT("error:%d,%s", ec.value(), ec.message().c_str());
                if(m_socket_status != Disconnected)
                {
                    m_socket_status = Disconnected;
                    if(m_conn_cb)
                    {
                        m_conn_cb(m_socket_status);
                    }
                }
                yield break;
            }
        }
    }
}

void HttpsTunnelClient::loop_check(boost::system::error_code ec)
{
    reenter(m_timer_co)
    {
        while(1)
        {
            if(ec)
            {
                yield break;
            }

            if(m_socket_status == Disconnected)
            {
                start_resolve_co();
            }

            m_timer.expires_after(std::chrono::seconds(30));
            yield m_timer.async_wait([this](boost::system::error_code ec) {
                this->loop_check(ec);
            });
        }
    }
}

#include <boost/asio/unyield.hpp>
