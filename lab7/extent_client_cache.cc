#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "extent_client_cache.h"

extent_client_cache::extent_client_cache(std::string dst) : extent_client(dst)
{
}

extent_protocol::status
extent_client_cache::get(extent_protocol::extentid_t eid, std::string &buf)
{
    extent_protocol::status ret = extent_protocol::OK;

    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_cache.count(eid))
    {
        switch (m_cache[eid].m_state)
        {
            case UPDATE:
            case MODIFIED:
                // 直接从缓存获取数据
                buf = m_cache[eid].data;
                m_cache[eid].attr.atime = time(NULL);
                break;

            case NONE:
                ret = cl->call(extent_protocol::get, eid, buf);
                // 更新缓存
                if(ret == extent_protocol::OK)
                {
                    m_cache[eid].data = buf;
                    m_cache[eid].m_state = UPDATE;
                    m_cache[eid].attr.atime = time(NULL);
                    m_cache[eid].attr.size = buf.size();
                }
                break;

            case REMOVED:
                ret = extent_protocol::NOENT;
                break;
        }
    }
    // 缓存中没有数据
    else
    {
        ret = cl->call(extent_protocol::get, eid, buf);
        if (ret == extent_protocol::OK)
        {
            m_cache[eid].data = buf;
            m_cache[eid].m_state = UPDATE;
            m_cache[eid].attr.atime = time(NULL);
            m_cache[eid].attr.size = buf.size();
            m_cache[eid].attr.ctime = 0;
            m_cache[eid].attr.mtime = 0;
        }
    }

    return ret;
}

extent_protocol::status
extent_client_cache::getattr(extent_protocol::extentid_t eid,
                             extent_protocol::attr &attr)
{
    extent_protocol::status ret = extent_protocol::OK;

    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_cache.count(eid))
    {
        switch (m_cache[eid].m_state)
        {
            case UPDATE:
            case MODIFIED:
            case NONE:
                attr = m_cache[eid].attr;
                break;

            case REMOVED:
                ret = extent_protocol::NOENT;
                break;
        }
    }
    else
    {
        m_cache[eid].m_state = NONE;
        ret = cl->call(extent_protocol::getattr, eid, attr);
        if(ret == extent_protocol::OK)
        {
            m_cache[eid].attr.atime = attr.atime;
            m_cache[eid].attr.ctime = attr.ctime;
            m_cache[eid].attr.mtime = attr.mtime;
            m_cache[eid].attr.size = attr.size;
        }
    }

    return ret;
}

extent_protocol::status
extent_client_cache::put(extent_protocol::extentid_t eid, std::string buf)
{
    extent_protocol::status ret = extent_protocol::OK;

    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_cache.count(eid))
    {
        switch (m_cache[eid].m_state)
        {
            case NONE:
            case UPDATE:
            case MODIFIED:
                m_cache[eid].data = buf;
                m_cache[eid].m_state = MODIFIED;
                m_cache[eid].attr.mtime = time(NULL);
                m_cache[eid].attr.ctime = time(NULL);
                m_cache[eid].attr.size = buf.size();
                break;

            case REMOVED:
                ret = extent_protocol::NOENT;
                break;
        }
    }
    else
    {
        m_cache[eid].data = buf;
        m_cache[eid].m_state = MODIFIED;
        m_cache[eid].attr.mtime = time(NULL);
        m_cache[eid].attr.ctime = time(NULL);
        m_cache[eid].attr.size = buf.size();
    }

    return ret;
}

extent_protocol::status
extent_client_cache::remove(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;

    std::lock_guard<std::mutex> lg(m_mutex);

    if (m_cache.count(eid))
    {
        switch (m_cache[eid].m_state)
        {
        case NONE:
        case UPDATE:
        case MODIFIED:
            m_cache[eid].m_state = REMOVED;
            break;

        case REMOVED:
            ret = extent_protocol::NOENT;
            break;
        }
    }
    else
    {
        m_cache[eid].m_state = REMOVED;
    }

    return ret;
}

extent_protocol::status
extent_client_cache::flush(extent_protocol::extentid_t eid)
{
    extent_protocol::status ret = extent_protocol::OK;
    int r;

    std::lock_guard<std::mutex> lg(m_mutex);

    if(m_cache.count(eid))
    {
        switch (m_cache[eid].m_state)
        {
            case MODIFIED:
                ret = cl->call(extent_protocol::put, eid, m_cache[eid].data, r);
                break;

            case REMOVED:
                ret = cl->call(extent_protocol::remove, eid);
                break;
            
            case NONE:
            case UPDATE:
                break;
        }
        m_cache.erase(eid);
    }
    else
    {
        ret = extent_protocol::NOENT;
    }

    return ret;
}