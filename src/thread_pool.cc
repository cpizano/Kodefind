// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.
#include "target_version_win.h"
#include "thread_pool.h" 

ThreadPool::ThreadPool() : port_(NULL) {
  port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 2);
}

ThreadPool::~ThreadPool() {
  ::CloseHandle(port_);
}

void ThreadPool::EnterLoop( WorkerBase* worker) {
  DWORD bytes;
  ULONG_PTR key;
  OVERLAPPED* ov;
  
  while(::GetQueuedCompletionStatus(port_, &bytes, &key, &ov, INFINITE)) {
    if (!worker->DoWork(ov))
      break;
  }

}

bool ThreadPool::PostJob(void* ctx) {
  return ::PostQueuedCompletionStatus(port_, 0, 1ull, reinterpret_cast<OVERLAPPED*>(ctx)) == TRUE; 
}
