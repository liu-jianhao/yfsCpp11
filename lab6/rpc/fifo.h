#ifndef fifo_h
#define fifo_h

// fifo template
// blocks enq() and deq() when queue is FULL or EMPTY

#include <errno.h>
#include <list>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include "slock.h"
#include "lang/verify.h"

template<class T>
class fifo {
	public:
		fifo(int m=0);
		~fifo();
		bool enq(T, bool blocking=true);
		void deq(T *);
		bool size();

	private:
		std::list<T> q_;
		pthread_mutex_t m_;
		pthread_cond_t non_empty_c_; // q went non-empty
		pthread_cond_t has_space_c_; // q is not longer overfull
		unsigned int max_; //maximum capacity of the queue, block enq threads if exceeds this limit
};

template<class T>
fifo<T>::fifo(int limit) : max_(limit)
{
	VERIFY(pthread_mutex_init(&m_, 0) == 0);
	VERIFY(pthread_cond_init(&non_empty_c_, 0) == 0);
	VERIFY(pthread_cond_init(&has_space_c_, 0) == 0);
}

template<class T>
fifo<T>::~fifo()
{
	//fifo is to be deleted only when no threads are using it!
	VERIFY(pthread_mutex_destroy(&m_)==0);
	VERIFY(pthread_cond_destroy(&non_empty_c_) == 0);
	VERIFY(pthread_cond_destroy(&has_space_c_) == 0);
}

template<class T> bool
fifo<T>::size()
{
	ScopedLock ml(&m_);
	return q_.size();
}

template<class T> bool
fifo<T>::enq(T e, bool blocking)
{
	ScopedLock ml(&m_);
	while (1) {
		if (!max_ || q_.size() < max_) {
			q_.push_back(e);
			break;
		}
		if (blocking)
			VERIFY(pthread_cond_wait(&has_space_c_, &m_) == 0);
		else
			return false;
	}
	VERIFY(pthread_cond_signal(&non_empty_c_) == 0);
	return true;
}

template<class T> void
fifo<T>::deq(T *e)
{
	ScopedLock ml(&m_);

	while(1) {
		if(q_.empty()){
			VERIFY (pthread_cond_wait(&non_empty_c_, &m_) == 0);
		} else {
			*e = q_.front();
			q_.pop_front();
			if (max_ && q_.size() < max_) {
				VERIFY(pthread_cond_signal(&has_space_c_)==0);
			}
			break;
		}
	}
	return;
}

#endif
