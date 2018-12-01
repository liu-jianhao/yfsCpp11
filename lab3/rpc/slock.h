#ifndef __SCOPED_LOCK__
#define __SCOPED_LOCK__

#include <pthread.h>
#include "lang/verify.h"
struct ScopedLock {
	private:
		pthread_mutex_t *m_;
	public:
		ScopedLock(pthread_mutex_t *m): m_(m) {
			VERIFY(pthread_mutex_lock(m_)==0);
		}
		~ScopedLock() {
			VERIFY(pthread_mutex_unlock(m_)==0);
		}
};
#endif  /*__SCOPED_LOCK__*/
