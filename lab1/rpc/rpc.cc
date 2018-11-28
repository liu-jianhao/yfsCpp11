/*
 The rpcc class handles client-side RPC.  Each rpcc is bound to a
 single RPC server.  The jobs of rpcc include maintaining a connection to
 server, sending RPC requests and waiting for responses, retransmissions,
 at-most-once delivery etc.

 The rpcs class handles the server side of RPC.  Each rpcs handles multiple
 connections from different rpcc objects.  The jobs of rpcs include accepting
 connections, dispatching requests to registered RPC handlers, at-most-once
 delivery etc.

 Both rpcc and rpcs use the connection class as an abstraction for the
 underlying communication channel.  To send an RPC request/reply, one calls
 connection::send() which blocks until data is sent or the connection has failed
 (thus the caller can free the buffer when send() returns).  When a
 request/reply is received, connection makes a callback into the corresponding
 rpcc or rpcs (see rpcc::got_pdu() and rpcs::got_pdu()).

 Thread organization:
 rpcc uses application threads to send RPC requests and blocks to receive the
 reply or error. All connections use a single PollMgr object to perform async
 socket IO.  PollMgr creates a single thread to examine the readiness of socket
 file descriptors and informs the corresponding connection whenever a socket is
 ready to be read or written.  (We use asynchronous socket IO to reduce the
 number of threads needed to manage these connections; without async IO, at
 least one thread is needed per connection to read data without blocking other
 activities.)  Each rpcs object creates one thread for listening on the server
 port and a pool of threads for executing RPC requests.  The
 thread pool allows us to control the number of threads spawned at the server
 (spawning one thread per request will hurt when the server faces thousands of
 requests).

 In order to delete a connection object, we must maintain a reference count.
 For rpcc,
 multiple client threads might be invoking the rpcc::call() functions and thus
 holding multiple references to the underlying connection object. For rpcs,
 multiple dispatch threads might be holding references to the same connection
 object.  A connection object is deleted only when the underlying connection is
 dead and the reference count reaches zero.

 The previous version of the RPC library uses pthread_cancel* routines 
 to implement the deletion of rpcc and rpcs objects. The idea is to cancel 
 all active threads that might be holding a reference to an object before 
 deleting that object. However, pthread_cancel is not robust and there are
 always bugs where outstanding references to deleted objects persist.
 This version of the RPC library does not do pthread_cancel, but explicitly 
 joins exited threads to make sure no outstanding references exist before 
 deleting objects.

 To delete a rpcc object safely, the users of the library must ensure that
 there are no outstanding calls on the rpcc object.

 To delete a rpcs object safely, we do the following in sequence: 1. stop
 accepting new incoming connections. 2. close existing active connections.
 3.  delete the dispatch thread pool which involves waiting for current active
 RPC handlers to finish.  It is interesting how a thread pool can be deleted
 without using thread cancellation. The trick is to inject x "poison pills" for
 a thread pool of x threads. Upon getting a poison pill instead of a normal 
 task, a worker thread will exit (and thread pool destructor waits to join all
 x exited worker threads).
 */

#include "rpc.h"
#include "method_thread.h"
#include "slock.h"

#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <time.h>
#include <netdb.h>

#include "jsl_log.h"
#include "gettime.h"
#include "lang/verify.h"

const rpcc::TO rpcc::to_max = { 120000 };
const rpcc::TO rpcc::to_min = { 1000 };

rpcc::caller::caller(unsigned int xxid, unmarshall *xun)
: xid(xxid), un(xun), done(false)
{
	VERIFY(pthread_mutex_init(&m,0) == 0);
	VERIFY(pthread_cond_init(&c, 0) == 0);
}

rpcc::caller::~caller()
{
	VERIFY(pthread_mutex_destroy(&m) == 0);
	VERIFY(pthread_cond_destroy(&c) == 0);
}

inline
void set_rand_seed()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	srandom((int)ts.tv_nsec^((int)getpid()));
}

