#include "storage/index/b_plus_tree.h"

#include <cassert>
#include <optional>
#include <sstream>
#include <string>

#include "buffer/lru_k_replacer.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"
#include "common/rid.h"
#include "storage/index/generic_key.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/hash_table_page_defs.h"
#include "storage/page/page_guard.h"
#include "type/type_id.h"

namespace bustub
{
void hello(){std::cout << "hello" << std::endl;}
template <typename KeyType, typename page_id_t, typename KeyComparator>
bool check_internal_page(BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>* page, const KeyComparator& cmp) {
  int size = page->GetSize();
  bool ok = true;
  for (int i = 1; i < size - 1; ++i) {
    if (!cmp(page->KeyAt(i), page->KeyAt(i + 1))) {ok = false;}
  }
  return ok;
}

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id,
                          BufferPoolManager* buffer_pool_manager,
                          const KeyComparator& comparator, int leaf_max_size,
                          int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id)
{
  WritePageGuard guard = bpm_ -> FetchPageWrite(header_page_id_);
  // In the original bpt, I fetch the header page
  // thus there's at least one page now
  auto root_header_page = guard.template AsMut<BPlusTreeHeaderPage>();
  // reinterprete the data of the page into "HeaderPage"
  root_header_page -> root_page_id_ = INVALID_PAGE_ID;
  // set the root_id to INVALID
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const  ->  bool
{
  ReadPageGuard guard = bpm_ -> FetchPageRead(header_page_id_);
  auto root_header_page = guard.template As<BPlusTreeHeaderPage>();
  bool is_empty = root_header_page -> root_page_id_ == INVALID_PAGE_ID;
  // Just check if the root_page_id is INVALID
  // usage to fetch a page:
  // fetch the page guard   ->   call the "As" function of the page guard
  // to reinterprete the data of the page as "BPlusTreePage"
  return is_empty;
}
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType& key,
                              std::vector<ValueType>* result, Transaction* txn)
     ->  bool
{
  ReadPageGuard cur_guard = this->bpm_->FetchPageRead(this->GetRootPageId());
  //std::cout << "fetched root" << std::endl;
  auto page = cur_guard.template As<BPlusTreePage>();
  
  /* Go to the corresponding leaf page. */
  while (!page->IsLeafPage()) {
    auto internal = reinterpret_cast<const BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
    int idx = BinaryFind(internal, key);
    page_id_t child_page_id = internal->ValueAt(idx);
    auto parent_guard = std::move(cur_guard);
    cur_guard = bpm_->FetchPageRead(child_page_id);
    //std::cout << "fetched " << child_page_id << std::endl;
    page = cur_guard.template As<BPlusTreePage>();
    parent_guard.Drop();
  }

  cur_guard.Drop();

  /* Find key in the leaf. */
  auto leaf = reinterpret_cast<const BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(page);
  int idx = BinaryFind(leaf, key);
  // std::cout << leaf->GetMaxSize() << " " << leaf->GetSize() << std::endl;
  // std::cout << "keys in leaf " << leaf->KeyAt(0) << " " << leaf->KeyAt(1) << std::endl;
  if (idx == -1 || comparator_(leaf->KeyAt(idx), key)) {//std::cout << "do not find key: " << key << std::endl; std::cout << idx << std::endl; 
  return false;}
  result->push_back(leaf->ValueAt(idx));
  return true;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BasicInsert(const KeyType &key, const ValueType &value, Transaction *txn, Context & ctx) -> bool {
  //std::cout << "BasicInsert" << std::endl;
  /* Add the root to write guard set. */
  std::deque<std::pair<page_id_t, int> > path;

  page_id_t cur_id = ctx.root_page_id_;
  std::cout << "cur_id: " << cur_id << std::endl;
  ctx.write_set_.push_back(bpm_->FetchPageWrite(cur_id));
    

  if (ctx.write_set_.back().template As<BPlusTreePage>()->IsLeafPage()) {path.push_back({cur_id, -1}); }
  
  //int temp_last_id = -1;
  while (true) {
    auto &wg = ctx.write_set_.back();
    auto page = wg.template As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      //std::cout << wg.PageId() << " " << ctx.root_page_id_ << std::endl;
      if (wg.PageId() != ctx.root_page_id_) {
      path.push_back({wg.PageId(), -1});
      }
      break; }

    auto internal = reinterpret_cast<const InternalPage*>(page);
    int idx = BinaryFind(internal, key);
    //if (cur_id == ctx.root_page_id_) { 
      path.push_back({wg.PageId(), idx});
    //}
    page_id_t child_page_id = internal->ValueAt(idx);

    WritePageGuard child_guard = bpm_->FetchPageWrite(child_page_id);


   auto child_page = child_guard.template As<BPlusTreePage>();
   bool safe = child_page->GetSize() < child_page->GetMaxSize();

    if (safe) {
      for (size_t i = 0; i + 1 < ctx.write_set_.size(); i++) {
        ctx.write_set_[i].Drop();
      }
      ctx.write_set_.erase(ctx.write_set_.begin(), ctx.write_set_.end() - 1);
      path.erase(path.begin(), path.end() - 1);
    }

    ctx.write_set_.push_back(std::move(child_guard));
    //path.push_back({child_page_id, temp_last_id});
  //  temp_last_id = idx;
    cur_id = child_page_id;
  }

  if (ctx.write_set_.size() != path.size()) {
    std::cout << "path.size(): " << path.size() << " write_set_.size(): " << ctx.write_set_.size() << std::endl;
  }
   assert(ctx.write_set_.size() == path.size());
  /* Check whether path and ctx.write_set_ are in sync. */
  int temp_i = 0;
  for (auto& wg:ctx.write_set_) {
    assert(wg.PageId() == path[temp_i].first);
    std::cout << "pageid: " << path[temp_i].first << " child_idx: " << path[temp_i].second << std::endl;
    temp_i++;
  }

  /* Get leaf. */
  //int get_wg = ctx.write_set_.size() - 1;
  WritePageGuard leaf_guard = std::move(ctx.write_set_.back());  ctx.write_set_.pop_back();
  path.pop_back();

  auto leaf = leaf_guard.template AsMut<LeafPage>();
  int idx = BinaryFind(leaf, key);
  int pos = idx + 1;
  if (pos > 0 && comparator_(leaf->KeyAt(pos - 1), key) == 0) {
    leaf_guard.Drop();
    // ctx.header_page_->Drop();
    return false;
  }

  /* Split leaf. */
  page_id_t right_leaf_id;
  auto new_leaf_gurad = std::move(bpm_->NewPageGuarded(&right_leaf_id)).UpgradeWrite();
//  std::cout << "new page: " << right_leaf_id << std::endl;

  auto right_page = new_leaf_gurad.template AsMut<LeafPage>();
  right_page->Init(leaf_max_size_);

  /* Set the new leaf page as the right page. */
  int up = (leaf->GetSize() + 1) >> 1;  /* Move from up to the last. */
  int maxsize = leaf->GetSize();
  right_page->SetSize(maxsize - up);
  for (int i = up; i < maxsize; ++i) {
    right_page->SetAt(i - up, leaf->KeyAt(i), leaf->ValueAt(i));
  }
  /* Change (left) leaf accordingly. */
  page_id_t left_id = leaf_guard.PageId();
  leaf->SetSize(up);

  /* Insert in (left or right) leaf. */
  if (idx + 1 <= up) {
    /* Insert in left leaf. */
    leaf->IncreaseSize(1);
    for (int i = up; i > idx + 1; --i) {
      leaf->SetAt(i, leaf->KeyAt(i - 1), leaf->ValueAt(i - 1));
    }
    leaf->SetAt(idx + 1, key, value);
  } else {
    /* Insert in right leaf. */
    right_page->IncreaseSize(1);
    for (int i = right_page->GetSize() - 1; i > idx - up + 1; --i) {
      right_page->SetAt(i, right_page->KeyAt(i - 1), right_page->ValueAt(i - 1));
    }
    right_page->SetAt(idx - up + 1, key, value);
  }
  /* Link leaves. */
  page_id_t old_left_next = leaf->GetNextPageId();
  leaf->SetNextPageId(right_leaf_id);
  right_page->SetNextPageId(old_left_next);

  /* Insert upward. */
  KeyType right_key = right_page->KeyAt(0);
  //left_key = leaf->KeyAt(0);
  bool splitup = true;

  bpm_->FlushPage(right_leaf_id);
  new_leaf_gurad.Drop();
  leaf_guard.Drop();

  while (splitup && !path.empty()) {
     //std::cout << "right_key: " << right_key << std::endl;
    auto [id, child_idx] = path.back();  path.pop_back();
    /* Insert one more key to the parent. */
    WritePageGuard parent_guard = std::move(ctx.write_set_.back());  ctx.write_set_.pop_back();
    //WritePageGuard& parent_guard = ctx.write_set_[get_wg--];

    auto modify = parent_guard.template AsMut<BPlusTreePage>();
    //assert(modify->IsLeafPage() == 0);
    auto modify_parent = reinterpret_cast<InternalPage*>(modify);
    
    if (modify_parent->GetSize() < modify_parent->GetMaxSize()) {
      modify_parent->IncreaseSize(1);
      int cursize = modify_parent->GetSize();
      std::cout << "child_idx + 1: " << child_idx + 1 << std::endl;
      for (int i = cursize - 1; i > child_idx + 1; --i) {
        modify_parent->SetKeyAt(i, modify_parent->KeyAt(i - 1));
        modify_parent->SetValueAt(i, modify_parent->ValueAt(i - 1));
        assert(i != 0 || modify_parent->ValueAt(i - 1) != 0);
      }

      modify_parent->SetKeyAt(child_idx + 1, right_key);
      modify_parent->SetValueAt(child_idx + 1, right_leaf_id);
      assert(child_idx + 1 != 0 || right_leaf_id != 0);
      parent_guard.Drop();
      splitup = false;
      break;
    }

    /* Parent also full. */
    /* Split parent. */
    page_id_t tmp_id;
    auto new_parent_guard = std::move(bpm_->NewPageGuarded(&tmp_id)).UpgradeWrite();
    std::vector<std::pair<KeyType, page_id_t> > temp;

    /* Sort. */
    for (int i = 0, n = modify_parent->GetSize(); i < n; ++i) {
        temp.push_back({i == 0 ? KeyType{} : modify_parent->KeyAt(i), modify_parent->ValueAt(i)});
    }
    temp.push_back({right_key, right_leaf_id});
    std::sort(temp.begin(), temp.end(), [&](const auto& a, const auto& b) { return comparator_(a.first, b.first) < 0; });

    auto parent_right_page = new_parent_guard.template AsMut<InternalPage>();
    parent_right_page->Init(internal_max_size_);

    int total_ptrs = temp.size();
    int left_ptrs = (total_ptrs + 1) >> 1;
    int right_ptrs = total_ptrs - left_ptrs;

    KeyType tmp_right_key = temp[left_ptrs].first;

    parent_right_page->SetSize(right_ptrs);
    for (int i = 0; i < right_ptrs; ++i) {
      parent_right_page->SetKeyAt(i, temp[i + left_ptrs].first);
      parent_right_page->SetValueAt(i, temp[i + left_ptrs].second);
      assert(i != 0 || temp[i + left_ptrs].second != 0);
    }
      
    /* Change modify_parent to parent_left_page. */
    modify_parent->SetSize(left_ptrs);
    for (int i = 0; i < left_ptrs; ++i) {
      modify_parent->SetKeyAt(i, temp[i].first);
      modify_parent->SetValueAt(i, temp[i].second);
      assert(i != 0 || temp[i + left_ptrs].second != 0);
    }

    /* Update what to insert upward. */
    right_key = tmp_right_key;

    right_leaf_id = tmp_id;
   // left_key = modify_parent->KeyAt(0);
    left_id = parent_guard.PageId();

    // ok = check_internal_page<KeyType, page_id_t, KeyComparator>(modify_parent, comparator_);
    // assert(ok);
    // ok = check_internal_page<KeyType, page_id_t, KeyComparator>(parent_right_page, comparator_);
    // assert(ok);

  //     std::cout << "left start print" << std::endl;
  // for (int i = 0; i < modify_parent->GetSize(); ++i) {
  //   std::cout <<modify_parent->ValueAt(i) << " ";
  // }
  // std::cout << std::endl<< "left end print" << std::endl;
  //  std::cout << "right start print" << std::endl;
  // for (int i = 0; i < parent_right_page->GetSize(); ++i) {
  //   std::cout << parent_right_page->ValueAt(i) << " ";
  // }
  // std::cout << std::endl<< "left end print" << std::endl;
  // std::cout << "left parent page id: " << left_id << " right parent page id: " << right_leaf_id << std::endl;
  // std::cout << std::endl<<"end print" << std::endl;

    bpm_->FlushPage(tmp_id);
    new_parent_guard.Drop();
    parent_guard.Drop();
  }

  /* If root is full, increase tree height. */
  if (splitup) {
    // std::cout << "split root" << std::endl;
    //  std::cout << "right_key: " << right_key << std::endl;
    page_id_t new_root_id;
    auto new_root_guard = std::move(bpm_->NewPageGuarded(&new_root_id)).UpgradeWrite();
//    std::cout << "new page: " << new_root_id << std::endl;

    auto newpage = new_root_guard.template AsMut<BPlusTreePage>();
    auto new_root_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(newpage);
    new_root_page->Init(internal_max_size_);
    
    new_root_page->SetSize(2);
    //new_root_page->SetKeyAt(0, left_key);
    new_root_page->SetValueAt(0, left_id);
    assert(left_id != 0);
    new_root_page->SetKeyAt(1, right_key);
    new_root_page->SetValueAt(1, right_leaf_id);
    assert(right_leaf_id != 0);

    bpm_->FlushPage(new_root_id);
    new_root_guard.Drop();
    {
      auto header_page = bpm_->FetchPageWrite(header_page_id_);
      header_page.template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = new_root_id;
    }
  }

  for (auto& wg: ctx.write_set_)  wg.Drop();
  ctx.write_set_.clear();
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType& key, const ValueType& value,
                            Transaction* txn)  ->  bool
{
  //std::cout << "Insert" << std::endl;
  Context ctx;
  auto header_page = bpm_->FetchPageRead(header_page_id_);
  ctx.root_page_id_ = header_page.template As<BPlusTreeHeaderPage>()->root_page_id_;
  header_page.Drop();
  //std::cout << "root: " << ctx.root_page_id_ << std::endl;

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    /* Create a leaf as root. */
    page_id_t leaf_id;
    auto page = std::move(bpm_->NewPageGuarded(&leaf_id)).UpgradeWrite();
  // std::cout << "new page: " << leaf_id << std::endl;
    auto leaf_page = page.template AsMut<LeafPage>();

    /* Necessary initialization. */
    leaf_page->Init(leaf_max_size_);
    leaf_page->SetSize(1);

    /* Insert the pair. */
    leaf_page->SetAt(0, key, value);
    bpm_->FlushPage(leaf_id);

    /* Change the root page id to its real id. */
    {
      auto header_page = bpm_->FetchPageWrite(header_page_id_);
      header_page.template AsMut<BPlusTreeHeaderPage>()->root_page_id_ = leaf_id;
      ctx.root_page_id_ = leaf_id;
      header_page.Drop();
    }
    ctx.header_page_->Drop();
    return true;
  }

  /* Add the root to read guard set. */
  page_id_t cur_id = ctx.root_page_id_;
  ctx.read_set_.push_back(bpm_->FetchPageRead(cur_id));

  /* Go to the corresponding leaf page. */
  while (true) {
    auto &rg = ctx.read_set_.back();
    auto page = rg.template As<BPlusTreePage>();
    if (page->IsLeafPage()) {break; }

    auto internal = reinterpret_cast<const InternalPage*>(page);
    int idx = BinaryFind(internal, key);
    page_id_t child_page_id = internal->ValueAt(idx);
    
    if (child_page_id == 0) {
      std::cout << "idx: " << idx << std::endl;
      std::cout << "key: " << key << std::endl;
      std::cout << "Size: " << internal->GetSize() << " MaxSize: " << internal->GetMaxSize() << std::endl;
      for (int i = 0; i < internal->GetSize(); ++i) {
        std::cout << "key: " << internal->KeyAt(i) << "  page: " << internal->ValueAt(i) << std::endl;
      }
    }
    assert(child_page_id != 0);

    ReadPageGuard child_page = bpm_->FetchPageRead(child_page_id);
    ctx.read_set_.push_back(std::move(child_page));
  
    /* Release parent's read guard -- optimistic. */
    auto tmp = std::move(ctx.read_set_[ctx.read_set_.size() - 2]);
    tmp.Drop();
    ctx.read_set_.erase(ctx.read_set_.end() - 2);
  }

  /* Try to insert in leaf. */
  auto &leaf_read = ctx.read_set_.back();
  auto leaf_page = reinterpret_cast<const LeafPage *>(leaf_read.template As<BPlusTreePage>());
  int idx = BinaryFind(leaf_page, key);
  /* If key already exists, do nothing. */
  if (idx != -1 && !comparator_(leaf_page->KeyAt(idx), key)) {
    for (auto &rg : ctx.read_set_) rg.Drop();
    return false; 
  }
  
  /* No split. */
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    page_id_t convert_to_wg = leaf_read.PageId();
    leaf_read.Drop();
    
    WritePageGuard leaf_write = bpm_->FetchPageWrite(convert_to_wg);

    auto leaf = leaf_write.template AsMut<LeafPage>();
    leaf->IncreaseSize(1);
    for (int i = leaf->GetSize() - 1; i >= idx + 2; --i) {
      leaf->SetAt(i, leaf->KeyAt(i - 1), leaf->ValueAt(i - 1));
    }

    /* Insert at idx + 1. */
    leaf->SetAt(idx + 1, key, value);
    leaf_write.Drop();
    //ctx.header_page_->Drop();
    return true;
  }

  /* Have to split from bottom to top. */
  for (auto &rg : ctx.read_set_) rg.Drop();
  ctx.read_set_.clear();
  //ctx.header_page_->Drop();

  return BasicInsert(key, value, txn, ctx);
}


