#ifndef lock_server_cache_rsm_h
#define lock_server_cache_rsm_h

#include <string>
#include <set>
#include <mutex>

#include "lock_protocol.h"
#include "rpc.h"
#include "rsm_state_transfer.h"
#include "rsm.h"
#include "fifo.h"

class lock_server_cache_rsm : public rsm_state_transfer
{
  private:
    class rsm *rsm;
    int nacquire;

    enum lock_state {
        FREE,
        LOCKED,
        LOCKED_AND_WAIT,
        RETRYING
    };

    struct lock_entry {
        lock_state state;
        std::string owner;
        bool revoked;
        std::set<std::string> waitSet;

        std::map<std::string, lock_protocol::xid_t> highest_xid_from_client;
        std::map<std::string, int> highest_xid_acquire_reply;
        std::map<std::string, int> highest_xid_release_reply;

        lock_entry() : state(FREE), revoked(false) {}
    };

    std::map<lock_protocol::lockid_t, lock_entry> m_lockMap;
    std::mutex m_mutex;

    struct revoke_retry_entry {
        std::string id;
        lock_protocol::lockid_t lid;
        lock_protocol::xid_t xid;

        revoke_retry_entry(const std::string& id_ = "", lock_protocol::lockid_t lid_ = 0, lock_protocol::xid_t xid_ = 0)
                                : id(id_), lid(lid_), xid(xid_) {}
    };

    fifo<revoke_retry_entry> retryQueue;
    fifo<revoke_retry_entry> revokeQueue;

  public:
    lock_server_cache_rsm(class rsm *rsm = 0);
    lock_protocol::status stat(lock_protocol::lockid_t, int &);
    void revoker();
    void retryer();
    std::string marshal_state();
    void unmarshal_state(std::string state);
    int acquire(lock_protocol::lockid_t, std::string id,
                lock_protocol::xid_t, int &);
    int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
                int &);
};

#endif
