#pragma once
// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.

class WorkerBase {
public:
  virtual bool DoWork(void* p) = 0;
};

template <typename Derived>
class Worker : public WorkerBase {
  //typedef typename Derived::Context Ctx;
protected:
  virtual bool DoWork(void* p) override {
    return static_cast<Derived*>(this)->OnWork(reinterpret_cast<Derived::Context*>(p));
  }
};

class ThreadPool {
public:
  ThreadPool();
  ~ThreadPool();
  void EnterLoop(WorkerBase* job);
  bool PostJob(void* ctx);

private:
  HANDLE port_;
};