#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "method_thread.h"
#include "connection.h"
#include "slock.h"
#include "pollmgr.h"
#include "jsl_log.h"
#include "gettime.h"
#include "lang/verify.h"

#define MAX_PDU (10<<20) //maximum PDF is 10M


connection::connection(chanmgr *m1, int f1, int l1) 
: mgr_(m1), fd_(f1), dead_(false),waiters_(0), refno_(1),lossy_(l1)
{

	int flags = fcntl(fd_, F_GETFL, NULL);
	flags |= O_NONBLOCK;
	fcntl(fd_, F_SETFL, flags);

	signal(SIGPIPE, SIG_IGN);
	VERIFY(pthread_mutex_init(&m_,0)==0);
	VERIFY(pthread_mutex_init(&ref_m_,0)==0);
	VERIFY(pthread_cond_init(&send_wait_,0)==0);
	VERIFY(pthread_cond_init(&send_complete_,0)==0);
 
        VERIFY(gettimeofday(&create_time_, NULL) == 0); 

	PollMgr::Instance()->add_callback(fd_, CB_RDONLY, this);
}

connection::~connection()
{
	VERIFY(dead_);
	VERIFY(pthread_mutex_destroy(&m_)== 0);
	VERIFY(pthread_mutex_destroy(&ref_m_)== 0);
	VERIFY(pthread_cond_destroy(&send_wait_) == 0);
	VERIFY(pthread_cond_destroy(&send_complete_) == 0);
	if (rpdu_.buf)
		free(rpdu_.buf);
	VERIFY(!wpdu_.buf);
	close(fd_);
}

void
connection::incref()
{
	ScopedLock ml(&ref_m_);
	refno_++;
}

bool
connection::isdead()
{
	ScopedLock ml(&m_);
	return dead_;
}

void
connection::closeconn()
{
	{
		ScopedLock ml(&m_);
		if (!dead_) {
			dead_ = true;
			shutdown(fd_,SHUT_RDWR);
		}else{
			return;
		}
	}
	//after block_remove_fd, select will never wait on fd_ 
	//and no callbacks will be active
	PollMgr::Instance()->block_remove_fd(fd_);
}

void
connection::decref()
{
	VERIFY(pthread_mutex_lock(&ref_m_)==0);
	refno_ --;
	VERIFY(refno_>=0);
	if (refno_==0) {
		VERIFY(pthread_mutex_lock(&m_)==0);
		if (dead_) {
			VERIFY(pthread_mutex_unlock(&ref_m_)==0);
			VERIFY(pthread_mutex_unlock(&m_)==0);
			delete this;
			return;
		}
		VERIFY(pthread_mutex_unlock(&m_)==0);
	}
	pthread_mutex_unlock(&ref_m_);
}

int
connection::ref()
{
	ScopedLock rl(&ref_m_);
	return refno_;
}

int
connection::compare(connection *another)
{
        if (create_time_.tv_sec > another->create_time_.tv_sec)
                return 1;
        if (create_time_.tv_sec < another->create_time_.tv_sec)
                return -1;
        if (create_time_.tv_usec > another->create_time_.tv_usec)
                return 1;
        if (create_time_.tv_usec < another->create_time_.tv_usec)
                return -1;
        return 0;
}

bool
connection::send(char *b, int sz)
{
	ScopedLock ml(&m_);
	waiters_++;
	while (!dead_ && wpdu_.buf) {
		VERIFY(pthread_cond_wait(&send_wait_, &m_)==0);
	}
	waiters_--;
	if (dead_) {
		return false;
	}
	wpdu_.buf = b;
	wpdu_.sz = sz;
	wpdu_.solong = 0;

	if (lossy_) {
		if ((random()%100) < lossy_) {
			jsl_log(JSL_DBG_1, "connection::send LOSSY TEST shutdown fd_ %d\n", fd_);
			shutdown(fd_,SHUT_RDWR);
		}
	}

	if (!writepdu()) {
		dead_ = true;
		VERIFY(pthread_mutex_unlock(&m_) == 0);
		PollMgr::Instance()->block_remove_fd(fd_);
		VERIFY(pthread_mutex_lock(&m_) == 0);
	}else{
		if (wpdu_.solong == wpdu_.sz) {
		}else{
			//should be rare to need to explicitly add write callback
			PollMgr::Instance()->add_callback(fd_, CB_WRONLY, this);
			while (!dead_ && wpdu_.solong >= 0 && wpdu_.solong < wpdu_.sz) {
				VERIFY(pthread_cond_wait(&send_complete_,&m_) == 0);
			}
		}
	}
	bool ret = (!dead_ && wpdu_.solong == wpdu_.sz);
	wpdu_.solong = wpdu_.sz = 0;
	wpdu_.buf = NULL;
	if (waiters_ > 0)
		pthread_cond_broadcast(&send_wait_);
	return ret;
}

