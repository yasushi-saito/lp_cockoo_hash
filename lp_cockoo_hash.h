#pragma once

#include <array>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

#define LP_COCKOO_HASH_DEBUG 1

#ifdef LP_COCKOO_HASH_DEBUG
#include <iostream>
#endif  // LP_COCKOO_HASH_DEBUG

// Lehman-Panigrahy hash table.
//
// 3.5-way Cockoo Hashing for the price of 2-and-a-bit.  Eric Lehman and Rita
// Panigrahy, European Symp. on Algorithms, 2009.
//
// https://pdfs.semanticscholar.org/aa7f/47954647604107fd5e67fa8162c7a785de71.pdf
//
// It also incorporates some of the ideas presented in the following paper:
//
// Algorithmic Improvements for Fast Concurrent Cuckoo Hashing, Xiaozhou Li,
// David G. Andersen, Michael Kaminsky, Michael J. Freedman, Eurosys 14.
//
// https://www.cs.princeton.edu/~mfreed/docs/cuckoo-eurosys14.pdf
//
// Opts should be a struct of the following form:
//
// struct Opts {
//   // NumHashes is the number of hash functions to use. Typically 2.
//   static constexpr int NumHashes = 2;
//   // BucketWidth is the number of elements in one bucket. Typically 2 to 4.
//   static constexpr int BucketWidth = 2;
//
//   // Alloc is called to allocate an default-initialized array of "V"
//   Value* Alloc(int n) { return new Value[n](); }
//   // Free is called to free the array allocated by Alloc(n).
//   void Free(Value* array, int n) { delete[] array; }
//
//   // Hash computes the Nth hash function (0 <= N < NumHashes).
//   // This hash must of good quality - e.g., farmhash or seahash.
//   size_t Hash(int n, Key k) const { return k + hash_index; }
//   size_t Hash(int n, const Value& v) { return v.key + hash_index; }
//
//   // Init is called when "v" is about to store key for Nth hash
//   (0<=N<NumHashes).
//   // hash is passed as perf optimization. It's always equal to Hash(n, k).
//   //
//   // Invariant: After the Clear call, Empty(*v) must return false.
//   void Init(int n, size_t hash, Key k, Value* v) { v->key = k; }
//
//   // Equals should check if "v" has key "k". "hash" is a performance hint.
//   bool Equals(size_t hash, Key k, const Value& v) const { return k == v.key;
//   }
//   // Empty checks if "v" stores a valid value. The default-constructed value
//   must return false. bool Empty(const Value& v) const { return v.key ==
//   kEmpty; }
//   // Clear is called when "v" no longer stores a value.
//   //
//   // Invariant: After the Clear call, Empty(*v) must return true.
//   bool Clear(Value* v) const { return v->key = kEmpty; }
// };
//
template <typename K, typename V, typename Opts>
class LpCockooHash {
 public:
  static constexpr int NumHashes = Opts::NumHashes;
  static constexpr int BucketWidth = Opts::BucketWidth;
  static constexpr size_t kNoParent = std::numeric_limits<size_t>::max();
  static constexpr double LoadFactor = 0.9;
  using HashValue = size_t;  // Return value of hash functions.

  struct iterator {
    const LpCockooHash* parent;
    int table;
    size_t index;

    bool operator==(const iterator i2) const {
      return i2.table == table && i2.index == index;
    }
    bool operator!=(const iterator i2) const { return !(*this == i2); }
    V& operator*() { return parent->tables_[table][index]; }
    V* operator->() { return &parent->tables_[table][index]; }
  };

  // "elems" is the max number of elems that will be stored in the table.  The
  // hashtable hehavior is undefined if you try to store more that "elems"
  // elements.
  //
  // TODO(saito) Implement dynamic resizing.
  LpCockooHash(size_t elems, Opts opts = Opts()) : opts_(std::move(opts)) {
    buckets_per_table_ = (elems / LoadFactor - 1) / NumHashes + 1;
    for (int i = 0; i < tables_.size(); i++) {
      tables_[i] = opts_.Alloc(buckets_per_table_);
    }
  }

  ~LpCockooHash() {
    for (int i = 0; i < tables_.size(); i++) {
      opts_.Free(tables_[i], buckets_per_table_);
    }
  }

  iterator begin() const { return iterator{this, 0, 0}; }
  iterator end() const { return iterator{this, NumHashes, 0}; }
  iterator find(const K& key) const;
  void erase(iterator iter);
  std::pair<iterator, bool> insert(const K& key);

 private:
  struct Coord {
    size_t id;
    size_t parent;
    int table;
    size_t index;
  };

  Coord EvictChain(Coord tail, const std::vector<Coord>& queue);
  V* MutableSlot(Coord c) { return &tables_[c.table][c.index]; }

  const V& Slot(Coord c) const { return tables_[c.table][c.index]; }
  std::string CoordDebugString(Coord c) const {
    std::ostringstream m;
    m << Slot(c).DebugString() << "(id:" << c.id << " parent:";
    if (c.parent == kNoParent) {
      m << "-";
    } else {
      m << c.parent;
    }
    m << " table:" << c.table << " index:" << c.index << ")";
    return m.str();
  }

