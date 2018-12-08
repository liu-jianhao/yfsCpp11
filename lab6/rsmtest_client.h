// rsmtest client interface.

#ifndef rsmtest_client_h
#define rsmtest_client_h

#include <string>
#include "rsm_protocol.h"
#include "rpc.h"

// Client interface to the rsmtest server
class rsmtest_client {
 protected:
  rpcc *cl;
 public:
  rsmtest_client(std::string d);
  virtual ~rsmtest_client() {};
  virtual rsm_test_protocol::status net_repair(int heal);
  virtual rsm_test_protocol::status breakpoint(int b);
};
#endif
