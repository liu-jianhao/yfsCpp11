# Lab6：Paxos

之前的实现中没有考虑锁服务器会失败的情形，考虑到这种情形我们采用`replicated state machine(RSM)`方法来备份锁服务器

RSM基本的想法是这些机器初始状态相同，那么执行相同的操作系列后状态也是相同的. 

因为网络乱序等原因，无法保证所有备份机器收到的操作请求序列都是相同的，所以采用一机器为master,master从客户端接受请求,决定请求次序，然后发送给各个备份机器，然后以相同的次序在所有备份(replicas)机器上执行，master等待所有备份机器返回，然后master返回给客户端，当master失败，任何一个备份(replicas)可以接管工作.因为他们都有相同的状态.

上面的RSM的核心是要所有机器达成一个协议:哪一个备份(replica)是master,而哪些slave机器是正在运行的(alive),并没有fail.因为任何机器在任何时刻都有可能失败


## 实现Paxos
先理解论文内容，看懂下面的伪代码:
```
proposer run(instance, v):
 choose n, unique and higher than any n seen so far
 send prepare(instance, n) to all servers including self
 if oldinstance(instance, instance_value) from any node:
   commit to the instance_value locally
 else if prepare_ok(n_a, v_a) from majority:
   v' = v_a with highest n_a; choose own v otherwise
   send accept(instance, n, v') to all
   if accept_ok(n) from majority:
     send decided(instance, v') to all

acceptor state:
 must persist across reboots
 n_h (highest prepare seen)
 instance_h, (highest instance accepted)
 n_a, v_a (highest accept seen)

acceptor prepare(instance, n) handler:
 if instance <= instance_h
   reply oldinstance(instance, instance_value)
 else if n > n_h
   n_h = n
   reply prepare_ok(n_a, v_a)
 else
   reply prepare_reject

acceptor accept(instance, n, v) handler:
 if n >= n_h
   n_a = n
   v_a = v
   reply accept_ok(n)
 else
   reply accept_reject

acceptor decide(instance, v) handler:
 paxos_commit(instance, v)
```

`paxos.cc`是Paxos算法的实现的主要过程。

### Phase1
```cpp
bool
proposer::prepare(unsigned instance, std::vector<std::string> &accepts, 
         std::vector<std::string> nodes,
         std::string &v)
{
  prop_t max;
  max.n = 0;
  max.m = std::string();

  paxos_protocol::preparearg a;
  a.instance = instance;
  a.n = my_n;
  paxos_protocol::prepareres r;

  paxos_protocol::status ret;

  for(auto it = nodes.begin(); it != nodes.end(); ++it)
  {
    handle h(*it);

    pthread_mutex_unlock(&pxs_mutex);
    rpcc *cl = h.safebind();
    if(cl)
    {
      ret = cl->call(paxos_protocol::preparereq, me, a, r, rpcc::to(1000));
    }
    pthread_mutex_lock(&pxs_mutex);

    if(cl)
    {
      if(ret == paxos_protocol::OK)
      {
        // oldinstance为true说明未批准
        if(r.oldinstance)
        {
          acc->commit(instance, r.v_a);
          return false;
        }
        else if(r.accept)
        {
          accepts.push_back(*it);
          if(r.n_a > max)
          {
            v = r.v_a;
            max = r.n_a;
          }
        }
      }
    }
  }

  return true;
}
```

下面的函数是RPC调用，可以被每个服务器调用
```cpp
paxos_protocol::status
acceptor::preparereq(std::string src, paxos_protocol::preparearg a,
    paxos_protocol::prepareres &r)
{
  ScopedLock ml(&pxs_mutex);
  // 每个instance代表状态机的轮次
  // 该轮次小于之前已经决定的最大的轮次，则拒绝
  if(a.instance <= instance_h)
  {
    r.oldinstance = true;
    r.accept = false;
    r.v_a = values[a.instance];
  }
  // 轮次大于，并且请求的proposal number该acceptor见过的最大的还大，则该acceptor 批准该proposal
  else if(a.n > n_h)
  {
    n_h = a.n;
    r.n_a = n_a;
    r.v_a = v_a;
    r.oldinstance = false;
    r.accept = true;
    // 写入日志，持久化
    l->logprop(n_h);
  }
  // 小于或等于见过的最大的proposal number，acceptor拒绝
  else
  {
    r.oldinstance = false;
    r.accept = false;
  }

  return paxos_protocol::OK;
}
```


