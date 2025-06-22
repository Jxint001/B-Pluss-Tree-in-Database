#include "storage/page/page_guard.h"

#include "buffer/buffer_pool_manager.h"

namespace bustub
{

BasicPageGuard::BasicPageGuard(BasicPageGuard&& that) noexcept
{
  bpm_ = that.bpm_;
  page_ = that.page_;
  is_dirty_ = that.is_dirty_;
  that.page_ = nullptr;
}

void BasicPageGuard::Drop()
{
  if (page_ == nullptr)
  {
    return;
  }
     std::cout << "[DROP] Drop on page " << page_->GetPageId() << " is_dirty: " << is_dirty_ << std::endl;
  bpm_ -> UnpinPage(page_ -> GetPageId(), is_dirty_);
  page_ = nullptr;
}

auto BasicPageGuard::operator=(BasicPageGuard&& that) noexcept
     ->  BasicPageGuard&
{
  if (this == &that)
  {
    return that;
  }
  Drop();
  // drop the previous one
  bpm_ = that.bpm_;
  page_ = that.page_;
  is_dirty_ = that.is_dirty_;
  that.page_ = nullptr;
  return *this;
}

BasicPageGuard::~BasicPageGuard() { this -> Drop(); }

auto BasicPageGuard::UpgradeRead() -> ReadPageGuard { return {bpm_, page_}; }

auto BasicPageGuard::UpgradeWrite() -> WritePageGuard { return {bpm_, page_}; }

ReadPageGuard::ReadPageGuard(BufferPoolManager* bpm, Page* page)
{
  guard_ = BasicPageGuard(bpm, page);
  // std::cout << "[RLock▶] pid=" << page->GetPageId()
  //           << " thread=" << std::this_thread::get_id() << std::endl;
  guard_.page_ -> RLatch();
   //std::cout << "[RLock✓] pid=" << page->GetPageId() << std::endl;
}

ReadPageGuard::ReadPageGuard(ReadPageGuard&& that) noexcept
{
  //std::cout << "Read";
  guard_ = std::move(that.guard_);
  unlock_guard = false;
  that.unlock_guard = true;
  that.guard_.page_ = nullptr;
}

auto ReadPageGuard::operator=(ReadPageGuard&& that) noexcept -> ReadPageGuard&
{
  if (this == &that)
  {
    return that;
  }
  this -> Drop();
  guard_ = std::move(that.guard_);
  unlock_guard = false;
  that.unlock_guard = true;
  that.guard_.page_ = nullptr;
  return *this;
}

void ReadPageGuard::Drop()
{
  if(guard_.page_ != nullptr && unlock_guard == false)
  {
    unlock_guard = true;
    //This avoids repetitive drop
    guard_.page_ -> RUnlatch();
    std::cout << "Read ";
    guard_.Drop();
  }
}

ReadPageGuard::~ReadPageGuard() { this -> Drop(); }

WritePageGuard::WritePageGuard(BufferPoolManager* bpm, Page* page)
{
  guard_ = BasicPageGuard(bpm, page);
 // std::cout << "[WLock▶] pid=" << page->GetPageId()
            // << " thread=" << std::this_thread::get_id() << std::endl;
  guard_.page_ -> WLatch();
  //  std::cout << "[WLock✓] pid=" << page->GetPageId() << std::endl;
}

WritePageGuard::WritePageGuard(WritePageGuard&& that) noexcept
{
  //std::cout << "Write";
  guard_ = std::move(that.guard_);
  unlock_guard = false;
  that.unlock_guard = true;
  that.guard_.page_ = nullptr;
}

auto WritePageGuard::operator=(WritePageGuard&& that) noexcept
     ->  WritePageGuard&
{
  if (this == &that)
  {
    return that;
  }
  this -> Drop();
  guard_ = std::move(that.guard_);
  unlock_guard = false;
  that.unlock_guard = true;
  that.guard_.page_ = nullptr;
  return *this;
}

void WritePageGuard::Drop()
{
  if(guard_.page_ != nullptr && unlock_guard == false)
  {
    unlock_guard = true;
    //This avoids repetitive drop
    guard_.page_ -> WUnlatch();
    std::cout << "Write ";
    guard_.Drop();
  }
}

WritePageGuard::~WritePageGuard() { this -> Drop(); }

}  // namespace bustub