rpcc::rpcc(sockaddr_in d, bool retrans) : 
	dst_(d), srv_nonce_(0), bind_done_(false), xid_(1), lossytest_(0), 
	retrans_(retrans), reachable_(true), chan_(NULL), destroy_wait_ (false), xid_rep_done_(-1)
{
	VERIFY(pthread_mutex_init(&m_, 0) == 0);
	VERIFY(pthread_mutex_init(&chan_m_, 0) == 0);
	VERIFY(pthread_cond_init(&destroy_wait_c_, 0) == 0);

	if(retrans){
		set_rand_seed();
		clt_nonce_ = random();
	} else {
		// special client nonce 0 means this client does not 
		// require at-most-once logic from the server
		// because it uses tcp and never retries a failed connection
		clt_nonce_ = 0;
	}

	char *loss_env = getenv("RPC_LOSSY");
	if(loss_env != NULL){
		lossytest_ = atoi(loss_env);
	}

	// xid starts with 1 and latest received reply starts with 0
	xid_rep_window_.push_back(0);

	jsl_log(JSL_DBG_2, "rpcc::rpcc cltn_nonce is %d lossy %d\n", 
			clt_nonce_, lossytest_); 
}

// IMPORTANT: destruction should happen only when no external threads
// are blocked inside rpcc or will use rpcc in the future
rpcc::~rpcc()
{
	jsl_log(JSL_DBG_2, "rpcc::~rpcc delete nonce %d channo=%d\n", 
			clt_nonce_, chan_?chan_->channo():-1); 
	if(chan_){
		chan_->closeconn();
		chan_->decref();
	}
	VERIFY(calls_.size() == 0);
	VERIFY(pthread_mutex_destroy(&m_) == 0);
	VERIFY(pthread_mutex_destroy(&chan_m_) == 0);
}

int
rpcc::bind(TO to)
{
	int r;
	int ret = call(rpc_const::bind, 0, r, to);
	if(ret == 0){
		ScopedLock ml(&m_);
		bind_done_ = true;
		srv_nonce_ = r;
	} else {
		jsl_log(JSL_DBG_2, "rpcc::bind %s failed %d\n", 
				inet_ntoa(dst_.sin_addr), ret);
	}
	return ret;
};

// Cancel all outstanding calls
void
rpcc::cancel(void)
{
  ScopedLock ml(&m_);
  printf("rpcc::cancel: force callers to fail\n");
  std::map<int,caller*>::iterator iter;
  for(iter = calls_.begin(); iter != calls_.end(); iter++){
    caller *ca = iter->second;

    jsl_log(JSL_DBG_2, "rpcc::cancel: force caller to fail\n");
    {
      ScopedLock cl(&ca->m);
      ca->done = true;
      ca->intret = rpc_const::cancel_failure;
      VERIFY(pthread_cond_signal(&ca->c) == 0);
    }
  }

  while (calls_.size () > 0){
    destroy_wait_ = true;
    VERIFY(pthread_cond_wait(&destroy_wait_c_,&m_) == 0);
  }
  printf("rpcc::cancel: done\n");
}