  size_t buckets_per_table_;
  std::array<V*, NumHashes> tables_;
  Opts opts_;
  std::vector<Coord> tmp_queue_;
  std::vector<Coord> tmp_chain_;
};

template <typename K, typename V, typename Ops>
typename LpCockooHash<K, V, Ops>::Coord LpCockooHash<K, V, Ops>::EvictChain(
    Coord tail, const std::vector<Coord>& queue) {
  std::vector<Coord>* chain = &tmp_chain_;
  chain->clear();
  chain->push_back(tail);
  while (tail.parent != kNoParent) {
    if (tail.parent >= queue.size()) abort();
    chain->push_back(queue[tail.parent]);
    tail = queue[tail.parent];
  }
  if (chain->size() < 2) {
    abort();
  }
  for (size_t i = 0; i < chain->size() - 1; i++) {
    Coord c0 = (*chain)[i];
    V* v0 = MutableSlot(c0);
    Coord c1 = (*chain)[i + 1];
    V* v1 = MutableSlot(c1);
    std::cout << "Swap: " << CoordDebugString(c0) << "<->"
              << CoordDebugString(c1) << ")\n";
    std::swap(*v0, *v1);
  }
  Coord vacated = chain->back();
  std::cout << "Vacate: " << CoordDebugString(vacated) << "\n";
  if (!opts_.Empty(Slot(vacated))) abort();
  return vacated;
}

template <typename K, typename V, typename Ops>
typename LpCockooHash<K, V, Ops>::iterator LpCockooHash<K, V, Ops>::find(
    const K& key) const {
  for (int hi = 0; hi < NumHashes; hi++) {
    const size_t hash = opts_.Hash(hi, key);
    size_t ti = hash % buckets_per_table_;
    for (int dd = 0; dd < BucketWidth; dd++) {
      V* elem = &tables_[hi][ti];
      if (opts_.Equals(hash, key, *elem)) {
        return iterator{this, hi, ti};
      }
      ti++;
      if (ti >= buckets_per_table_) ti = 0;
    }
  }
  return end();
}

template <typename K, typename V, typename Ops>
std::pair<typename LpCockooHash<K, V, Ops>::iterator, bool>
LpCockooHash<K, V, Ops>::insert(const K& key) {
  std::array<size_t, NumHashes> hashes;

  iterator empty_slot = end();
  for (int hi = 0; hi < NumHashes; hi++) {
    const HashValue hash = opts_.Hash(hi, key);
    hashes[hi] = hash;
    size_t ti = hash % buckets_per_table_;
    for (int dd = 0; dd < BucketWidth; dd++) {
      V* elem = &tables_[hi][ti];
      if (empty_slot == end() && opts_.Empty(*elem)) {
        empty_slot = iterator{this, hi, ti};
      } else if (opts_.Equals(hash, key, *elem)) {
        return std::make_pair(iterator{this, hi, ti}, false);
      }
      ti++;
      if (ti >= buckets_per_table_) ti = 0;
    }
  }
  if (empty_slot != end()) {
    opts_.Init(empty_slot.table, hashes[empty_slot.table], key, &*empty_slot);
    std::cout << "Insert: " << empty_slot.table << ":" << empty_slot.index
              << "\n";
    return std::make_pair(empty_slot, true);
  }

  // All slots are full.
  std::vector<Coord>* queue = &tmp_queue_;
  queue->clear();

  // Do a BFS to find a chain of entries that leads to an empty slot. See the
  // LAKF paper for details.
  for (int hash_idx = 0; hash_idx < NumHashes; hash_idx++) {
    size_t ti = hashes[hash_idx] % buckets_per_table_;
    for (int dd = 0; dd < BucketWidth; dd++) {
      queue->push_back(Coord{queue->size(), kNoParent, hash_idx, ti});
      ti++;
      if (ti >= buckets_per_table_) ti = 0;
    }
  }

  size_t qi = 0;
  for (int rep = 0; rep < 100; rep++) {
    const Coord c = (*queue)[qi];  // prospective elem to be evicted
    const V& elem = tables_[c.table][c.index];

    for (int hash_idx2 = 0; hash_idx2 < NumHashes; hash_idx2++) {
      if (hash_idx2 == c.table) continue;
      const size_t hash = opts_.Hash(hash_idx2, elem);
      size_t ti = hash % buckets_per_table_;
      for (int dd = 0; dd < BucketWidth; dd++) {
        const Coord c2 = {queue->size(), qi, hash_idx2, ti};
        V* dest_elem = MutableSlot(c2);
        if (opts_.Empty(*dest_elem)) {
          Coord vacated = EvictChain(c2, *queue);

          iterator it = {this, vacated.table, vacated.index};
          opts_.Init(it.table, hashes[it.table], key, &*it);
          std::cout << "Insert(2): " << it.table << ":" << it.index << "\n";
          return std::make_pair(it, true);
        }
        queue->push_back(c2);
        ti++;
        if (ti >= buckets_per_table_) ti = 0;
      }
    }
    qi++;
  }
  abort();
  return std::make_pair(end(), false);
}

template <typename K, typename V, typename Ops>
void LpCockooHash<K, V, Ops>::erase(iterator it) {
  V* slot = &*it;
  opts_.Clear(slot);
}