//fd_ is ready to be written
void
connection::write_cb(int s)
{
	ScopedLock ml(&m_);
	VERIFY(!dead_);
	VERIFY(fd_ == s);
	if (wpdu_.sz == 0) {
		PollMgr::Instance()->del_callback(fd_,CB_WRONLY);
		return;
	}
	if (!writepdu()) {
		PollMgr::Instance()->del_callback(fd_, CB_RDWR);
		dead_ = true;
	}else{
		VERIFY(wpdu_.solong >= 0);
		if (wpdu_.solong < wpdu_.sz) {
			return;
		}
	} 
	pthread_cond_signal(&send_complete_);
}

//fd_ is ready to be read
void
connection::read_cb(int s)
{
	ScopedLock ml(&m_);
	VERIFY(fd_ == s);
	if (dead_)  {
		return;
	}

	bool succ = true;
	if (!rpdu_.buf || rpdu_.solong < rpdu_.sz) {
		succ = readpdu();
	}

	if (!succ) {
		PollMgr::Instance()->del_callback(fd_,CB_RDWR);
		dead_ = true;
		pthread_cond_signal(&send_complete_);
	}

	if (rpdu_.buf && rpdu_.sz == rpdu_.solong) {
		if (mgr_->got_pdu(this, rpdu_.buf, rpdu_.sz)) {
			//chanmgr has successfully consumed the pdu
			rpdu_.buf = NULL;
			rpdu_.sz = rpdu_.solong = 0;
		}
	}
}

bool
connection::writepdu()
{
	VERIFY(wpdu_.solong >= 0);
	if (wpdu_.solong == wpdu_.sz)
		return true;

	if (wpdu_.solong == 0) {
		int sz = htonl(wpdu_.sz);
		bcopy(&sz,wpdu_.buf,sizeof(sz));
	}
	int n = write(fd_, wpdu_.buf + wpdu_.solong, (wpdu_.sz-wpdu_.solong));
	if (n < 0) {
		if (errno != EAGAIN) {
			jsl_log(JSL_DBG_1, "connection::writepdu fd_ %d failure errno=%d\n", fd_, errno);
			wpdu_.solong = -1;
			wpdu_.sz = 0;
		}
		return (errno == EAGAIN);
	}
	wpdu_.solong += n;
	return true;
}

bool
connection::readpdu()
{
	if (!rpdu_.sz) {
		int sz, sz1;
		int n = read(fd_, &sz1, sizeof(sz1));

		if (n == 0) {
			return false;
		}

		if (n < 0) {
			VERIFY(errno!=EAGAIN);
			return false;
		}

		if (n >0 && n!= sizeof(sz)) {
			jsl_log(JSL_DBG_OFF, "connection::readpdu short read of sz\n");
			return false;
		}

		sz = ntohl(sz1);

		if (sz > MAX_PDU) {
			char *tmpb = (char *)&sz1;
			jsl_log(JSL_DBG_2, "connection::readpdu read pdu TOO BIG %d network order=%x %x %x %x %x\n", sz, 
					sz1, tmpb[0],tmpb[1],tmpb[2],tmpb[3]);
			return false;
		}

		rpdu_.sz = sz;
		VERIFY(rpdu_.buf == NULL);
		rpdu_.buf = (char *)malloc(sz+sizeof(sz));
		VERIFY(rpdu_.buf);
		bcopy(&sz1,rpdu_.buf,sizeof(sz));
		rpdu_.solong = sizeof(sz);
	}

	int n = read(fd_, rpdu_.buf + rpdu_.solong, rpdu_.sz - rpdu_.solong);
	if (n <= 0) {
		if (errno == EAGAIN)
			return true;
		if (rpdu_.buf)
			free(rpdu_.buf);
		rpdu_.buf = NULL;
		rpdu_.sz = rpdu_.solong = 0;
		return (errno == EAGAIN);
	}
	rpdu_.solong += n;
	return true;
}

