//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// rwmutex.h
//
// Identification: src/include/common/rwlatch.h
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>  // NOLINT
#include <shared_mutex>

#include "common/macros.h"
#include <iostream>
#include <thread>

namespace bustub
{

/**
 * Reader-Writer latch backed by std::mutex.
 */
class ReaderWriterLatch
{
  public:
  /**
   * Acquire a write latch.
   */
  void WLock() { 
      //std::cout<<"[WLock] page_latch="<<this<<" thread="<<std::this_thread::get_id()<<std::endl;
    mutex_.lock(); 
      //std::cout<<"[WLock✓] page_latch="<<this<<std::endl;
  }

  /**
   * Release a write latch.
   */
  void WUnlock() { 
   // std::cout<<"[WLock] page_latch="<<this<<" thread="<<std::this_thread::get_id()<<std::endl;
    mutex_.unlock(); 
   // std::cout<<"[WLock×] page_latch="<<this<<"UnLatched"<<std::endl;
  }

  /**
   * Acquire a read latch.
   */
  void RLock() { 
    //std::cout<<"[RLock] page_latch="<<this<<" thread="<<std::this_thread::get_id()<<std::endl;
    mutex_.lock_shared(); 
    //std::cout<<"[RLock✓] page_latch="<<this<<std::endl;
  }

  /**
   * Release a read latch.
   */
  void RUnlock() { 
    //std::cout<<"[RLock] page_latch="<<this<<" thread="<<std::this_thread::get_id()<<std::endl;
    mutex_.unlock_shared(); 
     //std::cout<<"[RLock×] page_latch="<<this<<"UnLatched"<<std::endl;
  }

  private:
  std::shared_mutex mutex_;
};

}  // namespace bustub
