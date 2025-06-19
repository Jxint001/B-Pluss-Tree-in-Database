Note that the implementation can be placed at a different place from definition.
## Tree structure
- Leaves store both index and data
- Internal vertices store only index


## Question
How to implement crabbing protocol?  
What is a page LSN?  
Why defining a header page can improve performance in concurrency?  
Do I need to unlock manually?


## src/include/storage/page/page.h
### Things in page
- data_
- isdirty
- pin_count
- page_id
- RWlock
### Rmk:
  - We have a read-write lock for every page.
  - `char* data_` points to **all the data** the page contains.
  - `page_id_` is the only identification of a `page`.
  - BPlusTreePage is the data in data_ in page (use reinterpret_cast).



## src/include/storage/page/b_plus_tree_page.h, internal_page.h, leaf_page.h
### Things in b plus tree page
- helper methods
- page_type
- size_ (current)
- max_size
### Things in internal page
- helper methods
- mapping array (key -> page_id)
### Things in leaf page
- helper methods
- mapping array (key -> record_id)
- next_page_id

### Rmk:
- The header page only holds the root (root_page_id) and it is **not** inherited from BPlusTreePage.



## src/include/storage/page/page_guard.hpp
### Rmk:
- AS() function gets the data in a page and reinterpret it to another type **based on the type you've given to the function.**
- ASMut() function is the mutable version of AS().
- UpgradeRead()/ UpgradeWrite() is useful if you want to create a page and immediately lock it.
- PageId() returns the page_id that current page_guard is holding.
- Member template use
- Drop() is equivalent to destruct manually.
- If needed, use move construction and move assignment. This will ensure correct management for Pin_Count and Lock.

As long as you destruct PageGuard, the corresponding page will be written to disk "automatically". (Drop -> write)  

## Fetch page
### Things needed (black box)
- auto FetchPageRead(page_id_t page_id) -> ReadPageGuard;
- auto FetchPageWrite(page_id_t page_id) -> WritePageGuard;
- auto NewPageGuarded(page_id_t* page_id, AccessType access_type = AccessType::Unknown) -> BasicPageGuard;

### Rmk
- NewPageGuarded function can new a page, allocate a page_id to it and assign the incoming page_id.
- ReadPageGuard and WritePageGuard will "automatically" get and release corresponding lock.

## BPlusTree class
- each instance owns its buffer pool manager bmp


## TO DO
src/include/storage/index/b_plus_tree.h  
src/storage/index/b_plus_tree.cpp  
Usual test files are in test\storage  

## Note
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size)
{
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  SetNextPageId(INVALID_PAGE_ID);
}