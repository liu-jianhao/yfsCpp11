#include "paxos.h"
#include <fstream>
#include <iostream>

// Paxos must maintain some durable state (i.e., that survives power
// failures) to run Paxos correct.  This module implements a log with
// all durable state to run Paxos.  Since the values chosen correspond
// to views, the log contains all views since the beginning of time.

log::log(acceptor *_acc, std::string _me)
  : pxs (_acc)
{
  name = "paxos-" + _me + ".log";
  logread();
}

void
log::logread(void)
{
  std::ifstream from;
  std::string type;
  unsigned instance;

  from.open(name.c_str());
  printf ("logread\n");
  while (from >> type) {
    if (type == "done") {
      std::string v;
      from >> instance;
      from.get();
      getline(from, v);
      pxs->values[instance] = v;
      pxs->instance_h = instance;
      printf ("logread: instance: %d w. v = %s\n", instance, 
	      pxs->values[instance].c_str());
      pxs->v_a.clear();
      pxs->n_h.n = 0;
      pxs->n_a.n = 0;
    } else if (type == "propseen") {
      from >> pxs->n_h.n;
      from >> pxs->n_h.m;
      printf("logread: high update: %d(%s)\n", pxs->n_h.n, pxs->n_h.m.c_str());
    } else if (type == "accepted") {
      std::string v;
      from >> pxs->n_a.n;
      from >> pxs->n_a.m;
      from.get();
      getline(from, v);
      pxs->v_a = v;
      printf("logread: prop update %d(%s) with v = %s\n", pxs->n_a.n, 
	     pxs->n_a.m.c_str(), pxs->v_a.c_str());
    } else {
      printf("logread: unknown log record\n");
      VERIFY(0);
    }
  } 
  from.close();
}

std::string 
log::dump()
{
  std::ifstream from;
  std::string res;
  std::string v;
  from.open(name.c_str());
  while (getline(from, v)) {
    res = res + v + "\n";
  }
  from.close();
  return res;
}

void
log::restore(std::string s)
{
  std::ofstream f;
  printf("restore: %s\n", s.c_str());
  f.open(name.c_str(), std::ios::trunc);
  f << s;
  f.close();
}

// XXX should be an atomic operation
void
log::loginstance(unsigned instance, std::string v)
{
  std::ofstream f;
  f.open(name.c_str(), std::ios::app);
  f << "done";
  f << " ";
  f << instance;
  f << " ";
  f << v;
  f << "\n";
  f.close();
}

// an acceptor should call logprop(n_h) when it
// receives a prepare to which it responds prepare_ok().
void
log::logprop(prop_t n_h)
{
  std::ofstream f;
  f.open(name.c_str(), std::ios::app);
  f << "propseen";
  f << " ";
  f << n_h.n;
  f << " ";
  f << n_h.m;
  f << "\n";
  f.close();
}

// an acceptor should call logaccept(n_a, v_a) when it
// receives an accept RPC to which it replies accept_ok().
void
log::logaccept(prop_t n, std::string v)
{
  std::ofstream f;
  f.open(name.c_str(), std::ios::app);
  f << "accepted";
  f << " ";
  f << n.n;
  f << " ";
  f << n.m;
  f << " ";
  f << v;
  f << "\n";
  f.close();
}

