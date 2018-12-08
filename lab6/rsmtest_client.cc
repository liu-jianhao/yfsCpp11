// RPC stubs for clients to talk to rsmtest_server

#include "rsmtest_client.h"
#include "rpc.h"
#include <arpa/inet.h>

#include <sstream>
#include <iostream>
#include <stdio.h>

rsmtest_client::rsmtest_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() < 0) {
    printf("rsmtest_client: call bind\n");
  }
}

int
rsmtest_client::net_repair(int heal)
{
  int r;
  int ret = cl->call(rsm_test_protocol::net_repair, heal, r);
  VERIFY (ret == rsm_test_protocol::OK);
  return r;
}

int
rsmtest_client::breakpoint(int b)
{
  int r;
  int ret = cl->call(rsm_test_protocol::breakpoint, b, r);
  VERIFY (ret == rsm_test_protocol::OK);
  return r;
}


