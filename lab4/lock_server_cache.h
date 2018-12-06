#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include <set>
#include <mutex>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
 private:
  int nacquire;
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);

private:
  enum lock_state {
    FREE,
    LOCKED,
    LOCKED_AND_WAIT,
    RETRYING
  };

  struct lock_entry {
    // 记录锁持有者的地址
    std::string owner;
    // 记录锁等待的所有客户
    std::set<std::string> waitSet;
    bool revoked;
    lock_state state;
    lock_entry() : revoked(false), state(FREE) {}
  };

  std::map<lock_protocol::lockid_t, lock_entry> m_lockMap;
  std::mutex m_mutex;
};

#endif