/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType& key, Transaction* txn)
{
  //Your code here
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/


INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BinaryFind(const LeafPage* leaf_page, const KeyType& key)
     ->  int
{
  int l = 0;
  int r = leaf_page -> GetSize() - 1;
  while (l < r)
  {
    int mid = (l + r + 1) >> 1;
    if (comparator_(leaf_page -> KeyAt(mid), key) != 1)
    {
      l = mid;
    }
    else
    {
      r = mid - 1;
    }
  }

  if (r >= 0 && comparator_(leaf_page -> KeyAt(r), key) == 1)
  {
    r = -1;
  }

  return r;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::BinaryFind(const InternalPage* internal_page,
                                const KeyType& key)  ->  int
{
  //std::cout << "binaryfinding internal " << key << std::endl;
  int l = 1;
  int r = internal_page -> GetSize() - 1;
  while (l < r)
  {
    int mid = (l + r + 1) >> 1;
    if (comparator_(internal_page -> KeyAt(mid), key) != 1)
    {
      l = mid;
    }
    else
    {
      r = mid - 1;
    }
  }

  if (r == -1 || comparator_(internal_page -> KeyAt(r), key) == 1)
  {
    r = 0;
  }

  return r;
}

//if (comparator_(key, internal_page->KeyAt(1)))  return 0;

/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin()  ->  INDEXITERATOR_TYPE
//Just go left forever
{
  ReadPageGuard head_guard = bpm_ -> FetchPageRead(header_page_id_);
  if (head_guard.template As<BPlusTreeHeaderPage>() -> root_page_id_ == INVALID_PAGE_ID)
  {
    return End();
  }
  ReadPageGuard guard = bpm_ -> FetchPageRead(head_guard.As<BPlusTreeHeaderPage>() -> root_page_id_);
  head_guard.Drop();

  auto tmp_page = guard.template As<BPlusTreePage>();
  while (!tmp_page -> IsLeafPage())
  {
    int slot_num = 0;
    guard = bpm_ -> FetchPageRead(reinterpret_cast<const InternalPage*>(tmp_page) -> ValueAt(slot_num));
    tmp_page = guard.template As<BPlusTreePage>();
  }
  int slot_num = 0;
  if (slot_num != -1)
  {
    return INDEXITERATOR_TYPE(bpm_, guard.PageId(), 0);
  }
  return End();
}


/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType& key)  ->  INDEXITERATOR_TYPE
{
  ReadPageGuard head_guard = bpm_ -> FetchPageRead(header_page_id_);

  if (head_guard.template As<BPlusTreeHeaderPage>() -> root_page_id_ == INVALID_PAGE_ID)
  {
    return End();
  }
  ReadPageGuard guard = bpm_ -> FetchPageRead(head_guard.As<BPlusTreeHeaderPage>() -> root_page_id_);
  head_guard.Drop();
  auto tmp_page = guard.template As<BPlusTreePage>();
  while (!tmp_page -> IsLeafPage())
  {
    auto internal = reinterpret_cast<const InternalPage*>(tmp_page);
    int slot_num = BinaryFind(internal, key);
    if (slot_num == -1)
    {
      return End();
    }
    guard = bpm_ -> FetchPageRead(reinterpret_cast<const InternalPage*>(tmp_page) -> ValueAt(slot_num));
    tmp_page = guard.template As<BPlusTreePage>();
  }
  auto* leaf_page = reinterpret_cast<const LeafPage*>(tmp_page);

  int slot_num = BinaryFind(leaf_page, key);
  if (slot_num != -1)
  {
    return INDEXITERATOR_TYPE(bpm_, guard.PageId(), slot_num);
  }
  return End();
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End()  ->  INDEXITERATOR_TYPE
{
  return INDEXITERATOR_TYPE(bpm_, -1, -1);
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId()  ->  page_id_t
{
  ReadPageGuard guard = bpm_ -> FetchPageRead(header_page_id_);
  auto root_header_page = guard.template As<BPlusTreeHeaderPage>();
  page_id_t root_page_id = root_header_page -> root_page_id_;
  return root_page_id;
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string& file_name,
                                    Transaction* txn)
{
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key)
  {
    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, txn);
  }
}
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string& file_name,
                                    Transaction* txn)
{
  int64_t key;
  std::ifstream input(file_name);
  while (input >> key)
  {
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, txn);
  }
}

