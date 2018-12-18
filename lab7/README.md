# Lab7：Replicated State Machine


复制状态机实现了一个主服务器和几个备份。
主服务器接收请求，根据接收的顺序给每一个请求分配一个`view stamp`(一个`vid`和一个顺序的数字)，并转发到所有备份。

一个备份按照`view stamp`的顺序执行请求并返回`OK`给主服务器。

主服务器在收到所有备份的`OK`之后执行请求，并发送回复信息给客户。

`config`模块将告诉`RSM`一个新的`view`。如果前一个`view`的主服务器是新`view`的成员，那么它依旧是主服务器。否则，前一个`view`中的最小数字节点将会是新的主服务器。其他情况下，主服务器将会是前一个`view`中的成员。

配置模块为`RSM`构建顺序的`views`，`RSM`确保它们总会只有一个主服务器，这个主服务器是前一个`view`的成员。

当一个新节点开始，`recovery thread`会让它加入到`RSM`中。它将收集主服务器的内部RSM状态；主服务器让配置模块添加新的节点并且返回内部RSM状态。因为只有一个主服务器，所以所有添加都会很有秩序。

`recovery thread`也会运行在一个`view`变化(例如一个节点失败)。一种失败的情况是：一些备份已经执行了请求，但是主服务器没有，但是执行的结果对于客户来说是不可见的。


当`RSM`在发生`view`变化时(一个节点失败，一个新的`view`形成，但是同步没有完成)，这也是一种失败情况。
有两个变量来追踪这些情况：
+ `inviewchange`：一个节点失败了，`RSM`正在进行`view`变化
+ `insync`：节点正在同步它的状态

一个请求不应该被阻塞。

+ RSM module：管理复制
+ config module：管理`view`
+ Paxos module：管理paxos来达成一致
每一个module都有各自的线程和内部的锁。

当`acceptor`在某一轮中批准了一个`value`，一个线程会`invoke`通知更高层这个`value`。

## 问题
1. 什么是`view`?
其实在上一次的实验报告中已经了解过了。一个`view`就是当前服务器集群的状态。
例如有a、b、c三个服务器，此时的`view`就是`{a b c}`，如果某个时刻c宕机了，那么`view`就需要改变，变为`{a b}`

2. `config`模块是怎么发现`view`需要改变？
通过心跳。


## setp one：重新设计锁缓存服务器和客户端
与lab4不同的是，锁缓存服务器和客户端都有较大的不同，特别是在lab4中，当`lock_server_cache`发送`revoke`RPC给锁持有者，这是是要等待回复的，
但在lab7中，是不需要这样的，是通过后台线程实现的。

在新的锁缓存服务器和客户端要解决两个问题：
1. 要避免死锁。死锁可能是由RSM层当要调用`acquire、release`时持有`invoke_mutex`。
  + 解决办法：避免在RPC handler中调用RPC
  + 在`lock_client_cache_rsm`中有`releaser`后台线程
  + 在`lock_server_cache_rsm`中有`retryer`和`revoker`两个后台线程
  + 在lab4中，当`lock_server_cache`发送`revoke`RPC给锁持有者，这是是要等待回复的，
但在lab7中，是不需要这样的，是通过后台线程实现的。

2. 锁缓存客户端应该可以处理主锁服务器failed的情况。

在知道了上面的问题后，我们就可以动手修改lab4写的代码了。

需要修改的地方：
1. `lock_client_cache_rsm`
  1. `releaser`后台线程：从队列里取出请求信息，调用RPC
  2. `revoke_handler`RPC handler：往队列里添加请求信息
  3. ...
2. `lock_server_cache_rsm`
  1. `retryer`和`revoker`后台线程：从队列里取出请求信息，调用RPC
  2. `acquire`和`release`：都需要一些修改，不再需要RPC call
  3. ...



## step two：不考虑失败的RSM
流程：
1. RSM客户端发送`invoke`RPC请求给主服务器
2. 主服务器分配传来的`invoke`RPC按`view stamp`的顺序给从服务器。
3. 从服务器执行请求，返回OK给主服务器
4. 主服务器执行请求，返回OK给客户


+ `rsm::client_invoke()`:
  + 给客户调用的，发送RSM请求给主服务器
  + 如果RSM副本正在`view change`返回`BUSY`
  + 如果RSM副本不是主服务器，返回`NOTPRIMARY`
  + 如果是主服务器，按照上面的流程执行

+ `rsm::invoke`：
  + 给主服务器调用的，发送给从服务器

+ `rsm::inviewchange`：


## step three：考虑副本失败和实现状态转移
节点失败需要`recovery`，然后重新加入，因此需要数据的持久化和恢复数据。

流程：
1. Paxos 一致性
2. 下一个`view`
3. `commit_change` -> 新的`view`形成 -> `inviewchange = true`
4. 该节点还需要恢复RSM状态，在继续执行RSM请求前要`rsm::recovery`
5. `recovery`结束 -> `inviewchange = false`


## step four：考虑主服务器失败
`rsm_client::primary_failure`


## step five：处理更复杂的失败
在`lock_client_cache_rsm`中的`acquire`中的需要重试的情况要添加超时，
如果没有设置超时，客户就会被卡死，在测试12时会卡死。

如果之前写的都正确，第五步基本不用改什么代码。

现在可以通过所有测试了。