tcpsconn::tcpsconn(chanmgr *m1, int port, int lossytest) 
: mgr_(m1), lossy_(lossytest)
{

	VERIFY(pthread_mutex_init(&m_,NULL) == 0);

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	tcp_ = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp_ < 0){
		perror("tcpsconn::tcpsconn accept_loop socket:");
		VERIFY(0);
	}

	int yes = 1;
	setsockopt(tcp_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	setsockopt(tcp_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

	if(bind(tcp_, (sockaddr *)&sin, sizeof(sin)) < 0){
		perror("accept_loop tcp bind:");
		VERIFY(0);
	}

	if(listen(tcp_, 1000) < 0) {
		perror("tcpsconn::tcpsconn listen:");
		VERIFY(0);
	}

	jsl_log(JSL_DBG_2, "tcpsconn::tcpsconn listen on %d %d\n", port, 
		sin.sin_port);

	if (pipe(pipe_) < 0) {
		perror("accept_loop pipe:");
		VERIFY(0);
	}

	int flags = fcntl(pipe_[0], F_GETFL, NULL);
	flags |= O_NONBLOCK;
	fcntl(pipe_[0], F_SETFL, flags);

	VERIFY((th_ = method_thread(this, false, &tcpsconn::accept_conn)) != 0); 
}

tcpsconn::~tcpsconn()
{
	VERIFY(close(pipe_[1]) == 0);
	VERIFY(pthread_join(th_, NULL) == 0);

	//close all the active connections
	std::map<int, connection *>::iterator i;
	for (i = conns_.begin(); i != conns_.end(); i++) {
		i->second->closeconn();
		i->second->decref();
	}	
}

void
tcpsconn::process_accept()
{
	sockaddr_in sin;
	socklen_t slen = sizeof(sin);
	int s1 = accept(tcp_, (sockaddr *)&sin, &slen); 
	if (s1 < 0) {
		perror("tcpsconn::accept_conn error");
		pthread_exit(NULL);
	}

	jsl_log(JSL_DBG_2, "accept_loop got connection fd=%d %s:%d\n", 
			s1, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	connection *ch = new connection(mgr_, s1, lossy_);

        // garbage collect all dead connections with refcount of 1
        std::map<int, connection *>::iterator i;
        for (i = conns_.begin(); i != conns_.end();) {
                if (i->second->isdead() && i->second->ref() == 1) {
			jsl_log(JSL_DBG_2, "accept_loop garbage collected fd=%d\n",
					i->second->channo());
                        i->second->decref();
                        // Careful not to reuse i right after erase. (i++) will
                        // be evaluated before the erase call because in C++,
                        // there is a sequence point before a function call.
                        // See http://en.wikipedia.org/wiki/Sequence_point.
                        conns_.erase(i++);
                } else
                        ++i;
        }

	conns_[ch->channo()] = ch;
}

void
tcpsconn::accept_conn()
{
	fd_set rfds;
	int max_fd = pipe_[0] > tcp_ ? pipe_[0] : tcp_;

	while (1) { 
		FD_ZERO(&rfds);
		FD_SET(pipe_[0], &rfds);
		FD_SET(tcp_, &rfds);

		int ret = select(max_fd+1, &rfds, NULL, NULL, NULL);

		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				perror("accept_conn select:");
				jsl_log(JSL_DBG_OFF, "tcpsconn::accept_conn failure errno %d\n",errno);
				VERIFY(0);
	                }
		}

		if (FD_ISSET(pipe_[0], &rfds)) {
			close(pipe_[0]);
			close(tcp_);
			return;
		}
		else if (FD_ISSET(tcp_, &rfds)) {
			process_accept();
		} else {
			VERIFY(0);
		}
	}
}

connection *
connect_to_dst(const sockaddr_in &dst, chanmgr *mgr, int lossy)
{
	int s= socket(AF_INET, SOCK_STREAM, 0);
	int yes = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
	if(connect(s, (sockaddr*)&dst, sizeof(dst)) < 0) {
		jsl_log(JSL_DBG_1, "rpcc::connect_to_dst failed to %s:%d\n", 
				inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
		close(s);
		return NULL;
	}
	jsl_log(JSL_DBG_2, "connect_to_dst fd=%d to dst %s:%d\n",
			s, inet_ntoa(dst.sin_addr), (int)ntohs(dst.sin_port));
	return new connection(mgr, s, lossy);
}


