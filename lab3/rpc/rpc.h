#ifndef rpc_h
#define rpc_h

#include <sys/socket.h>
#include <netinet/in.h>
#include <list>
#include <map>
#include <stdio.h>

#include "thr_pool.h"
#include "marshall.h"
#include "connection.h"

#ifdef DMALLOC
#include "dmalloc.h"
#endif

class rpc_const {
	public:
		static const unsigned int bind = 1;   // handler number reserved for bind
		static const int timeout_failure = -1;
		static const int unmarshal_args_failure = -2;
		static const int unmarshal_reply_failure = -3;
		static const int atmostonce_failure = -4;
		static const int oldsrv_failure = -5;
		static const int bind_failure = -6;
		static const int cancel_failure = -7;
};

// rpc client endpoint.
// manages a xid space per destination socket
// threaded: multiple threads can be sending RPCs,
class rpcc : public chanmgr {

	private:

		//manages per rpc info
		struct caller {
			caller(unsigned int xxid, unmarshall *un);
			~caller();

			unsigned int xid;
			unmarshall *un;
			int intret;
			bool done;
			pthread_mutex_t m;
			pthread_cond_t c;
		};

		void get_refconn(connection **ch);
		void update_xid_rep(unsigned int xid);


		sockaddr_in dst_;
		unsigned int clt_nonce_;
		unsigned int srv_nonce_;
		bool bind_done_;
		unsigned int xid_;
		int lossytest_;
		bool retrans_;
		bool reachable_;

		connection *chan_;

		pthread_mutex_t m_; // protect insert/delete to calls[]
		pthread_mutex_t chan_m_;

		bool destroy_wait_;
		pthread_cond_t destroy_wait_c_;

		std::map<int, caller *> calls_;
		std::list<unsigned int> xid_rep_window_;
                
                struct request {
                    request() { clear(); }
                    void clear() { buf.clear(); xid = -1; }
                    bool isvalid() { return xid != -1; }
                    std::string buf;
                    int xid;
                };
                struct request dup_req_;
                int xid_rep_done_;
	public:

		rpcc(sockaddr_in d, bool retrans=true);
		~rpcc();

		struct TO {
			int to;
		};
		static const TO to_max;
		static const TO to_min;
		static TO to(int x) { TO t; t.to = x; return t;}

		unsigned int id() { return clt_nonce_; }

		int bind(TO to = to_max);

		void set_reachable(bool r) { reachable_ = r; }

		void cancel();
                
                int islossy() { return lossytest_ > 0; }

		int call1(unsigned int proc, 
				marshall &req, unmarshall &rep, TO to);

		bool got_pdu(connection *c, char *b, int sz);


		template<class R>
			int call_m(unsigned int proc, marshall &req, R & r, TO to);

		template<class R>
			int call(unsigned int proc, R & r, TO to = to_max); 
		template<class R, class A1>
			int call(unsigned int proc, const A1 & a1, R & r, TO to = to_max); 
		template<class R, class A1, class A2>
			int call(unsigned int proc, const A1 & a1, const A2 & a2, R & r, 
					TO to = to_max); 
		template<class R, class A1, class A2, class A3>
			int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
					R & r, TO to = to_max); 
		template<class R, class A1, class A2, class A3, class A4>
			int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
					const A4 & a4, R & r, TO to = to_max);
		template<class R, class A1, class A2, class A3, class A4, class A5>
			int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
					const A4 & a4, const A5 & a5, R & r, TO to = to_max); 
		template<class R, class A1, class A2, class A3, class A4, class A5,
			class A6>
				int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
						const A4 & a4, const A5 & a5, const A6 & a6,
						R & r, TO to = to_max); 
		template<class R, class A1, class A2, class A3, class A4, class A5, 
			class A6, class A7>
				int call(unsigned int proc, const A1 & a1, const A2 & a2, const A3 & a3, 
						const A4 & a4, const A5 & a5, const A6 &a6, const A7 &a7,
						R & r, TO to = to_max); 

};

template<class R> int 
rpcc::call_m(unsigned int proc, marshall &req, R & r, TO to) 
{
	unmarshall u;
	int intret = call1(proc, req, u, to);
	if (intret < 0) return intret;
	u >> r;
	if(u.okdone() != true) {
                fprintf(stderr, "rpcc::call_m: failed to unmarshall the reply."
                       "You are probably calling RPC 0x%x with wrong return "
                       "type.\n", proc);
                VERIFY(0);
		return rpc_const::unmarshal_reply_failure;
        }
	return intret;
}

template<class R> int
rpcc::call(unsigned int proc, R & r, TO to) 
{
	marshall m;
	return call_m(proc, m, r, to);
}

template<class R, class A1> int
rpcc::call(unsigned int proc, const A1 & a1, R & r, TO to) 
{
	marshall m;
	m << a1;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5,
	class A6> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, 
		const A6 & a6, R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	m << a6;
	return call_m(proc, m, r, to);
}

template<class R, class A1, class A2, class A3, class A4, class A5,
	class A6, class A7> int
rpcc::call(unsigned int proc, const A1 & a1, const A2 & a2,
		const A3 & a3, const A4 & a4, const A5 & a5, 
		const A6 & a6, const A7 & a7,
		R & r, TO to) 
{
	marshall m;
	m << a1;
	m << a2;
	m << a3;
	m << a4;
	m << a5;
	m << a6;
	m << a7;
	return call_m(proc, m, r, to);
}

bool operator<(const sockaddr_in &a, const sockaddr_in &b);

class handler {
	public:
		handler() { }
		virtual ~handler() { }
		virtual int fn(unmarshall &, marshall &) = 0;
};