/*
 * This method is used for test only
 * Read data from file and insert/remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::BatchOpsFromFile(const std::string& file_name,
                                      Transaction* txn)
{
  int64_t key;
  char instruction;
  std::ifstream input(file_name);
  while (input)
  {
    input >> instruction >> key;
    RID rid(key);
    KeyType index_key;
    index_key.SetFromInteger(key);
    switch (instruction)
    {
      case 'i':
        Insert(index_key, rid, txn);
        break;
      case 'd':
        Remove(index_key, txn);
        break;
      default:
        break;
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager* bpm)
{
  auto root_page_id = GetRootPageId();
  auto guard = bpm -> FetchPageBasic(root_page_id);
  PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PrintTree(page_id_t page_id, const BPlusTreePage* page)
{
  if (page -> IsLeafPage())
  {
    auto* leaf = reinterpret_cast<const LeafPage*>(page);
    std::cout << "Leaf Page: " << page_id << "\tNext: " << leaf -> GetNextPageId() << std::endl;

    // Print the contents of the leaf page.
    std::cout << "Contents: ";
    for (int i = 0; i < leaf -> GetSize(); i++)
    {
      std::cout << leaf -> KeyAt(i);
      if ((i + 1) < leaf -> GetSize())
      {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
  }
  else
  {
    auto* internal = reinterpret_cast<const InternalPage*>(page);
    std::cout << "Internal Page: " << page_id << std::endl;

    // Print the contents of the internal page.
    std::cout << "Contents: ";
    for (int i = 0; i < internal -> GetSize(); i++)
    {
      std::cout << internal -> KeyAt(i) << ": " << internal -> ValueAt(i);
      if ((i + 1) < internal -> GetSize())
      {
        std::cout << ", ";
      }
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal -> GetSize(); i++)
    {
      auto guard = bpm_ -> FetchPageBasic(internal -> ValueAt(i));
      PrintTree(guard.PageId(), guard.template As<BPlusTreePage>());
    }
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager* bpm, const std::string& outf)
{
  if (IsEmpty())
  {
    LOG_WARN("Drawing an empty tree");
    return;
  }

  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  auto root_page_id = GetRootPageId();
  auto guard = bpm -> FetchPageBasic(root_page_id);
  ToGraph(guard.PageId(), guard.template As<BPlusTreePage>(), out);
  out << "}" << std::endl;
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(page_id_t page_id, const BPlusTreePage* page,
                             std::ofstream& out)
{
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page -> IsLeafPage())
  {
    auto* leaf = reinterpret_cast<const LeafPage*>(page);
    // Print node name
    out << leaf_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf -> GetSize() << "\">P=" << page_id
        << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf -> GetSize() << "\">"
        << "max_size=" << leaf -> GetMaxSize()
        << ",min_size=" << leaf -> GetMinSize() << ",size=" << leaf -> GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf -> GetSize(); i++)
    {
      out << "<TD>" << leaf -> KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf -> GetNextPageId() != INVALID_PAGE_ID)
    {
      out << leaf_prefix << page_id << "   ->   " << leaf_prefix
          << leaf -> GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << page_id << " " << leaf_prefix
          << leaf -> GetNextPageId() << "};\n";
    }
  }
  else
  {
    auto* inner = reinterpret_cast<const InternalPage*>(page);
    // Print node name
    out << internal_prefix << page_id;
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" "
           "CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner -> GetSize() << "\">P=" << page_id
        << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner -> GetSize() << "\">"
        << "max_size=" << inner -> GetMaxSize()
        << ",min_size=" << inner -> GetMinSize() << ",size=" << inner -> GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner -> GetSize(); i++)
    {
      out << "<TD PORT=\"p" << inner -> ValueAt(i) << "\">";
      // if (i > 0) {
      out << inner -> KeyAt(i) << "  " << inner -> ValueAt(i);
      // } else {
      // out << inner  ->  ValueAt(0);
      // }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print leaves
    for (int i = 0; i < inner -> GetSize(); i++)
    {
      auto child_guard = bpm_ -> FetchPageBasic(inner -> ValueAt(i));
      auto child_page = child_guard.template As<BPlusTreePage>();
      ToGraph(child_guard.PageId(), child_page, out);
      if (i > 0)
      {
        auto sibling_guard = bpm_ -> FetchPageBasic(inner -> ValueAt(i - 1));
        auto sibling_page = sibling_guard.template As<BPlusTreePage>();
        if (!sibling_page -> IsLeafPage() && !child_page -> IsLeafPage())
        {
          out << "{rank=same " << internal_prefix << sibling_guard.PageId()
              << " " << internal_prefix << child_guard.PageId() << "};\n";
        }
      }
      out << internal_prefix << page_id << ":p" << child_guard.PageId()
          << "   ->   ";
      if (child_page -> IsLeafPage())
      {
        out << leaf_prefix << child_guard.PageId() << ";\n";
      }
      else
      {
        out << internal_prefix << child_guard.PageId() << ";\n";
      }
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::DrawBPlusTree()  ->  std::string
{
  if (IsEmpty())
  {
    return "()";
  }

  PrintableBPlusTree p_root = ToPrintableBPlusTree(GetRootPageId());
  std::ostringstream out_buf;
  p_root.Print(out_buf);

  return out_buf.str();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::ToPrintableBPlusTree(page_id_t root_id)
     ->  PrintableBPlusTree
{
  auto root_page_guard = bpm_ -> FetchPageBasic(root_id);
  auto root_page = root_page_guard.template As<BPlusTreePage>();
  PrintableBPlusTree proot;

  if (root_page -> IsLeafPage())
  {
    auto leaf_page = root_page_guard.template As<LeafPage>();
    proot.keys_ = leaf_page -> ToString();
    proot.size_ = proot.keys_.size() + 4;  // 4 more spaces for indent

    return proot;
  }

  // draw internal page
  auto internal_page = root_page_guard.template As<InternalPage>();
  proot.keys_ = internal_page -> ToString();
  proot.size_ = 0;
  for (int i = 0; i < internal_page -> GetSize(); i++)
  {
    page_id_t child_id = internal_page -> ValueAt(i);
    PrintableBPlusTree child_node = ToPrintableBPlusTree(child_id);
    proot.size_ += child_node.size_;
    proot.children_.push_back(child_node);
  }

  return proot;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub