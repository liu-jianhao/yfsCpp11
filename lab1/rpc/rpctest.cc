// RPC test and pseudo-documentation.
// generates print statements on failures, but eventually says "rpctest OK"

#include "rpc.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "jsl_log.h"
#include "gettime.h"
#include "lang/verify.h"

#define NUM_CL 2

rpcs *server;  // server rpc object
rpcc *clients[NUM_CL];  // client rpc object
struct sockaddr_in dst; //server's ip address
int port;
pthread_attr_t attr;

// server-side handlers. they must be methods of some class
// to simplify rpcs::reg(). a server process can have handlers
// from multiple classes.
class srv {
	public:
		int handle_22(const std::string a, const std::string b, std::string & r);
		int handle_fast(const int a, int &r);
		int handle_slow(const int a, int &r);
		int handle_bigrep(const int a, std::string &r);
};

// a handler. a and b are arguments, r is the result.
// there can be multiple arguments but only one result.
// the caller also gets to see the int return value
// as the return value from rpcc::call().
// rpcs::reg() decides how to unmarshall by looking
// at these argument types, so this function definition
// does what a .x file does in SunRPC.
int
srv::handle_22(const std::string a, std::string b, std::string &r)
{
	r = a + b;
	return 0;
}

int
srv::handle_fast(const int a, int &r)
{
	r = a + 1;
	return 0;
}

int
srv::handle_slow(const int a, int &r)
{
	usleep(random() % 5000);
	r = a + 2;
	return 0;
}

int
srv::handle_bigrep(const int len, std::string &r)
{
	r = std::string(len, 'x');
	return 0;
}

srv service;

void startserver()
{
	server = new rpcs(port);
	server->reg(22, &service, &srv::handle_22);
	server->reg(23, &service, &srv::handle_fast);
	server->reg(24, &service, &srv::handle_slow);
	server->reg(25, &service, &srv::handle_bigrep);
}

void
testmarshall()
{
	marshall m;
	req_header rh(1,2,3,4,5);
	m.pack_req_header(rh);
	VERIFY(m.size()==RPC_HEADER_SZ);
	int i = 12345;
	unsigned long long l = 1223344455L;
	std::string s = std::string("hallo....");
	m << i;
	m << l;
	m << s;

	char *b;
	int sz;
	m.take_buf(&b,&sz);
	VERIFY(sz == (int)(RPC_HEADER_SZ+sizeof(i)+sizeof(l)+s.size()+sizeof(int)));

	unmarshall un(b,sz);
	req_header rh1;
	un.unpack_req_header(&rh1);
	VERIFY(memcmp(&rh,&rh1,sizeof(rh))==0);
	int i1;
	unsigned long long l1;
	std::string s1;
	un >> i1;
	un >> l1;
	un >> s1;
	VERIFY(un.okdone());
	VERIFY(i1==i && l1==l && s1==s);
}

void *
client1(void *xx)
{

	// test concurrency.
	int which_cl = ((unsigned long) xx ) % NUM_CL;

	for(int i = 0; i < 100; i++){
		int arg = (random() % 2000);
		std::string rep;
		int ret = clients[which_cl]->call(25, arg, rep);
		VERIFY(ret == 0);
		if ((int)rep.size()!=arg) {
			printf("repsize wrong %d!=%d\n", (int)rep.size(), arg);
		}
		VERIFY((int)rep.size() == arg);
	}

	// test rpc replies coming back not in the order of
	// the original calls -- i.e. does xid reply dispatch work.
	for(int i = 0; i < 100; i++){
		int which = (random() % 2);
		int arg = (random() % 1000);
		int rep;

		struct timespec start,end;
		clock_gettime(CLOCK_REALTIME, &start);

		int ret = clients[which_cl]->call(which ? 23 : 24, arg, rep);
		clock_gettime(CLOCK_REALTIME, &end);
		int diff = diff_timespec(end, start);
		if (ret != 0)
			printf("%d ms have elapsed!!!\n", diff);
		VERIFY(ret == 0);
		VERIFY(rep == (which ? arg+1 : arg+2));
	}

	return 0;
}

void *
client2(void *xx)
{
	int which_cl = ((unsigned long) xx ) % NUM_CL;

	time_t t1;
	time(&t1);

	while(time(0) - t1 < 10){
		int arg = (random() % 2000);
		std::string rep;
		int ret = clients[which_cl]->call(25, arg, rep);
		if ((int)rep.size()!=arg) {
			printf("ask for %d reply got %d ret %d\n",
                               arg, (int)rep.size(), ret);
		}
		VERIFY((int)rep.size() == arg);
	}
	return 0;
}

void *
client3(void *xx)
{
	rpcc *c = (rpcc *) xx;

	for(int i = 0; i < 4; i++){
		int rep;
		int ret = c->call(24, i, rep, rpcc::to(3000));
		VERIFY(ret == rpc_const::timeout_failure || rep == i+2);
	}
	return 0;
}


void
simple_tests(rpcc *c)
{
	printf("simple_tests\n");
	// an RPC call to procedure #22.
	// rpcc::call() looks at the argument types to decide how
	// to marshall the RPC call packet, and how to unmarshall
	// the reply packet.
	std::string rep;
	int intret = c->call(22, (std::string)"hello", (std::string)" goodbye", rep);
	VERIFY(intret == 0); // this is what handle_22 returns
	VERIFY(rep == "hello goodbye");
	printf("   -- string concat RPC .. ok\n");

	// small request, big reply (perhaps req via UDP, reply via TCP)
	intret = c->call(25, 70000, rep, rpcc::to(200000));
	VERIFY(intret == 0);
	VERIFY(rep.size() == 70000);
	printf("   -- small request, big reply .. ok\n");

	// too few arguments
	intret = c->call(22, (std::string)"just one", rep);
	VERIFY(intret < 0);
	printf("   -- too few arguments .. failed ok\n");

	// too many arguments; proc #23 expects just one.
	intret = c->call(23, 1001, 1002, rep);
	VERIFY(intret < 0);
	printf("   -- too many arguments .. failed ok\n");

	// wrong return value size
	int wrongrep;
	intret = c->call(23, (std::string)"hello", (std::string)" goodbye", wrongrep);
	VERIFY(intret < 0);
	printf("   -- wrong ret value size .. failed ok\n");

	// specify a timeout value to an RPC that should succeed (udp)
	int xx = 0;
	intret = c->call(23, 77, xx, rpcc::to(3000));
	VERIFY(intret == 0 && xx == 78);
	printf("   -- no suprious timeout .. ok\n");

	// specify a timeout value to an RPC that should succeed (tcp)
	{
		std::string arg(1000, 'x');
		std::string rep;
		c->call(22, arg, (std::string)"x", rep, rpcc::to(3000));
		VERIFY(rep.size() == 1001);
		printf("   -- no suprious timeout .. ok\n");
	}

	// huge RPC
	std::string big(1000000, 'x');
	intret = c->call(22, big, (std::string)"z", rep);
	VERIFY(rep.size() == 1000001);
	printf("   -- huge 1M rpc request .. ok\n");

	// specify a timeout value to an RPC that should timeout (udp)
	struct sockaddr_in non_existent;
	memset(&non_existent, 0, sizeof(non_existent));
	non_existent.sin_family = AF_INET;
	non_existent.sin_addr.s_addr = inet_addr("127.0.0.1");
	non_existent.sin_port = htons(7661);
	rpcc *c1 = new rpcc(non_existent);
	time_t t0 = time(0);
	intret = c1->bind(rpcc::to(3000));
	time_t t1 = time(0);
	VERIFY(intret < 0 && (t1 - t0) <= 4);
	printf("   -- rpc timeout .. ok\n");
	printf("simple_tests OK\n");
}

void 
concurrent_test(int nt)
{
	// create threads that make lots of calls in parallel,
	// to test thread synchronization for concurrent calls
	// and dispatches.
	int ret;

	printf("start concurrent_test (%d threads) ...", nt);

	pthread_t th[nt];
	for(int i = 0; i < nt; i++){
		ret = pthread_create(&th[i], &attr, client1, (void *) i);
		VERIFY(ret == 0);
	}

	for(int i = 0; i < nt; i++){
		VERIFY(pthread_join(th[i], NULL) == 0);
	}
	printf(" OK\n");
}

void 
lossy_test()
{
	int ret;

	printf("start lossy_test ...");
	VERIFY(setenv("RPC_LOSSY", "5", 1) == 0);

	if (server) {
		delete server;
		startserver();
	}

	for (int i = 0; i < NUM_CL; i++) {
		delete clients[i];
		clients[i] = new rpcc(dst);
		VERIFY(clients[i]->bind()==0);
	}

	int nt = 1;
	pthread_t th[nt];
	for(int i = 0; i < nt; i++){
		ret = pthread_create(&th[i], &attr, client2, (void *) i);
		VERIFY(ret == 0);
	}
	for(int i = 0; i < nt; i++){
		VERIFY(pthread_join(th[i], NULL) == 0);
	}
	printf(".. OK\n");
	VERIFY(setenv("RPC_LOSSY", "0", 1) == 0);
}

