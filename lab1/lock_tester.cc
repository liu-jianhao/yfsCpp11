//
// Lock server tester
//

#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
#include "jsl_log.h"
#include <arpa/inet.h>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include "lang/verify.h"

// must be >= 2
int nt = 6; //XXX: lab1's rpc handlers are blocking. Since rpcs uses a thread pool of 10 threads, we cannot test more than 10 blocking rpc.
std::string dst;
lock_client **lc = new lock_client * [nt];
lock_protocol::lockid_t a = 1;
lock_protocol::lockid_t b = 2;
lock_protocol::lockid_t c = 3;

// check_grant() and check_release() check that the lock server
// doesn't grant the same lock to both clients.
// it assumes that lock names are distinct in the first byte.
int ct[256];
pthread_mutex_t count_mutex;

void
check_grant(lock_protocol::lockid_t lid)
{
  ScopedLock ml(&count_mutex);
  int x = lid & 0xff;
  if(ct[x] != 0){
    fprintf(stderr, "error: server granted %016llx twice\n", lid);
    fprintf(stdout, "error: server granted %016llx twice\n", lid);
    exit(1);
  }
  ct[x] += 1;
}

void
check_release(lock_protocol::lockid_t lid)
{
  ScopedLock ml(&count_mutex);
  int x = lid & 0xff;
  if(ct[x] != 1){
    fprintf(stderr, "error: client released un-held lock %016llx\n",  lid);
    exit(1);
  }
  ct[x] -= 1;
}

void
test1(void)
{
    printf ("acquire a release a acquire a release a\n");
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->release(a);
    check_release(a);
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->release(a);
    check_release(a);

    printf ("acquire a acquire b release b release a\n");
    lc[0]->acquire(a);
    check_grant(a);
    lc[0]->acquire(b);
    check_grant(b);
    lc[0]->release(b);
    check_release(b);
    lc[0]->release(a);
    check_release(a);
}

void *
test2(void *x) 
{
  int i = * (int *) x;

  printf ("test2: client %d acquire a release a\n", i);
  lc[i]->acquire(a);
  printf ("test2: client %d acquire done\n", i);
  check_grant(a);
  sleep(1);
  printf ("test2: client %d release\n", i);
  check_release(a);
  lc[i]->release(a);
  printf ("test2: client %d release done\n", i);
  return 0;
}

void *
test3(void *x)
{
  int i = * (int *) x;

  printf ("test3: client %d acquire a release a concurrent\n", i);
  for (int j = 0; j < 10; j++) {
    lc[i]->acquire(a);
    check_grant(a);
    printf ("test3: client %d got lock\n", i);
    check_release(a);
    lc[i]->release(a);
  }
  return 0;
}

void *
test4(void *x)
{
  int i = * (int *) x;

  printf ("test4: thread %d acquire a release a concurrent; same clnt\n", i);
  for (int j = 0; j < 10; j++) {
    lc[0]->acquire(a);
    check_grant(a);
    printf ("test4: thread %d on client 0 got lock\n", i);
    check_release(a);
    lc[0]->release(a);
  }
  return 0;
}

void *
test5(void *x)
{
  int i = * (int *) x;

  printf ("test5: client %d acquire a release a concurrent; same and diff clnt\n", i);
  for (int j = 0; j < 10; j++) {
    if (i < 5)  lc[0]->acquire(a);
    else  lc[1]->acquire(a);
    check_grant(a);
    printf ("test5: client %d got lock\n", i);
    check_release(a);
    if (i < 5) lc[0]->release(a);
    else lc[1]->release(a);
  }
  return 0;
}

int
main(int argc, char *argv[])
{
    int r;
    pthread_t th[nt];
    int test = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    srandom(getpid());

    //jsl_set_debug(2);

    if(argc < 2) {
      fprintf(stderr, "Usage: %s [host:]port [test]\n", argv[0]);
      exit(1);
    }

    dst = argv[1]; 

    if (argc > 2) {
      test = atoi(argv[2]);
      if(test < 1 || test > 5){
        printf("Test number must be between 1 and 5\n");
        exit(1);
      }
    }

    VERIFY(pthread_mutex_init(&count_mutex, NULL) == 0);
    printf("simple lock client\n");
    for (int i = 0; i < nt; i++) lc[i] = new lock_client(dst);

    if(!test || test == 1){
      test1();
    }

    if(!test || test == 2){
      // test2
      for (int i = 0; i < nt; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test2, (void *) a);
	VERIFY (r == 0);
      }
      for (int i = 0; i < nt; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 3){
      printf("test 3\n");
      
      // test3
      for (int i = 0; i < nt; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test3, (void *) a);
	VERIFY (r == 0);
      }
      for (int i = 0; i < nt; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 4){
      printf("test 4\n");
      
      // test 4
      for (int i = 0; i < 2; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test4, (void *) a);
	VERIFY (r == 0);
      }
      for (int i = 0; i < 2; i++) {
	pthread_join(th[i], NULL);
      }
    }

    if(!test || test == 5){
      printf("test 5\n");
      
      // test 5
      
      for (int i = 0; i < nt; i++) {
	int *a = new int (i);
	r = pthread_create(&th[i], NULL, test5, (void *) a);
	VERIFY (r == 0);
      }
      for (int i = 0; i < nt; i++) {
	pthread_join(th[i], NULL);
      }
    }

    printf ("%s: passed all tests successfully\n", argv[0]);

}