int
rpcc::call1(unsigned int proc, marshall &req, unmarshall &rep,
		TO to)
{

	caller ca(0, &rep);
        int xid_rep;
	{
		ScopedLock ml(&m_);

		if((proc != rpc_const::bind && !bind_done_) ||
				(proc == rpc_const::bind && bind_done_)){
			jsl_log(JSL_DBG_1, "rpcc::call1 rpcc has not been bound to dst or binding twice\n");
			return rpc_const::bind_failure;
		}

		if(destroy_wait_){
		  return rpc_const::cancel_failure;
		}

		ca.xid = xid_++;
		calls_[ca.xid] = &ca;

		req_header h(ca.xid, proc, clt_nonce_, srv_nonce_,
                             xid_rep_window_.front());
		req.pack_req_header(h);
                xid_rep = xid_rep_window_.front();
	}

	TO curr_to;
	struct timespec now, nextdeadline, finaldeadline; 

	clock_gettime(CLOCK_REALTIME, &now);
	add_timespec(now, to.to, &finaldeadline); 
	curr_to.to = to_min.to;

	bool transmit = true;
	connection *ch = NULL;

	while (1){
		if(transmit){
			get_refconn(&ch);
			if(ch){
			        if(reachable_) {
                                        request forgot;
                                        {
                                                ScopedLock ml(&m_);
                                                if (dup_req_.isvalid() && xid_rep_done_ > dup_req_.xid) {
                                                        forgot = dup_req_;
                                                        dup_req_.clear();
                                                }
                                        }
                                        if (forgot.isvalid()) 
                                                ch->send((char *)forgot.buf.c_str(), forgot.buf.size());
                                        ch->send(req.cstr(), req.size());
                                }
				else jsl_log(JSL_DBG_1, "not reachable\n");
				jsl_log(JSL_DBG_2, 
						"rpcc::call1 %u just sent req proc %x xid %u clt_nonce %d\n", 
						clt_nonce_, proc, ca.xid, clt_nonce_); 
			}
			transmit = false; // only send once on a given channel
		}

		if(!finaldeadline.tv_sec)
			break;

		clock_gettime(CLOCK_REALTIME, &now);
		add_timespec(now, curr_to.to, &nextdeadline); 
		if(cmp_timespec(nextdeadline,finaldeadline) > 0){
			nextdeadline = finaldeadline;
			finaldeadline.tv_sec = 0;
		}

		{
			ScopedLock cal(&ca.m);
			while (!ca.done){
			        jsl_log(JSL_DBG_2, "rpcc:call1: wait\n");
				if(pthread_cond_timedwait(&ca.c, &ca.m,
                                                 &nextdeadline) == ETIMEDOUT){
				  	jsl_log(JSL_DBG_2, "rpcc::call1: timeout\n");
					break;
				}
			}
			if(ca.done){
			        jsl_log(JSL_DBG_2, "rpcc::call1: reply received\n");
				break;
			}
		}

		if(retrans_ && (!ch || ch->isdead())){
			// since connection is dead, retransmit
                        // on the new connection 
			transmit = true; 
		}
		curr_to.to <<= 1;
	}

	{ 
                // no locking of ca.m since only this thread changes ca.xid 
		ScopedLock ml(&m_);
		calls_.erase(ca.xid);
		// may need to update the xid again here, in case the
		// packet times out before it's even sent by the channel.
		// I don't think there's any harm in maybe doing it twice
		update_xid_rep(ca.xid);

		if(destroy_wait_){
		  VERIFY(pthread_cond_signal(&destroy_wait_c_) == 0);
		}
	}

        if (ca.done && lossytest_)
        {
                ScopedLock ml(&m_);
                if (!dup_req_.isvalid()) {
                        dup_req_.buf.assign(req.cstr(), req.size());
                        dup_req_.xid = ca.xid;
                }
                if (xid_rep > xid_rep_done_)
                        xid_rep_done_ = xid_rep;
        }

	ScopedLock cal(&ca.m);

	jsl_log(JSL_DBG_2, 
			"rpcc::call1 %u call done for req proc %x xid %u %s:%d done? %d ret %d \n", 
			clt_nonce_, proc, ca.xid, inet_ntoa(dst_.sin_addr),
			ntohs(dst_.sin_port), ca.done, ca.intret);

	if(ch)
		ch->decref();

	// destruction of req automatically frees its buffer
	return (ca.done? ca.intret : rpc_const::timeout_failure);
}

void
rpcc::get_refconn(connection **ch)
{
	ScopedLock ml(&chan_m_);
	if(!chan_ || chan_->isdead()){
		if(chan_)
			chan_->decref();
		chan_ = connect_to_dst(dst_, this, lossytest_);
	}
	if(ch && chan_){
		if(*ch){
			(*ch)->decref();
		}
		*ch = chan_;
		(*ch)->incref();
	}
}

// PollMgr's thread is being used to 
// make this upcall from connection object to rpcc. 
// this funtion must not block.
//
// this function keeps no reference for connection *c 
bool
rpcc::got_pdu(connection *c, char *b, int sz)
{
	unmarshall rep(b, sz);
	reply_header h;
	rep.unpack_reply_header(&h);

	if(!rep.ok()){
		jsl_log(JSL_DBG_1, "rpcc:got_pdu unmarshall header failed!!!\n");
		return true;
	}

	ScopedLock ml(&m_);

	update_xid_rep(h.xid);

	if(calls_.find(h.xid) == calls_.end()){
		jsl_log(JSL_DBG_2, "rpcc::got_pdu xid %d no pending request\n", h.xid);
		return true;
	}
	caller *ca = calls_[h.xid];

	ScopedLock cl(&ca->m);
	if(!ca->done){
		ca->un->take_in(rep);
		ca->intret = h.ret;
		if(ca->intret < 0){
			jsl_log(JSL_DBG_2, "rpcc::got_pdu: RPC reply error for xid %d intret %d\n",
					h.xid, ca->intret);
		}
		ca->done = 1;
	}
	VERIFY(pthread_cond_broadcast(&ca->c) == 0);
	return true;
}

