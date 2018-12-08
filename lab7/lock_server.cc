// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock::lock(lock_protocol::lockid_t lid, int state) : m_lid(lid), m_state(state)
{
}


lock_server::lock_server():
  nacquire (0)
{
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

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