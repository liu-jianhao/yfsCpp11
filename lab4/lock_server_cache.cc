// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
  bool revoke = false;
  lock_protocol::status ret = lock_protocol::OK;

  std::unique_lock<std::mutex> lck(m_mutex);

  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    it = m_lockMap.insert(std::make_pair(lid, lock_entry())).first;
  }

  switch(it->second.state)
  {
    // 此时锁是空闲的，则可以直接分配
    case FREE:
      it->second.state = LOCKED;
      it->second.owner = id;
      break;

    // 此时锁其他客户端占有，添加进等待队列
    case LOCKED:
      it->second.state = LOCKED_AND_WAIT;
      it->second.waitSet.insert(id);
      revoke = true;
      ret = lock_protocol::RETRY;
      break;
    
    case LOCKED_AND_WAIT:
      it->second.waitSet.insert(id);
      ret = lock_protocol::RETRY;
      break;

    case RETRYING:
      if(it->second.waitSet.count(id))
      {
        it->second.waitSet.erase(id);
        it->second.owner = id;
        if(it->second.waitSet.size())
        {
          it->second.state = LOCKED_AND_WAIT;
          revoke = true;
        }
        else
        {
          it->second.state = LOCKED;
        }
      }
      else
      {
        it->second.waitSet.insert(id);
        ret = lock_protocol::RETRY;
      }
      break;
  }

  lck.unlock();

  if(revoke)
  {
    int r;
    handle(it->second.owner).safebind()->call(rlock_protocol::revoke, lid, r);
  }
  
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  bool retry = false;
  std::string client_need_retry;
  lock_protocol::status ret = lock_protocol::OK;
  
  std::unique_lock<std::mutex> lck(m_mutex);
  auto it = m_lockMap.find(lid);
  if(it == m_lockMap.end())
  {
    ret = lock_protocol::NOENT;
  }

  switch(it->second.state)
  {
    // 空闲是不可以free的
    case FREE:
      ret = lock_protocol::IOERR;
      break;

    case LOCKED:
      it->second.state = FREE;
      it->second.owner = "";
      break;
    
    // 如果有其他客户端正在等待该锁，需要提醒他重试
    case LOCKED_AND_WAIT:
      it->second.state = RETRYING;
      it->second.owner = "";
      client_need_retry = *it->second.waitSet.begin();
      retry = true;
      break;

    case RETRYING:
      ret = lock_protocol::IOERR;
      break;
  }

  lck.unlock();

  if(retry)
  {
    handle(client_need_retry).safebind()->call(rlock_protocol::retry, lid, r);
  }

  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

