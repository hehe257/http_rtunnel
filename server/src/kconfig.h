#ifndef KCONFIG_H
#define KCONFIG_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include "request_api_server.h"
#include <boost/uuid/uuid.hpp>
#include <boost/date_time.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/crc.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/gregorian/greg_duration.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/fiber/all.hpp>
#include "logger.h"

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::uuids;
using namespace boost::property_tree;


typedef boost::asio::io_context IoContext;
typedef boost::asio::ip::tcp::acceptor Acceptor;
typedef boost::asio::ip::tcp::endpoint Endpoint;
typedef boost::asio::ip::tcp::socket TcpSocket;
typedef boost::asio::coroutine Coroutine;
typedef boost::asio::ip::tcp::resolver Resolver;
typedef boost::asio::ip::tcp::resolver::results_type ResolverResult;

typedef std::lock_guard<boost::fibers::mutex> fiber_lock;


#define SETUP_MARK "SET-JRPROXY"
#define TID "TID-J"

class ConfigParams
{
public:
    bool init(int argc, char **argv);
    static ConfigParams& instance();

    string http_listen_addr = "0.0.0.0";
    uint16_t http_listen_port = 3080;
    uint16_t http_thread_pool = 3;

    string tunnel_listen_addr = "0.0.0.0";
    uint16_t tunnel_listen_port = 3081;
    uint16_t tunnel_thread_pool = 3;

    uint16_t req_timeout_secs = 15;
    vector<string> listen_urls;

    string log_path = "./http_rrproxy.log";
    boost::log::trivial::severity_level log_level = boost::log::trivial::debug;

private:
    ConfigParams();
    static ConfigParams *m_instance;
};

#endif
