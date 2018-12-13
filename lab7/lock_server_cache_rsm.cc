// the caching lock server implementation

#include "lock_server_cache_rsm.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
revokethread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->retryer();
  return 0;
}

lock_server_cache_rsm::lock_server_cache_rsm(class rsm *_rsm) 
  : rsm (_rsm)
{
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  VERIFY (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  VERIFY (r == 0);

  rsm->set_state_transfer(this);
}

void
lock_server_cache_rsm::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
  while (true)
  {
    revoke_retry_entry e;
    revokeQueue.deq(&e);

    if (rsm->amiprimary())
    {
      int r;
      rpcc *cl = handle(e.id).safebind();
      cl->call(rlock_protocol::revoke, e.lid, e.xid, r);
    }
  }
}


void
lock_server_cache_rsm::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
  while (true)
  {
    revoke_retry_entry e;
    retryQueue.deq(&e);

    if (rsm->amiprimary())
    {
      int r;
      rpcc *cl = handle(e.id).safebind();
      cl->call(rlock_protocol::retry, e.lid, e.xid, r);
    }
  }
}


int lock_server_cache_rsm::acquire(lock_protocol::lockid_t lid, std::string id, 
             lock_protocol::xid_t xid, int &)
{
  lock_protocol::status ret = lock_protocol::OK;
  std::lock_guard<std::mutex> lg(m_mutex);
  auto it = m_lockMap.find(lid);
  if (it == m_lockMap.end())
  {
    it = m_lockMap.insert(std::make_pair(lid, lock_entry())).first;
  }

  lock_entry &le = it->second;
  auto xid_it = le.highest_xid_from_client.find(id);

  if (xid_it == le.highest_xid_from_client.end() || xid_it->second < xid)
  {

    le.highest_xid_from_client[id] = xid;
    le.highest_xid_release_reply.erase(id);

    switch (it->second.state)
    {
    case FREE:
      it->second.state = LOCKED;
      it->second.owner = id;
      break;

    case LOCKED:
      it->second.state = LOCKED_AND_WAIT;
      it->second.waitSet.insert(id);
      revokeQueue.enq(revoke_retry_entry(it->second.owner, lid, le.highest_xid_from_client[it->second.owner]));
      ret = lock_protocol::RETRY;
      break;

    case LOCKED_AND_WAIT:
      it->second.waitSet.insert(id);
      revokeQueue.enq(revoke_retry_entry(it->second.owner, lid, le.highest_xid_from_client[it->second.owner]));
      ret = lock_protocol::RETRY;
      break;

    case RETRYING:
      if (it->second.waitSet.count(id))
      {
        it->second.waitSet.erase(id);
        it->second.owner = id;
        if (it->second.waitSet.size())
        {
          it->second.state = LOCKED_AND_WAIT;
          revokeQueue.enq(revoke_retry_entry(it->second.owner, lid, le.highest_xid_from_client[it->second.owner]));
        }
        else
          it->second.state = LOCKED;
      }
      else
      {
        it->second.waitSet.insert(id);
        ret = lock_protocol::RETRY;
      }
      break;
    }
    le.highest_xid_acquire_reply[id] = ret;
  }
  else if (xid_it->second == xid)
  {
    ret = le.highest_xid_acquire_reply[id];
  }

  return ret;
}

