#ifndef paxos_protocol_h
#define paxos_protocol_h

#include "rpc.h"

struct prop_t {
  unsigned n;
  std::string m;
};

class paxos_protocol {
 public:
  enum xxstatus { OK, ERR };
  typedef int status;
  enum rpc_numbers {
    preparereq = 0x11001,
    acceptreq,
    decidereq,
    heartbeat,
  };

  struct preparearg {
    unsigned instance;
    prop_t n;
  };

  struct prepareres {
    bool oldinstance;
    bool accept;
    prop_t n_a;
    std::string v_a;
  };

  struct acceptarg {
    unsigned instance;
    prop_t n;
    std::string v;
  };

  struct decidearg {
    unsigned instance;
    std::string v;
  };

};

inline unmarshall &
operator>>(unmarshall &u, prop_t &a)
{
  u >> a.n;
  u >> a.m;
  return u;
}

inline marshall &
operator<<(marshall &m, prop_t a)
{
  m << a.n;
  m << a.m;
  return m;
}

inline unmarshall &
operator>>(unmarshall &u, paxos_protocol::preparearg &a)
{
  u >> a.instance;
  u >> a.n;
  return u;
}

inline marshall &
operator<<(marshall &m, paxos_protocol::preparearg a)
{
  m << a.instance;
  m << a.n;
  return m;
}

inline unmarshall &
operator>>(unmarshall &u, paxos_protocol::prepareres &r)
{
  u >> r.oldinstance;
  u >> r.accept;
  u >> r.n_a;
  u >> r.v_a;
  return u;
}

inline marshall &
operator<<(marshall &m, paxos_protocol::prepareres r)
{
  m << r.oldinstance;
  m << r.accept;
  m << r.n_a;
  m << r.v_a;
  return m;
}

inline unmarshall &
operator>>(unmarshall &u, paxos_protocol::acceptarg &a)
{
  u >> a.instance;
  u >> a.n;
  u >> a.v;
  return u;
}

inline marshall &
operator<<(marshall &m, paxos_protocol::acceptarg a)
{
  m << a.instance;
  m << a.n;
  m << a.v;
  return m;
}

inline unmarshall &
operator>>(unmarshall &u, paxos_protocol::decidearg &a)
{
  u >> a.instance;
  u >> a.v;
  return u;
}

inline marshall &
operator<<(marshall &m, paxos_protocol::decidearg a)
{
  m << a.instance;
  m << a.v;
  return m;
}

#endif
