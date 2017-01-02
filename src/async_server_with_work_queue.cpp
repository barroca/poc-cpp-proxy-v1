/*
 * sample application to show the usage of work queues along with async http server
 *
 * (C) Copyright Dino Korah 2013.
 * Distributed under the Boost Software License, Version 1.0. (See copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/network/include/http/server.hpp>
#include <boost/network/uri.hpp>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>
#include <list>
#include <signal.h>

#define Log(line) \
    do { std::cout << line << std::endl; } while(false)

struct handler;
typedef boost::network::http::async_server<handler> server;

using boost::asio::ip::tcp;
/**
 * request + connection encapsulation (work item)
 */
struct request_data
{
    
    typedef boost::shared_ptr< request_data > pointer;
    const server::request req;
    server::connection_ptr conn;
    std::string _uri_string;
 
    request_data(server::request const& req, const server::connection_ptr& conn) :
        req(req), conn(conn)
    {
        namespace uri = boost::network::uri;
        
        using namespace boost::network::http;
        this->_uri_string = destination(req);

    }
};

/**
 * A basic work queue
 */
struct work_queue
{
    typedef std::list<request_data::pointer> list;

    list requests;
    boost::mutex mutex;

    inline void put(const request_data::pointer& p_rd)
    {
        boost::unique_lock< boost::mutex > lock(mutex);
        requests.push_back(p_rd);
        (void)lock;
    }

    inline request_data::pointer get()
    {
        boost::unique_lock< boost::mutex > lock(mutex);

        request_data::pointer p_ret;
        if (!requests.empty()) {
            p_ret = requests.front();
            requests.pop_front();
        }

        (void)lock;

        return p_ret;
    }
};

struct handler
{
    work_queue& queue;

    handler(work_queue& queue) : queue(queue) { }

    /**
     * Feed the work queue
     *
     * @param req
     * @param conn
     */
    void operator()(server::request const& req, const server::connection_ptr& conn)
    {
        queue.put(boost::make_shared<request_data>(req, conn));
    }
};

/**
 * Clean shutdown signal handler
 *
 * @param error
 * @param signal
 * @param p_server_instance
 */
void shut_me_down(
        const boost::system::error_code& error
        , int signal, boost::shared_ptr< server > p_server_instance)
{
    if (!error)
        p_server_instance->stop();
}
std::string make_string(boost::asio::streambuf& streambuf)
{
    return {boost::asio::buffers_begin(streambuf.data()),
        boost::asio::buffers_end(streambuf.data())};
}
/**
 * Process request; worker (thread)
 *
 * @param queue
 */
void process_request(work_queue& queue)
{
    const char* env_p = std::getenv("SEARCH_SERVER");
    while(!boost::this_thread::interruption_requested()) {
        request_data::pointer p_req(queue.get());
        if (p_req) {
            std::string st = p_req->_uri_string;
            boost::replace_all(st, "/?q=", "");
            boost::replace_all(st, "+", " ");
            
           
            boost::asio::io_service io_service;
            
            tcp::resolver resolver(io_service);
            tcp::resolver::query query(tcp::v4(), env_p, "5000");
            tcp::resolver::iterator iterator = resolver.resolve(query);
            
            boost::system::error_code ec;
            tcp::socket s(io_service);
            s.connect(*iterator);
            
            boost::asio::write(s, boost::asio::buffer(st, st.length()));
            
            boost::asio::streambuf b;
            b.prepare(1024);
            boost::asio::read(s,b,ec);
            
            if (!ec || ec == boost::asio::error::eof) {
                // woot, no problem
            }
            p_req->conn->set_status(server::connection::ok);
            p_req->conn->write("<html><body>"+make_string(b)+"</body></html>");
            
            
        }

    }
}



int main(int argc, char ** argv) try
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " address port" << std::endl;
        return 1;
    }
    // the thread group
    boost::shared_ptr< boost::thread_group > p_threads(
            boost::make_shared< boost::thread_group>());

    // setup asio::io_service
    boost::shared_ptr< boost::asio::io_service > p_io_service(
            boost::make_shared< boost::asio::io_service >());
    boost::shared_ptr< boost::asio::io_service::work > p_work(
            boost::make_shared< boost::asio::io_service::work >(
                    boost::ref(*p_io_service)));

    // io_service threads
    {
        int n_threads = 5;
        while(0 < n_threads--) {
            p_threads->create_thread(
                    boost::bind(&boost::asio::io_service::run, p_io_service));
        }
    }

    // the shared work queue
    work_queue queue;

    // worker threads that will process the request; off the queue
    {
        int n_threads = 5;
        while(0 < n_threads--) {
            p_threads->create_thread(
                    boost::bind(process_request, boost::ref(queue)));
        }
    }

    // setup the async server
    handler request_handler(queue);
    boost::shared_ptr< server > p_server_instance(
            boost::make_shared<server>(
                    server::options(request_handler).
                            address(argv[1])
                            .port(argv[2])
                            .io_service(p_io_service)
                            .reuse_address(true)
                            .thread_pool(
                                    boost::make_shared<boost::network::utils::thread_pool>(
                                            2 , p_io_service, p_threads))));

    // setup clean shutdown
    boost::asio::signal_set signals(*p_io_service, SIGINT, SIGTERM);
    signals.async_wait(boost::bind(shut_me_down, _1, _2, p_server_instance));

    // run the async server
    p_server_instance->run();

    // we are stopped - shutting down

    p_threads->interrupt_all();

    p_work.reset();
    p_io_service->stop();

    p_threads->join_all();

    Log("Terminated normally");
    exit(EXIT_SUCCESS);
}
catch(const std::exception& e)
{
    Log("Abnormal termination - exception:"<<e.what());
    exit(EXIT_FAILURE);
}