// rpc server endpoint.
class rpcs : public chanmgr {

	typedef enum {
		NEW,  // new RPC, not a duplicate
		INPROGRESS, // duplicate of an RPC we're still processing
		DONE, // duplicate of an RPC we already replied to (have reply)
		FORGOTTEN,  // duplicate of an old RPC whose reply we've forgotten
	} rpcstate_t;

	private:

        // state about an in-progress or completed RPC, for at-most-once.
        // if cb_present is true, then the RPC is complete and a reply
        // has been sent; in that case buf points to a copy of the reply,
        // and sz holds the size of the reply.
	struct reply_t {
		reply_t (unsigned int _xid) {
			xid = _xid;
			cb_present = false;
			buf = NULL;
			sz = 0;
		}
		unsigned int xid;
		bool cb_present; // whether the reply buffer is valid
		char *buf;      // the reply buffer
		int sz;         // the size of reply buffer
	};

	int port_;
	unsigned int nonce_;

	// provide at most once semantics by maintaining a window of replies
	// per client that that client hasn't acknowledged receiving yet.
        // indexed by client nonce.
	std::map<unsigned int, std::list<reply_t> > reply_window_;

	void free_reply_window(void);
	void add_reply(unsigned int clt_nonce, unsigned int xid, char *b, int sz);

	rpcstate_t checkduplicate_and_update(unsigned int clt_nonce, 
			unsigned int xid, unsigned int rep_xid,
			char **b, int *sz);

	void updatestat(unsigned int proc);

	// latest connection to the client
	std::map<unsigned int, connection *> conns_;

	// counting
	const int counting_;
	int curr_counts_;
	std::map<int, int> counts_;

	int lossytest_; 
	bool reachable_;

	// map proc # to function
	std::map<int, handler *> procs_;

	pthread_mutex_t procs_m_; // protect insert/delete to procs[]
	pthread_mutex_t count_m_;  //protect modification of counts
	pthread_mutex_t reply_window_m_; // protect reply window et al
	pthread_mutex_t conss_m_; // protect conns_


	protected:

	struct djob_t {
		djob_t (connection *c, char *b, int bsz):buf(b),sz(bsz),conn(c) {}
		char *buf;
		int sz;
		connection *conn;
	};
	void dispatch(djob_t *);

	// internal handler registration
	void reg1(unsigned int proc, handler *);

	ThrPool* dispatchpool_;
	tcpsconn* listener_;

	public:
	rpcs(unsigned int port, int counts=0);
	~rpcs();

	//RPC handler for clients binding
	int rpcbind(int a, int &r);

	void set_reachable(bool r) { reachable_ = r; }

	bool got_pdu(connection *c, char *b, int sz);

	// register a handler
	template<class S, class A1, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, R & r));
	template<class S, class A1, class A2, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1 a1, const A2, 
					R & r));
	template<class S, class A1, class A2, class A3, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, R & r));
	template<class S, class A1, class A2, class A3, class A4, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, const A4, R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class R>
		void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
					const A3, const A4, const A5, 
					R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class A6,
		class R>
			void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
						const A3, const A4, const A5, 
						const A6, R & r));
	template<class S, class A1, class A2, class A3, class A4, class A5, class A6,
		class A7, class R>
			void reg(unsigned int proc, S*, int (S::*meth)(const A1, const A2, 
						const A3, const A4, const A5, 
						const A6, const A7,
						R & r));
};

template<class S, class A1, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, R & r))
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
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			R & r))
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
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, R & r))
				: sob(xsob), meth(xmeth)  { }
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
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
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
				args >> a5;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, class A6, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			const A5 a5, const A6 a6, 
			R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
					const A5 a5, const A6 a6, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, const A5 a5, const A6 a6, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				A5 a5;
				A6 a6;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				args >> a5;
				args >> a6;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, a6, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}

template<class S, class A1, class A2, class A3, class A4, class A5, 
	class A6, class A7, class R> void
rpcs::reg(unsigned int proc, S*sob, int (S::*meth)(const A1 a1, const A2 a2, 
			const A3 a3, const A4 a4, 
			const A5 a5, const A6 a6,
			const A7 a7, R & r))
{
	class h1 : public handler {
		private:
			S * sob;
			int (S::*meth)(const A1 a1, const A2 a2, const A3 a3, const A4 a4, 
					const A5 a5, const A6 a6, const A7 a7, R & r);
		public:
			h1(S *xsob, int (S::*xmeth)(const A1 a1, const A2 a2, const A3 a3, 
						const A4 a4, const A5 a5, const A6 a6,
						const A7 a7, R & r))
				: sob(xsob), meth(xmeth) { }
			int fn(unmarshall &args, marshall &ret) {
				A1 a1;
				A2 a2;
				A3 a3;
				A4 a4;
				A5 a5;
				A6 a6;
				A7 a7;
				R r;
				args >> a1;
				args >> a2;
				args >> a3;
				args >> a4;
				args >> a5;
				args >> a6;
				args >> a7;
				if(!args.okdone())
					return rpc_const::unmarshal_args_failure;
				int b = (sob->*meth)(a1, a2, a3, a4, a5, a6, a7, r);
				ret << r;
				return b;
			}
	};
	reg1(proc, new h1(sob, meth));
}


void make_sockaddr(const char *hostandport, struct sockaddr_in *dst);
void make_sockaddr(const char *host, const char *port,
		struct sockaddr_in *dst);

int cmp_timespec(const struct timespec &a, const struct timespec &b);
void add_timespec(const struct timespec &a, int b, struct timespec *result);
int diff_timespec(const struct timespec &a, const struct timespec &b);

#endif
