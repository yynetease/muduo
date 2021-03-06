#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <set>
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer : boost::noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
  : loop_(loop),
    server_(loop, listenAddr, "ChatServer"),
    codec_(boost::bind(&ChatServer::onStringMessage, this, _1, _2, _3))
  {
    server_.setConnectionCallback(
        boost::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void setThreadNum(int numThreads)
  {
    server_.setThreadNum(numThreads);
  }

  void start()
  {
    server_.setThreadInitCallback(boost::bind(&ChatServer::threadInit, this, _1));
    server_.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
             << conn->peerAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected())
    {
      connections_->insert(conn);
    }
    else
    {
      connections_->erase(conn);
    }
  }

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
    EventLoop::Functor f = boost::bind(&ChatServer::distributeMessage, this, message);
    LOG_DEBUG;

    MutexLockGuard lock(mutex_);
    for (std::set<EventLoop*>::iterator it = loops_.begin();
        it != loops_.end();
        ++it)
    {
      (*it)->queueInLoop(f);
    }
    LOG_DEBUG;
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;

  void distributeMessage(const string& message)
  {
    LOG_DEBUG << "begin";
    for (ConnectionList::iterator it = connections_->begin();
        it != connections_->end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
    LOG_DEBUG << "end";
  }

  void threadInit(EventLoop* loop)
  {
    MutexLockGuard lock(mutex_);
    loops_.insert(loop);
    assert(connections_ == NULL);
    connections_ = new ConnectionList;
  }

  EventLoop* loop_;
  TcpServer server_;
  LengthHeaderCodec codec_;
  static __thread ConnectionList* connections_;

  MutexLock mutex_;
  std::set<EventLoop*> loops_;
};

 __thread ChatServer::ConnectionList* ChatServer::connections_;

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    if (argc > 2)
    {
      server.setThreadNum(atoi(argv[2]));
    }
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port [thread_num]\n", argv[0]);
  }
}


