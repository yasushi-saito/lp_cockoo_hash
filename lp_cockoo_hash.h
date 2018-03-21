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
// https://pdfs.semanticscholar.org/aa7f/47954647604107fd5e67fa8162c7a785de71.pdf
//
template <typename K, typename V, typename Opts>
class LpCockooHash {
 public:
  static constexpr int NumHashes = Opts::NumHashes;
  static constexpr int BucketWidth = Opts::BucketWidth;
  static constexpr size_t kNoParent = std::numeric_limits<size_t>::max();

  using HashValue = size_t;

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

  LpCockooHash(size_t elems, Opts opts = Opts()) : opts_(std::move(opts)) {
    buckets_per_table_ = (elems - 1) / NumHashes + 1;
    for (int i = 0; i < tables_.size(); i++) {
      tables_[i] = opts_.Alloc(buckets_per_table_ + BucketWidth);
    }
  }

  ~LpCockooHash() {
    for (int i = 0; i < tables_.size(); i++) {
      opts_.Free(tables_[i], buckets_per_table_ + BucketWidth);
    }
  }

  iterator begin() const { return iterator{this, 0, 0}; }
  iterator end() const { return iterator{this, NumHashes, 0}; }
  iterator find(const K& key) const;
  std::pair<iterator, bool> insert(const K& key);

 private:
  struct Coord {
    size_t id;
    size_t parent;
    int table;
    size_t index;

    std::string DebugString() const {
      std::ostringstream m;
      m << "{id:" << id << " parent:";
      if (parent == kNoParent) {
        m << "-";
      } else {
        m << parent;
      }
      m << " table:" << table << " index:" << index << "}";
      return m.str();
    }
  };

  Coord EvictChain(Coord tail, const std::vector<Coord>& queue);
  V* Slot(Coord c) { return &tables_[c.table][c.index]; }

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
  for (int i = chain->size() - 1; i >= 1; i--) {
    std::cout << "Swap: " << (*chain)[i].DebugString() << "<->"
              << (*chain)[i - 1].DebugString() << "\n";
    std::swap(*Slot((*chain)[i]), *Slot((*chain)[i - 1]));
  }
  Coord vacated = chain->back();
  std::cout << "Vacate: " << vacated.DebugString() << "\n";
  if (!opts_.Empty(*Slot(vacated))) abort();
  return vacated;
}

template <typename K, typename V, typename Ops>
typename LpCockooHash<K, V, Ops>::iterator LpCockooHash<K, V, Ops>::find(
    const K& key) const {
  for (int hi = 0; hi < NumHashes; hi++) {
    const size_t hash = opts_.Hash(hi, key);
    const size_t start = hash % buckets_per_table_;
    for (size_t ti = start; ti < start + BucketWidth; ti++) {
      V* elem = &tables_[hi][ti];
      if (opts_.Equals(hash, key, *elem)) {
        return iterator{this, hi, ti};
      }
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
    const size_t ti = hash % buckets_per_table_;
    hashes[hi] = hash;
    for (size_t i = ti; i < ti + BucketWidth; i++) {
      V* elem = &tables_[hi][ti];
      if (empty_slot == end() && opts_.Empty(*elem)) {
        empty_slot = iterator{this, hi, ti};
      } else if (opts_.Equals(hash, key, *elem)) {
        return std::make_pair(iterator{this, hi, ti}, false);
      }
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

  for (int hash_idx = 0; hash_idx < NumHashes; hash_idx++) {
    const size_t ti = hashes[hash_idx] % buckets_per_table_;
    for (size_t i = ti; i < ti + BucketWidth; i++) {
      queue->push_back(Coord{queue->size(), kNoParent, hash_idx, i});
    }
  }

  size_t qi = 0;
  for (int rep = 0; rep < 100; rep++) {
    const Coord c = (*queue)[qi];  // prospective elem to be evicted
    const V& elem = tables_[c.table][c.index];

    for (int hash_idx2 = 0; hash_idx2 < NumHashes; hash_idx2++) {
      if (hash_idx2 == c.table) continue;
      const size_t hash = opts_.Hash(hash_idx2, elem);
      const size_t start = hash % buckets_per_table_;
      for (size_t ti = start; ti < start + BucketWidth; ti++) {
        const Coord c2 = {queue->size(), qi, hash_idx2, ti};
        V* dest_elem = Slot(c2);
        if (opts_.Empty(*dest_elem)) {
          Coord vacated = EvictChain(c2, *queue);

          iterator it = {this, vacated.table, vacated.index};
          opts_.Init(it.table, hashes[it.table], key, &*it);
          std::cout << "Insert(2): " << it.table << ":" << it.index << "\n";
          return std::make_pair(it, true);
        }
        queue->push_back(c2);
      }
    }
    qi++;
  }
  abort();
  return std::make_pair(end(), false);
}