// assumes thread holds mutex m
void 
rpcc::update_xid_rep(unsigned int xid)
{
	std::list<unsigned int>::iterator it;

	if(xid <= xid_rep_window_.front()){
		return;
	}

	for (it = xid_rep_window_.begin(); it != xid_rep_window_.end(); it++){
		if(*it > xid){
			xid_rep_window_.insert(it, xid);
			goto compress;
		}
	}
	xid_rep_window_.push_back(xid);

compress:
	it = xid_rep_window_.begin();
	for (it++; it != xid_rep_window_.end(); it++){
		while (xid_rep_window_.front() + 1 == *it)
			xid_rep_window_.pop_front();
	}
}


rpcs::rpcs(unsigned int p1, int count)
  : port_(p1), counting_(count), curr_counts_(count), lossytest_(0), reachable_ (true)
{
	VERIFY(pthread_mutex_init(&procs_m_, 0) == 0);
	VERIFY(pthread_mutex_init(&count_m_, 0) == 0);
	VERIFY(pthread_mutex_init(&reply_window_m_, 0) == 0);
	VERIFY(pthread_mutex_init(&conss_m_, 0) == 0);

	set_rand_seed();
	nonce_ = random();
	jsl_log(JSL_DBG_2, "rpcs::rpcs created with nonce %d\n", nonce_);

	char *loss_env = getenv("RPC_LOSSY");
	if(loss_env != NULL){
		lossytest_ = atoi(loss_env);
	}

	reg(rpc_const::bind, this, &rpcs::rpcbind);
	dispatchpool_ = new ThrPool(6,false);

	listener_ = new tcpsconn(this, port_, lossytest_);
}

rpcs::~rpcs()
{
	// must delete listener before dispatchpool
	delete listener_;
	delete dispatchpool_;
	free_reply_window();
}

bool
rpcs::got_pdu(connection *c, char *b, int sz)
{
        if(!reachable_){
            jsl_log(JSL_DBG_1, "rpcss::got_pdu: not reachable\n");
            return true;
        }

	djob_t *j = new djob_t(c, b, sz);
	c->incref();
	bool succ = dispatchpool_->addObjJob(this, &rpcs::dispatch, j);
	if(!succ || !reachable_){
		c->decref();
		delete j;
	}
	return succ; 
}

void
rpcs::reg1(unsigned int proc, handler *h)
{
	ScopedLock pl(&procs_m_);
	VERIFY(procs_.count(proc) == 0);
	procs_[proc] = h;
	VERIFY(procs_.count(proc) >= 1);
}

void
rpcs::updatestat(unsigned int proc)
{
	ScopedLock cl(&count_m_);
	counts_[proc]++;
	curr_counts_--;
	if(curr_counts_ == 0){
		std::map<int, int>::iterator i;
		printf("RPC STATS: ");
		for (i = counts_.begin(); i != counts_.end(); i++){
			printf("%x:%d ", i->first, i->second);
		}
		printf("\n");

		ScopedLock rwl(&reply_window_m_);
		std::map<unsigned int,std::list<reply_t> >::iterator clt;

		unsigned int totalrep = 0, maxrep = 0;
		for (clt = reply_window_.begin(); clt != reply_window_.end(); clt++){
			totalrep += clt->second.size();
			if(clt->second.size() > maxrep)
				maxrep = clt->second.size();
		}
		jsl_log(JSL_DBG_1, "REPLY WINDOW: clients %d total reply %d max per client %d\n", 
                        (int) reply_window_.size(), totalrep, maxrep);
		curr_counts_ = counting_;
	}
}

void
rpcs::dispatch(djob_t *j)
{
	connection *c = j->conn;
	unmarshall req(j->buf, j->sz);
	delete j;

	req_header h;
	req.unpack_req_header(&h);
	int proc = h.proc;

	if(!req.ok()){
		jsl_log(JSL_DBG_1, "rpcs:dispatch unmarshall header failed!!!\n");
		c->decref();
		return;
	}

	jsl_log(JSL_DBG_2,
			"rpcs::dispatch: rpc %u (proc %x, last_rep %u) from clt %u for srv instance %u \n",
			h.xid, proc, h.xid_rep, h.clt_nonce, h.srv_nonce);

	marshall rep;
	reply_header rh(h.xid,0);

	// is client sending to an old instance of server?
	if(h.srv_nonce != 0 && h.srv_nonce != nonce_){
		jsl_log(JSL_DBG_2,
				"rpcs::dispatch: rpc for an old server instance %u (current %u) proc %x\n",
				h.srv_nonce, nonce_, h.proc);
		rh.ret = rpc_const::oldsrv_failure;
		rep.pack_reply_header(rh);
		c->send(rep.cstr(),rep.size());
		return;
	}

	handler *f;
	// is RPC proc a registered procedure?
	{
		ScopedLock pl(&procs_m_);
		if(procs_.count(proc) < 1){
			fprintf(stderr, "rpcs::dispatch: unknown proc %x.\n",
				proc);
			c->decref();
                        VERIFY(0);
			return;
		}

		f = procs_[proc];
	}

	rpcs::rpcstate_t stat;
	char *b1;
	int sz1;

	if(h.clt_nonce){
		// have i seen this client before?
		{
			ScopedLock rwl(&reply_window_m_);
			// if we don't know about this clt_nonce, create a cleanup object
			if(reply_window_.find(h.clt_nonce) == reply_window_.end()){
				VERIFY (reply_window_[h.clt_nonce].size() == 0); // create
				jsl_log(JSL_DBG_2,
						"rpcs::dispatch: new client %u xid %d chan %d, total clients %d\n", 
						h.clt_nonce, h.xid, c->channo(), (int)reply_window_.size());
			}
		}

		// save the latest good connection to the client
		{
			ScopedLock rwl(&conss_m_);
			if(conns_.find(h.clt_nonce) == conns_.end()){
				c->incref();
				conns_[h.clt_nonce] = c;
			} else if(conns_[h.clt_nonce]->compare(c) < 0){
				conns_[h.clt_nonce]->decref();
				c->incref();
				conns_[h.clt_nonce] = c;
			}
		}

		stat = checkduplicate_and_update(h.clt_nonce, h.xid,
                                                 h.xid_rep, &b1, &sz1);
	} else {
		// this client does not require at most once logic
		stat = NEW;
	}

	switch (stat){
		case NEW: // new request
			if(counting_){
				updatestat(proc);
			}

			rh.ret = f->fn(req, rep);
                        if (rh.ret == rpc_const::unmarshal_args_failure) {
                                fprintf(stderr, "rpcs::dispatch: failed to"
                                       " unmarshall the arguments. You are"
                                       " probably calling RPC 0x%x with wrong"
                                       " types of arguments.\n", proc);
                                VERIFY(0);
                        }
			VERIFY(rh.ret >= 0);

			rep.pack_reply_header(rh);
			rep.take_buf(&b1,&sz1);

			jsl_log(JSL_DBG_2,
					"rpcs::dispatch: sending and saving reply of size %d for rpc %u, proc %x ret %d, clt %u\n",
					sz1, h.xid, proc, rh.ret, h.clt_nonce);

			if(h.clt_nonce > 0){
				// only record replies for clients that require at-most-once logic
				add_reply(h.clt_nonce, h.xid, b1, sz1);
			}

			// get the latest connection to the client
			{
				ScopedLock rwl(&conss_m_);
				if(c->isdead() && c != conns_[h.clt_nonce]){
					c->decref();
					c = conns_[h.clt_nonce];
					c->incref();
				}
			}

			c->send(b1, sz1);
			if(h.clt_nonce == 0){
				// reply is not added to at-most-once window, free it
				free(b1);
			}
			break;
		case INPROGRESS: // server is working on this request
			break;
		case DONE: // duplicate and we still have the response
			c->send(b1, sz1);
			break;
		case FORGOTTEN: // very old request and we don't have the response anymore
			jsl_log(JSL_DBG_2, "rpcs::dispatch: very old request %u from %u\n", 
					h.xid, h.clt_nonce);
			rh.ret = rpc_const::atmostonce_failure;
			rep.pack_reply_header(rh);
			c->send(rep.cstr(),rep.size());
			break;
	}
	c->decref();
}

// rpcs::dispatch calls this when an RPC request arrives.
// checks to see if an RPC with xid from clt_nonce has already been received.
// if not, remembers the request.
// returns one of:
//   NEW: never seen this xid before.
//   INPROGRESS: seen this xid, and still processing it.
//   DONE: seen this xid, previous reply returned in *b and *sz.
//   FORGOTTEN: might have seen this xid, but deleted previous reply.
rpcs::rpcstate_t 
rpcs::checkduplicate_and_update(unsigned int clt_nonce, unsigned int xid,
		unsigned int xid_rep, char **b, int *sz)
{
	ScopedLock rwl(&reply_window_m_);

        // You fill this in for Lab 1.
	return NEW;
}

// rpcs::dispatch calls add_reply when it is sending a reply to an RPC,
// and passes the return value in b and sz.
// add_reply() should remember b and sz.
// free_reply_window() and checkduplicate_and_update is responsible for 
// calling free(b).
void
rpcs::add_reply(unsigned int clt_nonce, unsigned int xid,
		char *b, int sz)
{
	ScopedLock rwl(&reply_window_m_);
        // You fill this in for Lab 1.
}

void
rpcs::free_reply_window(void)
{
	std::map<unsigned int,std::list<reply_t> >::iterator clt;
	std::list<reply_t>::iterator it;

	ScopedLock rwl(&reply_window_m_);
	for (clt = reply_window_.begin(); clt != reply_window_.end(); clt++){
		for (it = clt->second.begin(); it != clt->second.end(); it++){
			free((*it).buf);
		}
		clt->second.clear();
	}
	reply_window_.clear();
}

// rpc handler
int 
rpcs::rpcbind(int a, int &r)
{
	jsl_log(JSL_DBG_2, "rpcs::rpcbind called return nonce %u\n", nonce_);
	r = nonce_;
	return 0;
}

void
marshall::rawbyte(unsigned char x)
{
	if(_ind >= _capa){
		_capa *= 2;
		VERIFY (_buf != NULL);
		_buf = (char *)realloc(_buf, _capa);
		VERIFY(_buf);
	}
	_buf[_ind++] = x;
}

void
marshall::rawbytes(const char *p, int n)
{
	if((_ind+n) > _capa){
		_capa = _capa > n? 2*_capa:(_capa+n);
		VERIFY (_buf != NULL);
		_buf = (char *)realloc(_buf, _capa);
		VERIFY(_buf);
	}
	memcpy(_buf+_ind, p, n);
	_ind += n;
}

marshall &
operator<<(marshall &m, bool x)
{
	m.rawbyte(x);
	return m;
}

marshall &
operator<<(marshall &m, unsigned char x)
{
	m.rawbyte(x);
	return m;
}

marshall &
operator<<(marshall &m, char x)
{
	m << (unsigned char) x;
	return m;
}


marshall &
operator<<(marshall &m, unsigned short x)
{
	m.rawbyte((x >> 8) & 0xff);
	m.rawbyte(x & 0xff);
	return m;
}

marshall &
operator<<(marshall &m, short x)
{
	m << (unsigned short) x;
	return m;
}

marshall &
operator<<(marshall &m, unsigned int x)
{
	// network order is big-endian
	m.rawbyte((x >> 24) & 0xff);
	m.rawbyte((x >> 16) & 0xff);
	m.rawbyte((x >> 8) & 0xff);
	m.rawbyte(x & 0xff);
	return m;
}

marshall &
operator<<(marshall &m, int x)
{
	m << (unsigned int) x;
	return m;
}

marshall &
operator<<(marshall &m, const std::string &s)
{
	m << (unsigned int) s.size();
	m.rawbytes(s.data(), s.size());
	return m;
}

marshall &
operator<<(marshall &m, unsigned long long x)
{
	m << (unsigned int) (x >> 32);
	m << (unsigned int) x;
	return m;
}

void
marshall::pack(int x)
{
	rawbyte((x >> 24) & 0xff);
	rawbyte((x >> 16) & 0xff);
	rawbyte((x >> 8) & 0xff);
	rawbyte(x & 0xff);
}

void
unmarshall::unpack(int *x)
{
	(*x) = (rawbyte() & 0xff) << 24;
	(*x) |= (rawbyte() & 0xff) << 16;
	(*x) |= (rawbyte() & 0xff) << 8;
	(*x) |= rawbyte() & 0xff;
}

// take the contents from another unmarshall object
void
unmarshall::take_in(unmarshall &another)
{
	if(_buf)
		free(_buf);
	another.take_buf(&_buf, &_sz);
	_ind = RPC_HEADER_SZ;
	_ok = _sz >= RPC_HEADER_SZ?true:false;
}

bool
unmarshall::okdone()
{
	if(ok() && _ind == _sz){
		return true;
	} else {
		return false;
	}
}

unsigned int
unmarshall::rawbyte()
{
	char c = 0;
	if(_ind >= _sz)
		_ok = false;
	else
		c = _buf[_ind++];
	return c;
}

unmarshall &
operator>>(unmarshall &u, bool &x)
{
	x = (bool) u.rawbyte() ;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned char &x)
{
	x = (unsigned char) u.rawbyte() ;
	return u;
}

unmarshall &
operator>>(unmarshall &u, char &x)
{
	x = (char) u.rawbyte();
	return u;
}


unmarshall &
operator>>(unmarshall &u, unsigned short &x)
{
	x = (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, short &x)
{
	x = (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned int &x)
{
	x = (u.rawbyte() & 0xff) << 24;
	x |= (u.rawbyte() & 0xff) << 16;
	x |= (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, int &x)
{
	x = (u.rawbyte() & 0xff) << 24;
	x |= (u.rawbyte() & 0xff) << 16;
	x |= (u.rawbyte() & 0xff) << 8;
	x |= u.rawbyte() & 0xff;
	return u;
}

unmarshall &
operator>>(unmarshall &u, unsigned long long &x)
{
	unsigned int h, l;
	u >> h;
	u >> l;
	x = l | ((unsigned long long) h << 32);
	return u;
}

unmarshall &
operator>>(unmarshall &u, std::string &s)
{
	unsigned sz;
	u >> sz;
	if(u.ok())
		u.rawbytes(s, sz);
	return u;
}

void
unmarshall::rawbytes(std::string &ss, unsigned int n)
{
	if((_ind+n) > (unsigned)_sz){
		_ok = false;
	} else {
		std::string tmps = std::string(_buf+_ind, n);
		swap(ss, tmps);
		VERIFY(ss.size() == n);
		_ind += n;
	}
}

bool operator<(const sockaddr_in &a, const sockaddr_in &b){
	return ((a.sin_addr.s_addr < b.sin_addr.s_addr) ||
			((a.sin_addr.s_addr == b.sin_addr.s_addr) &&
			 ((a.sin_port < b.sin_port))));
}

/*---------------auxilary function--------------*/
void
make_sockaddr(const char *hostandport, struct sockaddr_in *dst){

	char host[200];
	const char *localhost = "127.0.0.1";
	const char *port = index(hostandport, ':');
	if(port == NULL){
		memcpy(host, localhost, strlen(localhost)+1);
		port = hostandport;
	} else {
		memcpy(host, hostandport, port-hostandport);
		host[port-hostandport] = '\0';
		port++;
	}

	make_sockaddr(host, port, dst);

}

void
make_sockaddr(const char *host, const char *port, struct sockaddr_in *dst){

	in_addr_t a;

	bzero(dst, sizeof(*dst));
	dst->sin_family = AF_INET;

	a = inet_addr(host);
	if(a != INADDR_NONE){
		dst->sin_addr.s_addr = a;
	} else {
		struct hostent *hp = gethostbyname(host);
		if(hp == 0 || hp->h_length != 4){
			fprintf(stderr, "cannot find host name %s\n", host);
			exit(1);
		}
		dst->sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	}
	dst->sin_port = htons(atoi(port));
}

int
cmp_timespec(const struct timespec &a, const struct timespec &b)
{
	if(a.tv_sec > b.tv_sec)
		return 1;
	else if(a.tv_sec < b.tv_sec)
		return -1;
	else {
		if(a.tv_nsec > b.tv_nsec)
			return 1;
		else if(a.tv_nsec < b.tv_nsec)
			return -1;
		else
			return 0;
	}
}

void
add_timespec(const struct timespec &a, int b, struct timespec *result)
{
	// convert to millisec, add timeout, convert back
	result->tv_sec = a.tv_sec + b/1000;
	result->tv_nsec = a.tv_nsec + (b % 1000) * 1000000;
	VERIFY(result->tv_nsec >= 0);
	while (result->tv_nsec > 1000000000){
		result->tv_sec++;
		result->tv_nsec-=1000000000;
	}
}

int
diff_timespec(const struct timespec &end, const struct timespec &start)
{
	int diff = (end.tv_sec > start.tv_sec)?(end.tv_sec-start.tv_sec)*1000:0;
	VERIFY(diff || end.tv_sec == start.tv_sec);
	if(end.tv_nsec > start.tv_nsec){
		diff += (end.tv_nsec-start.tv_nsec)/1000000;
	} else {
		diff -= (start.tv_nsec-end.tv_nsec)/1000000;
	}
	return diff;
}
