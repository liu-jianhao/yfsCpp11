# MKDIR, UNLINK, and Locking
继续完善上一次实验的文件系统服务。

## Part1：MKDIR, UNLINK
这一部分与上一次实验完成的内容差不多，有了前面的理解，这一部分很简单。


## Part2：Locking
最简单的方法就是为整个文件系统提供单个锁，或是锁定整个目录，但这些都不好。

锁应该与每个内容相关联，使用文件或目录的`inum`作为锁的名称，利用之前在`lock_client`的实现，每个`yfs_client`都有一个`lock_client`。
```cpp
class yfs_client {
  extent_client *ec;
  lock_client *m_lc;
public:
  ...
```

而在CREATE等方法中如果直接使用`m_lc`的话会比较繁杂，可以自己实现一个锁类，采用RAII(资源获取即初始化)，
类似于标准库中的`lock_guard`。
```cpp
class LockGuard {
public:
  LockGuard(lock_client *lc, lock_protocol::lockid_t lid) : m_lc(lc), m_lid(lid)
  {
    m_lc->acquire(m_lid);
  }

  ~LockGuard()
  {
    m_lc->release(m_lid);
  }
private:
  lock_client *m_lc;
  lock_protocol::lockid_t m_lid;
};
```


## 测试
### Part1
```shell
$ ./test-lab-3-a.pl ./yfs1
mkdir ./yfs1/d14527
create x-0
delete x-0
create x-1
checkmtime x-1
checkdirmtime
...
...
Passed all tests!
```
### Part2
```shell
$ ./test-lab-3-b ./yfs1 ./yfs2
Create then read: OK
Unlink: OK
Append: OK
Readdir: OK
Many sequential creates: OK
Write 20000 bytes: OK
Concurrent creates: OK
Concurrent creates of the same file: OK
Concurrent create/delete: OK
Concurrent creates, same file, same server: OK
Concurrent writes to different parts of same file: OK
test-lab-3-b: Passed all tests.
$ ./test-lab-3-c ./yfs1 ./yfs2
Create/delete in separate directories: tests completed OK
```