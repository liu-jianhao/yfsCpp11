#ifndef pollmgr_h
#define pollmgr_h 

#include <sys/select.h>
#include <vector>

#ifdef __linux__
#include <sys/epoll.h>
#endif

#define MAX_POLL_FDS 128

typedef enum {
	CB_NONE = 0x0,
	CB_RDONLY = 0x1,
	CB_WRONLY = 0x10,
	CB_RDWR = 0x11,
	CB_MASK = ~0x11,
} poll_flag;

class aio_mgr {
	public:
		virtual void watch_fd(int fd, poll_flag flag) = 0;
		virtual bool unwatch_fd(int fd, poll_flag flag) = 0;
		virtual bool is_watched(int fd, poll_flag flag) = 0;
		virtual void wait_ready(std::vector<int> *readable, std::vector<int> *writable) = 0;
		virtual ~aio_mgr() {}
};

class aio_callback {
	public:
		virtual void read_cb(int fd) = 0;
		virtual void write_cb(int fd) = 0;
		virtual ~aio_callback() {}
};

class PollMgr {
	public:
		PollMgr();
		~PollMgr();

		static PollMgr *Instance();
		static PollMgr *CreateInst();

		void add_callback(int fd, poll_flag flag, aio_callback *ch);
		void del_callback(int fd, poll_flag flag);
		bool has_callback(int fd, poll_flag flag, aio_callback *ch);
		void block_remove_fd(int fd);
		void wait_loop();


		static PollMgr *instance;
		static int useful;
		static int useless;

	private:
		pthread_mutex_t m_;
		pthread_cond_t changedone_c_;
		pthread_t th_;

		aio_callback *callbacks_[MAX_POLL_FDS];
		aio_mgr *aio_;
		bool pending_change_;

};

class SelectAIO : public aio_mgr {
	public :

		SelectAIO();
		~SelectAIO();
		void watch_fd(int fd, poll_flag flag);
		bool unwatch_fd(int fd, poll_flag flag);
		bool is_watched(int fd, poll_flag flag);
		void wait_ready(std::vector<int> *readable, std::vector<int> *writable);

	private:

		fd_set rfds_;
		fd_set wfds_;
		int highfds_;
		int pipefd_[2];

		pthread_mutex_t m_;

};

#ifdef __linux__ 
class EPollAIO : public aio_mgr {
	public:
		EPollAIO();
		~EPollAIO();
		void watch_fd(int fd, poll_flag flag);
		bool unwatch_fd(int fd, poll_flag flag);
		bool is_watched(int fd, poll_flag flag);
		void wait_ready(std::vector<int> *readable, std::vector<int> *writable);

	private:
		int pollfd_;
		struct epoll_event ready_[MAX_POLL_FDS];
		int fdstatus_[MAX_POLL_FDS];

};
#endif /* __linux */

#endif /* pollmgr_h */

