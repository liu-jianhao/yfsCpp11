// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include <mutex>
#include <condition_variable>

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    stat
  };
};

// 锁类
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

#endif 