void 
failure_test()
{
	rpcc *client1;
	rpcc *client = clients[0];

	printf("failure_test\n");

	delete server;

	client1 = new rpcc(dst);
	VERIFY (client1->bind(rpcc::to(3000)) < 0);
	printf("   -- create new client and try to bind to failed server .. failed ok\n");

	delete client1;

	startserver();

	std::string rep;
	int intret = client->call(22, (std::string)"hello", (std::string)" goodbye", rep);
	VERIFY(intret == rpc_const::oldsrv_failure);
	printf("   -- call recovered server with old client .. failed ok\n");

	delete client;

	clients[0] = client = new rpcc(dst);
	VERIFY (client->bind() >= 0);
	VERIFY (client->bind() < 0);

	intret = client->call(22, (std::string)"hello", (std::string)" goodbye", rep);
	VERIFY(intret == 0);
	VERIFY(rep == "hello goodbye");

	printf("   -- delete existing rpc client, create replacement rpc client .. ok\n");


	int nt = 10;
	int ret;
	printf("   -- concurrent test on new rpc client w/ %d threads ..", nt);

	pthread_t th[nt];
	for(int i = 0; i < nt; i++){
		ret = pthread_create(&th[i], &attr, client3, (void *) client);
		VERIFY(ret == 0);
	}

	for(int i = 0; i < nt; i++){
		VERIFY(pthread_join(th[i], NULL) == 0);
	}
	printf("ok\n");

	delete server;
	delete client;

	startserver();
	clients[0] = client = new rpcc(dst);
	VERIFY (client->bind() >= 0);
	printf("   -- delete existing rpc client and server, create replacements.. ok\n");

	printf("   -- concurrent test on new client and server w/ %d threads ..", nt);
	for(int i = 0; i < nt; i++){
		ret = pthread_create(&th[i], &attr, client3, (void *)client);
		VERIFY(ret == 0);
	}

	for(int i = 0; i < nt; i++){
		VERIFY(pthread_join(th[i], NULL) == 0);
	}
	printf("ok\n");

	printf("failure_test OK\n");
}

int
main(int argc, char *argv[])
{

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	int debug_level = 0;

	bool isclient = false;
	bool isserver = false;

	srandom(getpid());
	port = 20000 + (getpid() % 10000);

	char ch = 0;
	while ((ch = getopt(argc, argv, "csd:p:l"))!=-1) {
		switch (ch) {
			case 'c':
				isclient = true;
				break;
			case 's':
				isserver = true;
				break;
			case 'd':
				debug_level = atoi(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'l':
				VERIFY(setenv("RPC_LOSSY", "5", 1) == 0);
			default:
				break;
		}
	}

	if (!isserver && !isclient)  {
		isserver = isclient = true;
	}

	if (debug_level > 0) {
		//__loginit.initNow();
		jsl_set_debug(debug_level);
		jsl_log(JSL_DBG_1, "DEBUG LEVEL: %d\n", debug_level);
	}

	testmarshall();

	pthread_attr_init(&attr);
	// set stack size to 32K, so we don't run out of memory
	pthread_attr_setstacksize(&attr, 32*1024);

	if (isserver) {
		printf("starting server on port %d RPC_HEADER_SZ %d\n", port, RPC_HEADER_SZ);
		startserver();
	}

	if (isclient) {
		// server's address.
		memset(&dst, 0, sizeof(dst));
		dst.sin_family = AF_INET;
		dst.sin_addr.s_addr = inet_addr("127.0.0.1");
		dst.sin_port = htons(port);


		// start the client.  bind it to the server.
		// starts a thread to listen for replies and hand them to
		// the correct waiting caller thread. there should probably
		// be only one rpcc per process. you probably need one
		// rpcc per server.
		for (int i = 0; i < NUM_CL; i++) {
			clients[i] = new rpcc(dst);
			VERIFY (clients[i]->bind() == 0);
		}

		simple_tests(clients[0]);
		concurrent_test(10);
		lossy_test();
		if (isserver) {
			failure_test();
		}

		printf("rpctest OK\n");

		exit(0);
	}

	while (1) {
		sleep(1);
	}
}