### Phase2
```cpp
void
proposer::accept(unsigned instance, std::vector<std::string> &accepts,
        std::vector<std::string> nodes, std::string v)
{
  paxos_protocol::status ret;
  paxos_protocol::acceptarg a;
  a.instance = instance;
  a.n = my_n;
  a.v = v;

  bool r;

  for(auto it = nodes.begin(); it != nodes.end(); ++it)
  {
    handle h(*it);

    pthread_mutex_unlock(&pxs_mutex);
    rpcc *cl = h.safebind();
    if(cl)
    {
      ret = cl->call(paxos_protocol::acceptreq, me, a, r, rpcc::to(1000));
    }
    pthread_mutex_lock(&pxs_mutex);

    if(cl)
    {
      if(ret == paxos_protocol::OK && r)
      {
        accepts.push_back(*it);
      }
    }
  }
}
```

与Prepare一样，accept也是RPC调用
```cpp
paxos_protocol::status
acceptor::acceptreq(std::string src, paxos_protocol::acceptarg a, bool &r)
{
  ScopedLock ml(&pxs_mutex);
  if(a.n >= n_h)
  {
    n_a = a.n;
    v_a = a.v;
    r = true;
    // 写入日志，持久化
    l->logaccept(n_a, v_a);
  }
  else
  {
    r = false;
  }

  return paxos_protocol::OK;
}
```

## Phase3
```cpp
void
proposer::decide(unsigned instance, std::vector<std::string> accepts, 
	      std::string v)
{
  paxos_protocol::status ret;
  paxos_protocol::decidearg a;
  a.instance = instance;
  a.v = v;

  int r;

  for(auto it = accepts.begin(); it != accepts.end(); ++it)
  {
    handle h(*it);

    pthread_mutex_unlock(&pxs_mutex);
    rpcc *cl = h.safebind();
    if(cl)
    {
      ret = cl->call(paxos_protocol::decidereq, me, a, r, rpcc::to(1000));
    }
    pthread_mutex_lock(&pxs_mutex);
  }
}
```

```cpp
paxos_protocol::status
acceptor::decidereq(std::string src, paxos_protocol::decidearg a, int &r)
{
  ScopedLock ml(&pxs_mutex);
  tprintf("decidereq for accepted instance %d (my instance %d) v=%s\n", 
	 a.instance, instance_h, v_a.c_str());
  if (a.instance == instance_h + 1) {
    VERIFY(v_a == a.v);
    commit_wo(a.instance, v_a);
  } else if (a.instance <= instance_h) {
    // we are ahead ignore.
  } else {
    // we are behind
    VERIFY(0);
  }
  return paxos_protocol::OK;
}
```


## 测试
```shell
% ./rsm_tester.pl 0 1 2 3 4 5 6 7 
test0: start 3-process lock server
...
test1: start 3-process lock server, kill third server
...
test2: start 3-process lock server, kill first server
...
test3: start 3-process lock_server, kill a server, restart a server
...
test4: 3-process lock_server, kill third server, kill second server, restart third server, kill third server again, restart second server, re-restart third server, check logs
...
test5: 3-process lock_server, send signal 1 to first server, kill third server, restart third server, check logs
...
test6: 4-process lock_server, send signal 2 to first server, kill fourth server, restart fourth server, check logs
...
test7: 4-process lock_server, send signal 2 to first server, kill fourth server, kill other servers, restart other servers, restart fourth server, check logs Start lock_server on 28286
...
tests done OK
```