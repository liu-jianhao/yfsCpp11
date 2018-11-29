# Lab1：Lock Server

## Step One：假设网络正常，实现锁服务器

### 使用RPC
+ 服务器通过创建侦听端口并注册各种RPC处理程序的RPC服务器对象(rpcs)来使用RPC库。
+ 客户端创建RPC客户端对象(rpcc)，要求它连接到`lock_server`的地址和端口，并调用RPC。
+ 每个RPC都有一个唯一的过程标识号，使用RPC服务器对象时要注册这些RPC的处理程序

`lock_smain.cc`中服务器注册RPC调用
```cpp
server.reg(lock_protocol::acquire, &ls, &lock_server::acquire);
server.reg(lock_protocol::release, &ls, &lock_server::release);
```


### 实现锁服务器
+ 用`map`保存锁的状态表，用`lock_protocol::lockid_t`作为key，用锁作为vlaue
+ 锁要自己实现

自己实现的锁类(`lock_protocol.h`)：
```cpp
class lock {
public:
  enum lock_status {FREE, LOCKED};
  // 用这个来标识每个锁
  lock_protocol::lockid_t m_lid;
  // FREE or LOCKED
  int m_state;
  // 条件变量
  std::condition_variable m_cv;

  // 构造函数
  lock(lock_protocol::lockid_t lid, int state);
};
```
`lock_server.cc`
```cpp
lock::lock(lock_protocol::lockid_t lid, int state) : m_lid(lid), m_state(state)
{
}
```

### 实现锁客户端
+ 获得锁：发送RPC`acquire()`
+ 释放锁：接收RPC`release()`

`lock_client.cc`中客户端调用RPC
```cpp
lock_protocol::status
lock_client::acquire(lock_protocol::lockid_t lid)
{
  int r;
  int ret = cl->call(lock_protocol::acquire, cl->id(), lid, r);
  VERIFY (ret == lock_protocol::OK);
  return r;
}

lock_protocol::status
lock_client::release(lock_protocol::lockid_t lid)
{
  int r;
  int ret = cl->call(lock_protocol::release, cl->id(), lid, r);
  VERIFY (ret == lock_protocol::OK);
  return r;
}
```


### 处理多线程并发
+ 访问共享变量时要加锁

`lock_server.cc`中RPC调用的实现
```cpp
lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto iter = m_lockMap.find(lid);
  if (iter != m_lockMap.end())
  {
    while(iter->second->m_state != lock::FREE)
    {
      iter->second->m_cv.wait(lck);
    }
    iter->second->m_state = lock::LOCKED;
  }
  else
  {
    // 没找到就新建一个锁
    auto p_mutex = new lock(lid, lock::LOCKED);
    m_lockMap.insert(std::pair<lock_protocol::lockid_t, lock*>(lid, p_mutex));
  }

  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto iter = m_lockMap.find(lid);
  if (iter != m_lockMap.end())
  {
    iter->second->m_state = lock::FREE;
    iter->second->m_cv.notify_all();
  }
  else
  {
    ret = lock_protocol::IOERR;
  }

  m_mutex.unlock();
  return ret;
}
```

### 测试
先启动服务器
```shell
$ ./lock_server 7749
```

调用测试程序
```shell
$ ./lock_tester localhost:7749
simple lock client
省略...
./lock_tester: passed all tests successfully
```

## Step Two：实现RPC的at-most-once执行
### RPC处理流程
已经提供的RPC代码中已经具有at-most-once执行的客户端实现：客户端在等待响应超时时，重新发送请求，
并为每个请求提供服务器将需要的信息(`req_header`)。

RPCS接收到RPC请求时，调用`rpcs::got_pdu`将请求分派到线程池(`ThrPool`)中的线程。
线程池有固定数量的线程组成，这些线程调用`rpcs::dispatch`将RPC请求分派给相关的已注册RPC处理程序。

但是服务端中还需要完善两个函数`rpcs::checkduplicate_and_update`和`rpcs::add_reply`。

### 怎么实现at-most-once？
这里采用的方法是记录已收到的RPC请求，每个RPC用`xid`和`clt_nonce`来标识，
+ `xid`则用来标识某个客户端的一个请求
+ `clt_nonce`用来标识客户端
由于记录这些请求很消耗内存，所以需要用到滑动窗口，这就需要`xid`是递增的，
每个请求除了上面两个信息之外还需要包含`xid_rep`
+ `xid_rep`用来标识这个请求的客户端已经接收到回复了
有了这个信息服务端就可以清楚已经收到回复的这些请求

### 测试
先将将RPC_LOSSY设置为5：
```shell
$ export RPC_LOSSY=5
```

再次如Setp One那样测试，同样能通过测试。