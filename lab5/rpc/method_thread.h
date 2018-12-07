#ifndef method_thread_h
#define method_thread_h

// method_thread(): start a thread that runs an object method.
// returns a pthread_t on success, and zero on error.

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lang/verify.h"

static pthread_t
method_thread_parent(void *(*fn)(void *), void *arg, bool detach)
{
	pthread_t th;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	// set stack size to 100K, so we don't run out of memory
	pthread_attr_setstacksize(&attr, 100*1024);
	int err = pthread_create(&th, &attr, fn, arg);
	pthread_attr_destroy(&attr);
	if (err != 0) {
		fprintf(stderr, "pthread_create ret %d %s\n", err, strerror(err));
		exit(1);
	}

	if (detach) {
		// don't keep thread state around after exit, to avoid
		// running out of threads. set detach==false if you plan
		// to pthread_join.
		VERIFY(pthread_detach(th) == 0);
	}

	return th;
}

static void
method_thread_child()
{
	// defer pthread_cancel() by default. check explicitly by
	// enabling then pthread_testcancel().
	int oldstate, oldtype;
	VERIFY(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) == 0);
	VERIFY(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype) == 0);
}

template <class C> pthread_t 
method_thread(C *o, bool detach, void (C::*m)())
{
	class XXX {
		public:
			C *o;
			void (C::*m)();
			static void *yyy(void *vvv) {
				XXX *x = (XXX*)vvv;
				C *o = x->o;
				void (C::*m)() = x->m;
				delete x;
				method_thread_child();
				(o->*m)();
				return 0;
			}
	};
	XXX *x = new XXX;
	x->o = o;
	x->m = m;
	return method_thread_parent(&XXX::yyy, (void *) x, detach);
}

template <class C, class A> pthread_t
method_thread(C *o, bool detach, void (C::*m)(A), A a)
{
	class XXX {
		public:
			C *o;
			void (C::*m)(A a);
			A a;
			static void *yyy(void *vvv) {
				XXX *x = (XXX*)vvv;
				C *o = x->o;
				void (C::*m)(A ) = x->m;
				A a = x->a;
				delete x;
				method_thread_child();
				(o->*m)(a);
				return 0;
			}
	};
	XXX *x = new XXX;
	x->o = o;
	x->m = m;
	x->a = a;
	return method_thread_parent(&XXX::yyy, (void *) x, detach);
}

namespace {
	// ~xavid: this causes a bizzare compile error on OS X.5 when
	//         it's declared in the function, so I moved it out here.
	template <class C, class A1, class A2>
		class XXX {
			public:
				C *o;
				void (C::*m)(A1 a1, A2 a2);
				A1 a1;
				A2  a2;
				static void *yyy(void *vvv) {
					XXX *x = (XXX*)vvv;
					C *o = x->o;
					void (C::*m)(A1 , A2 ) = x->m;
					A1 a1 = x->a1;
					A2 a2 = x->a2;
					delete x;
					method_thread_child();
					(o->*m)(a1, a2);
					return 0;
				}
		};
}

template <class C, class A1, class A2> pthread_t
method_thread(C *o, bool detach, void (C::*m)(A1 , A2 ), A1 a1, A2 a2)
{
	XXX<C,A1,A2> *x = new XXX<C,A1,A2>;
	x->o = o;
	x->m = m;
	x->a1 = a1;
	x->a2 = a2;
	return method_thread_parent(&XXX<C,A1,A2>::yyy, (void *) x, detach);
}

template <class C, class A1, class A2, class A3> pthread_t
method_thread(C *o, bool detach, void (C::*m)(A1 , A2, A3 ), A1 a1, A2 a2, A3 a3)
{
	class XXX {
		public:
			C *o;
			void (C::*m)(A1 a1, A2 a2, A3 a3);
			A1 a1;
			A2  a2;
			A3 a3;
			static void *yyy(void *vvv) {
				XXX *x = (XXX*)vvv;
				C *o = x->o;
				void (C::*m)(A1 , A2 , A3 ) = x->m;
				A1 a1 = x->a1;
				A2 a2 = x->a2;
				A3 a3 = x->a3;
				delete x;
				method_thread_child();
				(o->*m)(a1, a2, a3);
				return 0;
			}
	};
	XXX *x = new XXX;
	x->o = o;
	x->m = m;
	x->a1 = a1;
	x->a2 = a2;
	x->a3 = a3;
	return method_thread_parent(&XXX::yyy, (void *) x, detach);
}

#endif
