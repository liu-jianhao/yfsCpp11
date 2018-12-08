#ifndef rsm_client_h
#define rsm_client_h

#include "rpc.h"
#include "rsm_protocol.h"
#include <string>
#include <vector>


//
// rsm client interface.
//
// The client stubs package up an rpc, and then call the invoke procedure 
// on the replicated state machine passing the RPC as an argument.  This way 
// the replicated state machine isn't service specific; any server can use it.
//

class rsm_client {

 protected:
  std::string primary;
  std::vector<std::string> known_mems;
  pthread_mutex_t rsm_client_mutex;
  void primary_failure();
  bool init_members();
 public:
  rsm_client(std::string dst);
  rsm_protocol::status invoke(int proc, std::string req, std::string &rep);

  template<class R, class A1>
    int call(unsigned int proc, const A1 & a1, R &r);

  template<class R, class A1, class A2>
    int call(unsigned int proc, const A1 & a1, const A2 & a2, R &r);

  template<class R, class A1, class A2, class A3>
    int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
	     R &r);

  template<class R, class A1, class A2, class A3, class A4>
    int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
	     const A4 & a4, R &r);

  template<class R, class A1, class A2, class A3, class A4, class A5>
    int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
	     const A4 & a4, const A5 & a5, R &r);
 private:
  template<class R> int call_m(unsigned int proc, marshall &req, R &r);
};

template<class R> int
rsm_client::call_m(unsigned int proc, marshall &req, R &r)
{
	std::string rep;
        std::string res;
	int intret = invoke(proc, req.str(), rep);
        VERIFY( intret == rsm_client_protocol::OK );
        unmarshall u(rep);
	u >> intret;
	if (intret < 0) return intret;
        u >> res;
        if (!u.okdone()) {
                fprintf(stderr, "rsm_client::call_m: failed to unmarshall the reply.\n"
                       "You probably forgot to set the reply string in "
                       "rsm::client_invoke, or you may call RPC 0x%x with wrong return "
                       "type\n", proc);
                VERIFY(0);
		return rpc_const::unmarshal_reply_failure;
        }
        unmarshall u1(res);
        u1 >> r;
	if(!u1.okdone()) {
                fprintf(stderr, "rsm_client::call_m: failed to unmarshall the reply.\n"
                       "You are probably calling RPC 0x%x with wrong return "
                       "type.\n", proc);
                VERIFY(0);
		return rpc_const::unmarshal_reply_failure;
        }
	return intret;
}

template<class R, class A1> int
  rsm_client::call(unsigned int proc, const A1 & a1, R & r)
{
  marshall m;
  m << a1;
  return call_m(proc, m, r);
}

template<class R, class A1, class A2> int
  rsm_client::call(unsigned int proc, const A1 & a1, const A2 & a2, R & r)
{
  marshall m;
  m << a1;
  m << a2;
  return call_m(proc, m, r);
}

template<class R, class A1, class A2, class A3> int
  rsm_client::call(unsigned int proc, const A1 & a1, 
		const A2 & a2, const A3 & a3, R & r)
{
  marshall m;
  std::string rep;
  std::string res;
  m << a1;
  m << a2;
  m << a3;
  return call_m(proc, m, r);
}

template<class R, class A1, class A2, class A3, class A4> int
  rsm_client::call(unsigned int proc, const A1 & a1, 
		   const A2 & a2, const A3 & a3, const A4 & a4, R & r)
{
  marshall m;
  std::string rep;
  std::string res;
  m << a1;
  m << a2;
  m << a3;
  m << a4;
  return call_m(proc, m, r);
}

template<class R, class A1, class A2, class A3, class A4, class A5> int
  rsm_client::call(unsigned int proc, const A1 & a1, 
		   const A2 & a2, const A3 & a3, const A4 & a4, const A5 & a5,
		   R & r)
{
  marshall m;
  std::string rep;
  std::string res;
  m << a1;
  m << a2;
  m << a3;
  m << a4;
  m << a5;
  return call_m(proc, m, r);
}

#endif 
