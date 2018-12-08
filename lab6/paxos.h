#ifndef paxos_h
#define paxos_h

#include <string>
#include <vector>
#include "rpc.h"
#include "paxos_protocol.h"
#include "log.h"


class paxos_change {
 public:
  virtual void paxos_commit(unsigned instance, std::string v) = 0;
  virtual ~paxos_change() {};
};

class acceptor {
 private:
  log *l;
  rpcs *pxs;
  paxos_change *cfg;
  std::string me;
  pthread_mutex_t pxs_mutex;

  // Acceptor state
  prop_t n_h;		// number of the highest proposal seen in a prepare
  prop_t n_a;		// number of highest proposal accepted
  std::string v_a;	// value of highest proposal accepted
  unsigned instance_h;	// number of the highest instance we have decided
  std::map<unsigned,std::string> values;	// vals of each instance

  void commit_wo(unsigned instance, std::string v);
  paxos_protocol::status preparereq(std::string src, 
          paxos_protocol::preparearg a,
          paxos_protocol::prepareres &r);
  paxos_protocol::status acceptreq(std::string src, 
          paxos_protocol::acceptarg a, bool &r);
  paxos_protocol::status decidereq(std::string src, 
          paxos_protocol::decidearg a, int &r);

  friend class log;

 public:
  acceptor(class paxos_change *cfg, bool _first, std::string _me, 
	std::string _value);
  ~acceptor() {};
  void commit(unsigned instance, std::string v);
  unsigned instance() { return instance_h; }
  std::string value(unsigned instance) { return values[instance]; }
  std::string dump();
  void restore(std::string);
  rpcs *get_rpcs() { return pxs; };
  prop_t get_n_h() { return n_h; };
  unsigned get_instance_h() { return instance_h; };
};

extern bool isamember(std::string m, const std::vector<std::string> &nodes);
extern std::string print_members(const std::vector<std::string> &nodes);

class proposer {
 private:
  log *l;
  paxos_change *cfg;
  acceptor *acc;
  std::string me;
  bool break1;
  bool break2;

  pthread_mutex_t pxs_mutex;

  // Proposer state
  bool stable;
  prop_t my_n;		// number of the last proposal used in this instance

  void setn();
  bool prepare(unsigned instance, std::vector<std::string> &accepts, 
         std::vector<std::string> nodes,
         std::string &v);
  void accept(unsigned instance, std::vector<std::string> &accepts, 
        std::vector<std::string> nodes, std::string v);
  void decide(unsigned instance, std::vector<std::string> accepts,
        std::string v);

  void breakpoint1();
  void breakpoint2();
  bool majority(const std::vector<std::string> &l1, const std::vector<std::string> &l2);

  friend class log;
 public:
  proposer(class paxos_change *cfg, class acceptor *_acceptor, std::string _me);
  ~proposer() {};
  bool run(int instance, std::vector<std::string> cnodes, std::string v);
  bool isrunning();
  void breakpoint(int b);
};



#endif /* paxos_h */
