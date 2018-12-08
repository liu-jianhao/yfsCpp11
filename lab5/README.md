# Lab5：扩展缓存
这次实验需要为YFS添加扩展缓存，这样可以减小扩展服务器的负载、提高YFS的性能。

这次实验的主要难点是确保扩展缓存的一致性：确保每个客户端都能看到最新的修改。
我们可以通过lab4的缓存锁服务来实现一致性。


## 扩展缓存
用`extent_client_cache`继承`extent_client`，还要把`extent_client`中的`get`等方法改为虚函数，
这样之前的通过指针调用就会调用到子类的方法。
```cpp
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
```

之后要实现`get`等虚函数：
```cpp
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
```

## 缓存一致性
因为在文件的读写都在文件缓存中进行,为了保证一致性(即读操作获取的内容必须是最近的写操作写的内容),

yfs采用**释放一致性**来保证一致性. 因为yfs中文件的id(i-number号)和锁id时同样的值，当释放一个锁回锁服务器时，必须确保文件内容客户端(extent_client)中对应的缓存文件也flush回了文件内容服务(extent_server).并且从缓存中删除这个文件，

flush操作检查文件内容是否已经修改，如果是则讲新的内容put到文件内容服务.如果文件被删除(即状态REMOVED),那么从文件内容服务上删除这个文件.

例如客户端A获取一个文件的锁，然后从文件内容服务get文件的内容.并在本地缓存中修改这个文件的内容，
此时客户端B也尝试获取这个文件的锁，这时锁服务器给客户端A发送revoke消息，然后客户端A在将锁释放回锁服务器前先把已修改的文件内容flush回文件内容服务，

然后客户端B会获取到这个锁，在从文件内容服务get文件内容(B的缓存中不会已经缓存了这个文件,所以必须访问文件内容服务.如果曾经缓存了，在释放锁时也将这个项缓存删除了).这时客户端B获取到的内容就是A修改后的内容.

在`yfs_client`中需要用到这个类，在发送`release`RPC之前需要调用`dorelease`。
```cpp
class lock_user : public lock_release_user {
private:
  extent_client_cache *ec;
public:
  lock_user(extent_client_cache *e) : ec(e) {}
  // dorelease在将锁释放回服务器时调用
  void dorelease(lock_protocol::lockid_t lid)
  {
    ec->flush(lid); 
  }
};
```


## 测试
在完成lab5之前：
```
$ export RPC_COUNT=25
$ ./start.sh
$ ./test-lab-3-c ./yfs1 ./yfs2
Create/delete in separate directories: tests completed OK
$ grep "RPC STATS" extent_server.log
...
RPC STATS: 1:2 6001:804 6002:2218 6003:1878 6004:198
```
可以看到很多PRC请求(省略了一百多个)。

之后:
```
$ grep "RPC STATS" extent_server.log
grep "RPC STATS" extent_server.log
RPC STATS: 1:2 6002:4 6003:19 
RPC STATS: 1:2 6002:4 6003:44 
RPC STATS: 1:2 6001:3 6002:17 6003:53 
RPC STATS: 1:2 6001:3 6002:38 6003:57 
RPC STATS: 1:2 6001:3 6002:59 6003:61 
RPC STATS: 1:2 6001:3 6002:82 6003:63 
RPC STATS: 1:2 6001:3 6002:104 6003:66 
RPC STATS: 1:2 6001:3 6002:125 6003:70 
RPC STATS: 1:2 6001:3 6002:147 6003:73 
RPC STATS: 1:2 6001:3 6002:169 6003:76 
RPC STATS: 1:2 6001:3 6002:191 6003:79 
RPC STATS: 1:2 6001:3 6002:213 6003:82 
RPC STATS: 1:2 6001:3 6002:235 6003:85 
RPC STATS: 1:2 6001:4 6002:254 6003:90 
RPC STATS: 1:2 6001:5 6002:263 6003:105 
```
可以看出RPC请求少了许多。