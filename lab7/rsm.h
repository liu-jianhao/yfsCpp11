// replicated state machine interface.

#ifndef rsm_h
#define rsm_h

#include <string>
#include <vector>
#include "rsm_protocol.h"
#include "rsm_state_transfer.h"
#include "rpc.h"
#include <arpa/inet.h>
#include "config.h"


class rsm : public config_view_change {
 private:
  void reg1(int proc, handler *);
 protected:
  std::map<int, handler *> procs;
  config *cfg;
  class rsm_state_transfer *stf;
  rpcs *rsmrpc;
  // On slave: expected viewstamp of next invoke request
  // On primary: viewstamp for the next request from rsm_client
  viewstamp myvs;
  viewstamp last_myvs;   // Viewstamp of the last executed request
  std::string primary;
  bool insync; 
  bool inviewchange;
  unsigned vid_commit;  // Latest view id that is known to rsm layer
  unsigned vid_insync;  // The view id that this node is synchronizing for
  std::vector<std::string> backups;   // A list of unsynchronized backups

  // For testing purposes
  rpcs *testsvr;
  bool partitioned;
  bool dopartition;
  bool break1;
  bool break2;


  rsm_client_protocol::status client_members(int i, 
					     std::vector<std::string> &r);
  rsm_protocol::status invoke(int proc, viewstamp vs, std::string mreq, 
			      int &dummy);
  rsm_protocol::status transferreq(std::string src, viewstamp last, unsigned vid,
				   rsm_protocol::transferres &r);
  rsm_protocol::status transferdonereq(std::string m, unsigned vid, int &);
  rsm_protocol::status joinreq(std::string src, viewstamp last, 
			       rsm_protocol::joinres &r);
  rsm_test_protocol::status test_net_repairreq(int heal, int &r);
  rsm_test_protocol::status breakpointreq(int b, int &r);

  pthread_mutex_t rsm_mutex;
  pthread_mutex_t invoke_mutex;
  pthread_cond_t recovery_cond;
  pthread_cond_t sync_cond;

  void execute(int procno, std::string req, std::string &r);
  rsm_client_protocol::status client_invoke(int procno, std::string req, 
              std::string &r);
  bool statetransfer(std::string m);
  bool statetransferdone(std::string m);
  bool join(std::string m);
  void set_primary(unsigned vid);
  std::string find_highest(viewstamp &vs, std::string &m, unsigned &vid);
  bool sync_with_backups();
  bool sync_with_primary();
  void net_repair_wo(bool heal);
  void breakpoint1();
  void breakpoint2();
  void partition1();
  void commit_change_wo(unsigned vid);
 public:
  rsm (std::string _first, std::string _me);
  ~rsm() {};

  bool amiprimary();
  void set_state_transfer(rsm_state_transfer *_stf) { stf = _stf; };
  void recovery();
  void commit_change(unsigned vid);

  template<class S, class A1, class R>
    void reg(int proc, S*, int (S::*meth)(const A1 a1, R &));
  template<class S, class A1, class A2, class R>
    void reg(int proc, S*, int (S::*meth)(const A1 a1, const A2 a2, R &));
  template<class S, class A1, class A2, class A3, class R>
    void reg(int proc, S*, int (S::*meth)(const A1 a1, const A2 a2, 
            const A3 a3, R &));
  template<class S, class A1, class A2, class A3, class A4, class R>
    void reg(int proc, S*, int (S::*meth)(const A1 a1, const A2 a2, 
            const A3 a3, const A4 a4, R &));
  template<class S, class A1, class A2, class A3, class A4, class A5, class R>
    void reg(int proc, S*, int (S::*meth)(const A1 a1, const A2 a2, 
            const A3 a3, const A4 a4, 
            const A5 a5, R &));
};

template<class S, class A1, class R> void
  rsm::reg(int proc, S*sob, int (S::*meth)(const A1 a1, R & r))
{
  class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, R & r);
  public:
  h1(S *xsob, int (S::*xmeth)(const A1 a1, R & r))
      : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      R r;
      args >> a1;
      VERIFY(args.okdone());
      int b = (sob->*meth)(a1,r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class R> void
  rsm::reg(int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, R & r))
{
 class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, R & r);
  public:
  h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, R & r))
    : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      R r;
      args >> a1;
      args >> a2;
      VERIFY(args.okdone());
      int b = (sob->*meth)(a1,a2,r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class R> void
  rsm::reg(int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
             const A3 a3, R & r))
{
 class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, R & r);
  public:
  h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, R & r))
    : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      VERIFY(args.okdone());
      int b = (sob->*meth)(a1,a2,a3,r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class R> void
  rsm::reg(int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
             const A3 a3, const A4 a4, R & r))
{
 class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, R & r);
  public:
  h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
            const A4 a4, R & r))
    : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      A4 a4;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      args >> a4;
      VERIFY(args.okdone());
      int b = (sob->*meth)(a1,a2,a3,a4,r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}


template<class S, class A1, class A2, class A3, class A4, class A5, class R> void
  rsm::reg(int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
             const A3 a3, const A4 a4, 
             const A5 a5, R & r))
{
 class h1 : public handler {
  private:
    S * sob;
    int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
       const A5 a5, R & r);
  public:
  h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
            const A4 a4, const A5 a5, R & r))
    : sob(xsob), meth(xmeth) { }
    int fn(unmarshall &args, marshall &ret) {
      A1 a1;
      A2 a2;
      A3 a3;
      A4 a4;
      A5 a5;
      R r;
      args >> a1;
      args >> a2;
      args >> a3;
      args >> a4;
      VERIFY(args.okdone());
      int b = (sob->*meth)(a1,a2,a3,a4,a5,r);
      ret << r;
      return b;
    }
  };
  reg1(proc, new h1(sob, meth));
}

#endif /* rsm_h */
