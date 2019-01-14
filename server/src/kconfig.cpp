#include "kconfig.h"
#include <boost/log/attributes.hpp>
#include <boost/program_options.hpp>


namespace {
std::map<string, boost::log::trivial::severity_level> logger_levels = {
    {"trace", boost::log::trivial::trace},
    {"debug", boost::log::trivial::debug},
    {"info", boost::log::trivial::info},
    {"warning", boost::log::trivial::warning},
    {"error", boost::log::trivial::error},
    {"fatal", boost::log::trivial::fatal}
};
}

ConfigParams *ConfigParams::m_instance = nullptr;

ConfigParams& ConfigParams::instance()
{
    if (m_instance)
        return *m_instance;

    m_instance = new ConfigParams();
    return *m_instance;
}

ConfigParams::ConfigParams()
{

}

bool ConfigParams::init(int argc, char **argv)
{
    namespace po = boost::program_options;
    try
    {
        string config_file;
        //命令行选项
        po::options_description cmdline_options("Generic options");
        cmdline_options.add_options()
                ("help,h", "produce help message")
                ("config,c", po::value<string>(&config_file)->default_value("../config/http_rrproxy_ws.cfg"));

        po::options_description config_file_options("configure file options");
        config_file_options.add_options()
                ("http_listen_addr", po::value<string>(), "http listen address")
                ("http_listen_port", po::value<uint16_t>(), "http listen port")
                ("ws_listen_addr", po::value<string>(), "websocket listen address")
                ("ws_listen_port", po::value<uint16_t>(), "websocket listen port")
                ("req_timeout_secs", po::value<uint16_t>(), "request timeout seconds")
                ("listen_urls", po::value<vector<string>>(), "http and ws listen urls")
                ("log_path", po::value<string>(), "log file path")
                ("log_level", po::value<string>(), "log level:trace debug info warning error fatal");


        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
        notify(vm);
        if (vm.count("help"))
        {
            cout << cmdline_options << endl;
            return false;
        }

        std::ifstream ifs(config_file);
        if (!ifs)
        {
            cout << "can not open config file: " << config_file << "\n";
            return false;
        }
        else
        {
            store(po::parse_config_file(ifs, config_file_options), vm);
            notify(vm);
        }

        http_listen_addr = vm["http_listen_addr"].as<string>();
        http_listen_port = vm["http_listen_addr"].as<uint16_t>();

        ws_listen_addr = vm["ws_listen_addr"].as<string>();
        ws_listen_port = vm["ws_listen_port"].as<uint16_t>();

        req_timeout_secs = vm["req_timeout_secs"].as<uint16_t>();

        listen_urls = vm["listen_urls"].as<vector<string>>();

        log_path = vm["log_path"].as<string>();

        string str_level = vm["log_level"].as<string>();
        auto it = logger_levels.find(str_level);
        if(it != logger_levels.end())
        {
            log_level = it->second;
        }
        return true;
    }
    catch (std::exception &e)
    {
        cout << "exception type:" << typeid(e).name() << ",error message:" <<  e.what() << endl;
        return false;
    }
}
