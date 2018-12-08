#ifndef log_h
#define log_h

#include <string>
#include <vector>


class acceptor;

class log {
 private:
  std::string name;
  acceptor *pxs;
 public:
  log (acceptor*, std::string _me);
  std::string dump();
  void restore(std::string s);
  void logread(void);
  /* Log a committed paxos instance*/
  void loginstance(unsigned instance, std::string v);
  /* Log the highest proposal number that the local paxos acceptor has ever seen */
  void logprop(prop_t n_h);
  /* Log the proposal (proposal number and value) that the local paxos acceptor 
     accept has ever accepted */
  void logaccept(prop_t n_a, std::string v);
};

#endif /* log_h */
