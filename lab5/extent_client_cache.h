#ifndef extent_client_cache_h
#define extent_client_cache_h

#include <mutex>
#include "extent_client.h"

class extent_client_cache : public extent_client {
    enum state {
        NONE,       // 文件内容不存在
        UPDATE,     // 缓存了文件内容且文件内容未被修改
        MODIFIED,   // 缓存了文件内容且文件内容已被修改
        REMOVED      // 文件已被删除
    };

    struct extent {
        std::string data;
        state m_state;
        extent_protocol::attr attr;
        extent() : m_state(NONE) {}
    };

private:
    std::mutex m_mutex;
    std::map<extent_protocol::extentid_t, extent> m_cache;

public:
    extent_client_cache(std::string dst);
    extent_protocol::status get(extent_protocol::extentid_t eid,
                                std::string &buf);
    extent_protocol::status getattr(extent_protocol::extentid_t eid,
                                    extent_protocol::attr &a);
    extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
    extent_protocol::status remove(extent_protocol::extentid_t eid);
    extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif