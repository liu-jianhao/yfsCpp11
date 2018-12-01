#ifndef connection_h
#define connection_h 1

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstddef>

#include <map>

#include "pollmgr.h"

class connection;

class chanmgr {
	public:
		virtual bool got_pdu(connection *c, char *b, int sz) = 0;
		virtual ~chanmgr() {}
};

class connection : public aio_callback {
	public:
		struct charbuf {
			charbuf(): buf(NULL), sz(0), solong(0) {}
			charbuf (char *b, int s) : buf(b), sz(s), solong(0){}
			char *buf;
			int sz;
			int solong; //amount of bytes written or read so far
		};

		connection(chanmgr *m1, int f1, int lossytest=0);
		~connection();

		int channo() { return fd_; }
		bool isdead();
		void closeconn();

		bool send(char *b, int sz);
		void write_cb(int s);
		void read_cb(int s);

		void incref();
		void decref();
		int ref();
                
                int compare(connection *another);
	private:

		bool readpdu();
		bool writepdu();

		chanmgr *mgr_;
		const int fd_;
		bool dead_;

		charbuf wpdu_;
		charbuf rpdu_;
                
                struct timeval create_time_;

		int waiters_;
		int refno_;
		const int lossy_;

		pthread_mutex_t m_;
		pthread_mutex_t ref_m_;
		pthread_cond_t send_complete_;
		pthread_cond_t send_wait_;
};

class tcpsconn {
	public:
		tcpsconn(chanmgr *m1, int port, int lossytest=0);
		~tcpsconn();

		void accept_conn();
	private:

		pthread_mutex_t m_;
		pthread_t th_;
		int pipe_[2];

		int tcp_; //file desciptor for accepting connection
		chanmgr *mgr_;
		int lossy_;
		std::map<int, connection *> conns_;

		void process_accept();
};

struct bundle {
	bundle(chanmgr *m, int s, int l):mgr(m),tcp(s),lossy(l) {}
	chanmgr *mgr;
	int tcp;
	int lossy;
};

void start_accept_thread(chanmgr *mgr, int port, pthread_t *th, int *fd = NULL, int lossy=0);
connection *connect_to_dst(const sockaddr_in &dst, chanmgr *mgr, int lossy=0);
#endif
