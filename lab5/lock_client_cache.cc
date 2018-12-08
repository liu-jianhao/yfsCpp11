// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int r;
  int ret = lock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    it = m_lockMap.insert(std::make_pair(lid, lock_entry())).first;
  }

  while(1)
  {
    switch(it->second.state)
    {
      case NONE:
        // 当客户端尝试向服务器获取锁时，状态变为ACQUIRING
        it->second.state = ACQUIRING;
        it->second.retry = false;
        lck.unlock();
        ret = cl->call(lock_protocol::acquire, lid, id, r);
        lck.lock();

        // 成功获得锁
        if(ret == lock_protocol::OK)
        {
          it->second.state = LOCKED;
          return ret;
        }
        // 否则挂起在retryQueue
        else if(ret == lock_protocol::RETRY)
        {
          if(!it->second.retry)
          {
            retryQueue.wait(lck);
          }
        }
        break;

      case FREE:
        it->second.state = LOCKED;
        return ret;
        break;

      case LOCKED:
        waitQueue.wait(lck);
        break;
      
      case ACQUIRING:
        if(!it->second.retry)
        {
          waitQueue.wait(lck);
        }
        else
        {
          it->second.retry = false;
          lck.unlock();
          ret = cl->call(lock_protocol::acquire, lid, id, r);
          lck.lock();

          if(ret == lock_protocol::OK)
          {
            it->second.state = LOCKED;
            return ret;
          }
          else if(ret == lock_protocol::RETRY)
          {
            if(!it->second.retry)
            {
              retryQueue.wait(lck);
            }
          }
        }
        break;

      case RELEASING:
        releaseQueue.wait(lck);
        break;
    }
  }

  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  int r;
  lock_protocol::status ret = lock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    return lock_protocol::NOENT;
  }

  if(it->second.revoked)
  {
    it->second.state = RELEASING;
    it->second.revoked = false;

    lck.unlock();
    lu->dorelease(lid);
    ret = cl->call(lock_protocol::release, lid, id, r);
    lck.lock();

    it->second.state = NONE;
    releaseQueue.notify_all();
  }
  else
  {
    it->second.state = FREE;
    waitQueue.notify_one();
  }

  return ret;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int r;
  int ret = rlock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    return lock_protocol::NOENT;
  }

  if(it->second.state == FREE)
  {
    it->second.state = RELEASING;
    lck.unlock();
    lu->dorelease(lid);
    cl->call(lock_protocol::release, lid, id, r);
    lck.lock();

    it->second.state = NONE;
    releaseQueue.notify_all();
  }
  else
  {
    it->second.revoked = true;
  }

  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    return lock_protocol::NOENT;
  }

  it->second.retry = true;
  retryQueue.notify_one();
  return ret;
}



