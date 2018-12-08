// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include <mutex>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

#include <memory>

class lock_server {

 protected:
  int nacquire;

  std::mutex m_mutex;
  std::map<lock_protocol::lockid_t, lock*> m_lockMap; 

 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);

  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







