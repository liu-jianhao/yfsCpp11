// manage a cache of RPC connections.
// assuming cid is a std::string holding the
// host:port of the RPC server you want
// to talk to:
//
// handle h(cid);
// rpcc *cl = h.safebind();
// if(cl){
//   ret = cl->call(...);
// } else {
//   bind() failed
// }
//
// if the calling program has not contacted
// cid before, safebind() will create a new
// connection, call bind(), and return
// an rpcc*, or 0 if bind() failed. if the
// program has previously contacted cid,
// safebind() just returns the previously
// created rpcc*. best not to hold any
// mutexes while calling safebind().

#ifndef handle_h
#define handle_h

#include <string>
#include <vector>
#include "rpc.h"

struct hinfo {
  rpcc *cl;
  int refcnt;
  bool del;
  std::string m;
  pthread_mutex_t cl_mutex;
};

class handle {
 private:
  struct hinfo *h;
 public:
  handle(std::string m);
  ~handle();
  /* safebind will try to bind with the rpc server on the first call.
   * Since bind may block, the caller probably should not hold a mutex
   * when calling safebind.
   *
   * return: 
   *   if the first safebind succeeded, all later calls would return
   *   a rpcc object; otherwise, all later calls would return NULL.
   *
   * Example:
   *   handle h(dst);
   *   XXX_protocol::status ret;
   *   if (h.safebind()) {
   *     ret = h.safebind()->call(...);
   *   }
   *   if (!h.safebind() || ret != XXX_protocol::OK) {
   *     // handle failure
   *   }
   */
  rpcc *safebind();
};

class handle_mgr {
 private:
  pthread_mutex_t handle_mutex;
  std::map<std::string, struct hinfo *> hmap;
 public:
  handle_mgr();
  struct hinfo *get_handle(std::string m);
  void done_handle(struct hinfo *h);
  void delete_handle(std::string m);
  void delete_handle_wo(std::string m);
};

extern class handle_mgr mgr;

#endif
