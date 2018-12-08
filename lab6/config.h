#ifndef config_h
#define config_h

#include <string>
#include <vector>
#include "paxos.h"

class config_view_change {
 public:
  virtual void commit_change(unsigned vid) = 0;
  virtual ~config_view_change() {};
};

class config : public paxos_change {
 private:
  acceptor *acc;
  proposer *pro;
  rpcs *pxsrpc;
  unsigned myvid;
  std::string first;
  std::string me;
  config_view_change *vc;
  std::vector<std::string> mems;
  pthread_mutex_t cfg_mutex;
  pthread_cond_t heartbeat_cond;
  pthread_cond_t config_cond;
  paxos_protocol::status heartbeat(std::string m, unsigned instance, int &r);
  std::string value(std::vector<std::string> mems);
  std::vector<std::string> members(std::string v);
  std::vector<std::string> get_view_wo(unsigned instance);
  bool remove_wo(std::string);
  void reconstruct();
  typedef enum {
    OK,	// response and same view #
    VIEWERR,	// response but different view #
    FAILURE,	// no response
  } heartbeat_t;
  heartbeat_t doheartbeat(std::string m);
 public:
  config(std::string _first, std::string _me, config_view_change *_vc);
  unsigned vid() { return myvid; }
  std::string myaddr() { return me; };
  std::string dump() { return acc->dump(); };
  std::vector<std::string> get_view(unsigned instance);
  void restore(std::string s);
  bool add(std::string, unsigned vid);
  bool ismember(std::string m, unsigned vid);
  void heartbeater(void);
  void paxos_commit(unsigned instance, std::string v);
  rpcs *get_rpcs() { return acc->get_rpcs(); }
  void breakpoint(int b) { pro->breakpoint(b); }
};

#endif
