//
// RSM test client
//

#include "rsm_protocol.h"
#include "rsmtest_client.h"
#include "rpc.h"
#include <arpa/inet.h>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <string>
using namespace std;

rsmtest_client *lc;

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 4){
    fprintf(stderr, "Usage: %s [host:]port [partition] arg\n", argv[0]);
    exit(1);
  }

  lc = new rsmtest_client(argv[1]);
  string command(argv[2]);
  if (command == "partition") {
    r = lc->net_repair(atoi(argv[3]));
    printf ("net_repair returned %d\n", r);
  } else if (command == "breakpoint") {
    int b = atoi(argv[3]);
    r = lc->breakpoint(b);
    printf ("breakpoint %d returned %d\n", b, r);
  } else {
    fprintf(stderr, "Unknown command %s\n", argv[2]);
  }
  exit(0);
}
