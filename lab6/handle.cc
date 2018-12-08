#include "handle.h"
#include <stdio.h>
#include "tprintf.h"

handle_mgr mgr;

handle::handle(std::string m) 
{
  h = mgr.get_handle(m);
}

rpcc *
handle::safebind()
{
  if (!h)
    return NULL;
  ScopedLock ml(&h->cl_mutex);
  if (h->del)
    return NULL;
  if (h->cl)
    return h->cl;
  sockaddr_in dstsock;
  make_sockaddr(h->m.c_str(), &dstsock);
  rpcc *cl = new rpcc(dstsock);
  tprintf("handler_mgr::get_handle trying to bind...%s\n", h->m.c_str());
  int ret;
  // Starting with lab 6, our test script assumes that the failure
  // can be detected by paxos and rsm layer within few seconds. We have
  // to set the timeout with a small value to support the assumption.
  // 
  // Note: with RPC_LOSSY=5, your lab would failed to pass the tests of
  // lab 6 and lab 7 because the rpc layer may delay your RPC request, 
  // and cause a time out failure. Please make sure RPC_LOSSY is set to 0.
  ret = cl->bind(rpcc::to(1000));
  if (ret < 0) {
    tprintf("handle_mgr::get_handle bind failure! %s %d\n", h->m.c_str(), ret);
    delete cl;
    h->del = true;
  } else {
    tprintf("handle_mgr::get_handle bind succeeded %s\n", h->m.c_str());
    h->cl = cl;
  }
  return h->cl;
}

handle::~handle() 
{
  if (h) mgr.done_handle(h);
}

handle_mgr::handle_mgr()
{
  VERIFY (pthread_mutex_init(&handle_mutex, NULL) == 0);
}

struct hinfo *
handle_mgr::get_handle(std::string m)
{
  ScopedLock ml(&handle_mutex);
  struct hinfo *h = 0;
  if (hmap.find(m) == hmap.end()) {
    h = new hinfo;
    h->cl = NULL;
    h->del = false;
    h->refcnt = 1;
    h->m = m;
    pthread_mutex_init(&h->cl_mutex, NULL);
    hmap[m] = h;
  } else if (!hmap[m]->del) {
    h = hmap[m];
    h->refcnt ++;
  }
  return h;
}

void 
handle_mgr::done_handle(struct hinfo *h)
{
  ScopedLock ml(&handle_mutex);
  h->refcnt--;
  if (h->refcnt == 0 && h->del)
    delete_handle_wo(h->m);
}

void
handle_mgr::delete_handle(std::string m)
{
  ScopedLock ml(&handle_mutex);
  delete_handle_wo(m);
}

// Must be called with handle_mutex locked.
void
handle_mgr::delete_handle_wo(std::string m)
{
  if (hmap.find(m) == hmap.end()) {
    tprintf("handle_mgr::delete_handle_wo: cl %s isn't in cl list\n", m.c_str());
  } else {
    tprintf("handle_mgr::delete_handle_wo: cl %s refcnt %d\n", m.c_str(),
	   hmap[m]->refcnt);
    struct hinfo *h = hmap[m];
    if (h->refcnt == 0) {
      if (h->cl) {
        h->cl->cancel();
        delete h->cl;
      }
      pthread_mutex_destroy(&h->cl_mutex);
      hmap.erase(m);
      delete h;
    } else {
      h->del = true;
    }
  }
}