int 
lock_server_cache_rsm::release(lock_protocol::lockid_t lid, std::string id, 
         lock_protocol::xid_t xid, int &r)
{
  std::string client_need_retry;
  lock_protocol::status ret = lock_protocol::OK;
  std::lock_guard<std::mutex> lg(m_mutex);
  auto it = m_lockMap.find(lid);
  if (it == m_lockMap.end())
  {
    return lock_protocol::NOENT;
  }

  lock_entry &le = it->second;

  auto xid_it = le.highest_xid_from_client.find(id);
  if (xid_it != le.highest_xid_from_client.end() && xid == xid_it->second)
  {

    auto reply_it = le.highest_xid_release_reply.find(id);

    if (reply_it == le.highest_xid_release_reply.end())
    {

      if (it->second.owner != id)
      {
        ret = lock_protocol::IOERR;
        le.highest_xid_release_reply.insert(make_pair(id, ret));
      }
      switch (it->second.state)
      {
      case FREE:
        ret = lock_protocol::IOERR;
        break;

      case LOCKED:
        it->second.state = FREE;
        it->second.owner = "";
        break;

      case LOCKED_AND_WAIT:
        it->second.state = RETRYING;
        it->second.owner = "";
        client_need_retry = *it->second.waitSet.begin();
        retryQueue.enq(revoke_retry_entry(client_need_retry, lid, le.highest_xid_from_client[client_need_retry]));
        break;

      case RETRYING:
        ret = lock_protocol::IOERR;
        break;
      }
      le.highest_xid_release_reply.insert(make_pair(id, ret));
    }
    else
    {
      ret = reply_it->second;
    }
  }
  else
  {
    ret = lock_protocol::RPCERR;
  }

  return ret;
}

std::string
lock_server_cache_rsm::marshal_state()
{
  std::ostringstream ost;
  std::string r;

  std::lock_guard<std::mutex> lg(m_mutex);
  marshall m;
  unsigned int size = m_lockMap.size();
  m << size;
  for(auto it = m_lockMap.begin(); it != m_lockMap.end(); ++it)
  {
    m << it->first;
    m << static_cast<unsigned int>(it->second.state);
    m << it->second.owner;
    m << it->second.revoked;

    size = it->second.waitSet.size();
    m << size;
    for(auto it_set = it->second.waitSet.begin(); it_set != it->second.waitSet.end(); ++it_set)
    {
      m << *it_set;
    }

    size = it->second.highest_xid_from_client.size();
    m << size;
    for(auto it_client = it->second.highest_xid_from_client.begin();
          it_client != it->second.highest_xid_from_client.end(); ++it_client)
    {
      m << it_client->first;
      m << it_client->second;
    }

    size = it->second.highest_xid_acquire_reply.size();
    m << size;
    for(auto it_acquire = it->second.highest_xid_acquire_reply.begin();
          it_acquire != it->second.highest_xid_acquire_reply.end(); ++it_acquire)
    {
      m << it_acquire->first;
      m << it_acquire->second;
    }

    size = it->second.highest_xid_release_reply.size();
    m << size;
    for(auto it_release = it->second.highest_xid_release_reply.begin();
          it_release != it->second.highest_xid_release_reply.end(); ++it_release)
    {
      m << it_release->first;
      m << it_release->second;
    }
  }
  r = m.str();


  return r;
}

void
lock_server_cache_rsm::unmarshal_state(std::string state)
{
  std::lock_guard<std::mutex> lg(m_mutex);
  
  unmarshall m;
  unsigned int size;
  m >> size;
  for(auto it = m_lockMap.begin(); it != m_lockMap.end(); ++it)
  {
    lock_protocol::lockid_t lid;
    m >> lid;
    lock_entry *entry = new lock_entry();
    unsigned int state;
    m >> state;
    entry->state = static_cast<lock_state>(state);
    m >> entry->owner;
    m >> entry->revoked;

    unsigned int waitSet_size;
    m >> waitSet_size; 
    std::string waitid;
    for(unsigned int i = 0; i < waitSet_size; ++i)
    {
      m >> waitid;
      entry->waitSet.insert(waitid);
    }

    unsigned int xid_size;
    m >> xid_size;
		std::string client_id;
		lock_protocol::xid_t xid;
		for (unsigned int i = 0; i < xid_size; i++) {
			m >> client_id;
			m >> xid;
		   	entry->highest_xid_from_client[client_id] = xid;
		}
		unsigned int reply_size;
		m >> reply_size;
		int ret;
		for (unsigned int i = 0; i < reply_size; i++) {
			m >> client_id;
			m >> ret;
			entry->highest_xid_acquire_reply[client_id] = ret; 
		}
		m >> reply_size;
		for (unsigned int i = 0; i < reply_size; i++) {
			m >> client_id;
			m >> ret;
			entry->highest_xid_release_reply[client_id] = ret;
		}
		m_lockMap[lid] = *entry;
  }
}

lock_protocol::status
lock_server_cache_rsm::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

