/*
   Copyright 2018 Marius Appel <marius.appel@uni-muenster.de>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef SERVER_H
#define SERVER_H

#include <cpprest/http_listener.h>
#include <cpprest/uri_builder.h>
#include <boost/filesystem.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include "cube.h"

class server_chunk_cache {
   public:
    static server_chunk_cache* instance() {
        static GC g;
        _singleton_mutex.lock();
        if (!_instance) {
            _instance = new server_chunk_cache();
        }
        _singleton_mutex.unlock();
        return _instance;
    }

    void remove(std::pair<uint32_t, uint32_t> key) {
        _m.lock();
        auto it = _cache.find(key);
        if (it != _cache.end()) {
            _size_bytes -= it->second->total_size_bytes();
            _cache.erase(it);
        }

        auto it_2 = _prio_forward.find(key);
        uint32_t p = it_2->second;
        if (it_2 != _prio_forward.end()) {
            _prio_forward.erase(it_2);
        }

        auto it_3 = _prio_backward.find(p);
        if (it_3 != _prio_backward.end()) {
            _prio_backward.erase(it_3);
        }
        _m.unlock();
    }

    void add(std::pair<uint32_t, uint32_t> key, std::shared_ptr<chunk_data> value) {
        if (!has(key)) {
            _m.lock();
            while (_size_bytes + value->total_size_bytes() > config::instance()->get_server_chunkcache_max()) {
                auto it = _prio_backward.lower_bound(0);  // lowest value greater than or equal to 0
                uint32_t p = it->first;

                // remove it from the data
                remove(it->second);
            }

            _cache[key] = value;
            _size_bytes += value->total_size_bytes();

            uint64_t p = inc_prio();  // synchronize?

            _prio_forward[key] = p;
            _prio_backward[p] = key;

            _size_bytes += value->total_size_bytes();
            _m.unlock();
        }
    }

    inline bool has(std::pair<uint32_t, uint32_t> key) {
        return _cache.find(key) != _cache.end();
    }

    inline std::shared_ptr<chunk_data> get(std::pair<uint32_t, uint32_t> key) {
        if (!has(key)) {
            throw std::string("ERROR: in server_chunk_cache::get(): requested chunk is not available");
        }
        _m.lock();
        uint64_t pnew = inc_prio();
        auto it_1 = _prio_forward.find(key);
        if (it_1 != _prio_forward.end()) {
            uint32_t pold = it_1->second;
            it_1->second = pnew;

            auto it_2 = _prio_backward.find(pold);
            if (it_2 != _prio_backward.end()) {
                _prio_backward.erase(it_2);  // remove old
                _prio_backward[pnew] = key;  // add new
            }
        }
        _m.unlock();
        return _cache[key];
    }

    inline uint32_t total_size_bytes() {
        return _size_bytes;
    }

   private:
    const uint64_t inc_prio() {
        if (_cur_priority == std::numeric_limits<uint64_t>::max()) {
            uint64_t newp = 0;
            for (auto it = _prio_forward.begin(); it != _prio_forward.end(); ++it) {
                it->second = newp++;
                _prio_backward.insert(std::make_pair(it->second, it->first));
            }
            _cur_priority = newp;
        } else {
            ++_cur_priority;
        }
        return _cur_priority;
    }

    std::map<std::pair<uint32_t, uint32_t>, std::shared_ptr<chunk_data>> _cache;
    std::map<uint64_t, std::pair<uint32_t, uint32_t>> _prio_backward;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> _prio_forward;

    std::mutex _m;
    uint64_t _size_bytes;

    static std::mutex _singleton_mutex;
    uint64_t _cur_priority;

   private:
    server_chunk_cache() : _cache(), _size_bytes(0), _m(), _prio_backward(), _prio_forward(), _cur_priority(0) {}
    ~server_chunk_cache() {}
    server_chunk_cache(const server_chunk_cache&) = delete;
    static server_chunk_cache* _instance;

    class GC {
       public:
        ~GC() {
            if (server_chunk_cache::_instance) {
                delete server_chunk_cache::_instance;
                server_chunk_cache::_instance = nullptr;
            }
        }
    };
};

class gdalcubes_server {
   public:
    gdalcubes_server(std::string host, uint16_t port = 1111, std::string basepath = "gdalcubes/api/", bool ssl = false, boost::filesystem::path workdir = boost::filesystem::temp_directory_path() / "gdalcubes_server", std::set<std::string> whitelist = {}) : _host(host), _worker_cond(), _chunk_read_requests_set(), _mutex_worker_cond(), _mutex_chunk_read_executing(), _mutex_worker_threads(), _cur_id(0), _mutex_id(), _mutex_cubestore(), _port(port), _ssl(ssl), _basepath(basepath), _worker_thread_count(0), _cubestore(), _worker_threads(), _workdir(workdir), _listener(), _whitelist(whitelist) {
        if (boost::filesystem::exists(_workdir) && boost::filesystem::is_directory(_workdir)) {
            // boost::filesystem::remove_all(_workdir); // TODO: uncomment after testing
        } else if (boost::filesystem::exists(_workdir) && !boost::filesystem::is_directory(_workdir)) {
            throw std::string("ERROR in gdalcubes_server::gdalcubes_server(): working directory for gdalcubes_server is an existing file.");
        }
        boost::filesystem::create_directory(_workdir);
        boost::filesystem::current_path(_workdir);

        std::string url = ssl ? "https://" : "http://";
        url += host + ":" + std::to_string(port);
        web::uri_builder uri(url);
        uri.append_path(basepath);

        _listener = web::http::experimental::listener::http_listener(uri.to_string());

        _listener.support(web::http::methods::GET, std::bind(&gdalcubes_server::handle_get, this, std::placeholders::_1));
        _listener.support(web::http::methods::POST, std::bind(&gdalcubes_server::handle_post, this, std::placeholders::_1));
        _listener.support(web::http::methods::HEAD, std::bind(&gdalcubes_server::handle_head, this, std::placeholders::_1));
    }

   public:
    inline pplx::task<void> open() { return _listener.open(); }
    inline pplx::task<void> close() { return _listener.close(); }

    inline std::string get_service_url() { return _listener.uri().to_string(); }

   protected:
    void handle_get(web::http::http_request req);
    void handle_post(web::http::http_request req);
    void handle_head(web::http::http_request req);

    web::http::experimental::listener::http_listener _listener;

    inline uint32_t get_unique_id() {
        _mutex_id.lock();
        uint32_t id = _cur_id++;
        _mutex_id.unlock();
        return id;
    }

    const uint16_t _port;
    const std::string _host;
    const std::string _basepath;
    const bool _ssl;
    const boost::filesystem::path _workdir;

    std::map<uint32_t, std::shared_ptr<cube>> _cubestore;

    uint16_t _cur_id;
    std::mutex _mutex_id;
    std::mutex _mutex_cubestore;

    uint16_t _worker_thread_count;
    std::mutex _mutex_worker_thread_count;

    std::mutex _mutex_chunk_read_requests;
    std::list<std::pair<uint32_t, uint32_t>> _chunk_read_requests;
    std::set<std::pair<uint32_t, uint32_t>> _chunk_read_requests_set;

    std::mutex _mutex_chunk_read_executing;
    std::set<std::pair<uint32_t, uint32_t>> _chunk_read_executing;  // length = nthreads, TODO: read nthreads from gdalcubes_server command line or config file

    std::vector<std::thread> _worker_threads;
    std::mutex _mutex_worker_threads;

    std::condition_variable _worker_cond;
    std::mutex _mutex_worker_cond;

    std::map<std::pair<uint32_t, uint32_t>, std::pair<std::condition_variable, std::mutex>> _chunk_cond;

    std::set<std::string> _whitelist;
};

#endif  //SERVER_H