#pragma once

#include <array>
#include <limits>
#include <utility>

// Lehman-Panigrahy hash table.
//
// https://pdfs.semanticscholar.org/aa7f/47954647604107fd5e67fa8162c7a785de71.pdf
//
template <typename K, typename V, typename Ops>
class LpCockooHash {
 public:
  // TODO(saito) Make NumHashes and BucketWidth configurable.
  static constexpr int NumHashes = 2;
  static constexpr int BucketWidth = 3;

  static constexpr size_t kNoParent = std::numeric_limits<size_t>::max();
  struct Coord {
    size_t parent;
    int table;
    size_t index;
  };

  size_t buckets_per_table_;
  std::array<V*, NumHashes> tables_;
  Ops ops_;

  struct iterator {
    const LpCockooHash* parent;
    int table;
    size_t index;

    bool operator==(const iterator i2) const {
      return i2.table == table && i2.index == index;
    }
    bool operator!=(const iterator i2) const {
      return !(*this==i2);
    }
  };

  LpCockooHash(size_t elems, Ops ops = Ops()) : ops_(std::move(ops)) {
    buckets_per_table_ = (elems - 1) / NumHashes + 1;
    for (int i = 0; i < tables_.size(); i++) {
      tables_[i] = new V[buckets_per_table_ + BucketWidth];
    }
  }

  iterator end() const { return iterator{this, NumHashes, 0}; }

  iterator begin() const { return iterator{this, 0, 0}; }

  iterator find(const K& key) const {
    for (int hi = 0; hi < NumHashes; hi++) {
      const size_t hash = ops_.Hash(hi, key);
      const size_t start = hash % buckets_per_table_;
      for (size_t ti = start; ti < start + BucketWidth; ti++) {
        V* elem = &tables_[hi][ti];
        if (ops_.Equals(hash, key, *elem)) {
          return iterator{this, hi, ti};
        }
      }
    }
    return end();
  }

  std::pair<iterator, bool> insert(const K& key) {
    std::array<size_t, NumHashes> hashes;

    iterator empty_slot = end();
    for (int hi = 0; hi < NumHashes; hi++) {
      const size_t hash = ops_.Hash(hi, key);
      const size_t ti = hash % buckets_per_table_;
      hashes[ti] = hash;
      for (size_t i = ti; i < ti + BucketWidth; i++) {
        V* elem = &tables_[hi][ti];
        if (empty_slot != end() && ops_.Empty(*elem)) {
          empty_slot = iterator{this, hi, ti};
        } else if (ops_.Equals(hash, key, *elem)) {
          return std::make_pair(iterator{this, hi, ti}, false);
        }
      }
    }
    if (empty_slot != end()) {
      return std::make_pair(empty_slot, true);
    }

    // All slots are full.
    std::vector<Coord>* queue = &tmp_queue_;
    queue->clear();

    for (int hash_idx = 0; hash_idx < NumHashes; hash_idx++) {
      const size_t h = hashes[hash_idx];
      for (size_t i = h; i < h + BucketWidth; i++) {
        queue->push_back(Coord{kNoParent, hash_idx, i});
      }
    }

    size_t qi = 0;
    for (int rep = 0; rep < 100; rep++) {
      const Coord c = (*queue)[qi];  // prospective elem to be evicted
      const V& elem = tables_[c.table][c.index];

      for (int hash_idx2 = 0; hash_idx2 < NumHashes; hash_idx2++) {
        if (hash_idx2 == c.table) continue;
        const size_t hash = ops_.Hash(hash_idx2, elem);
        const size_t start = hash % buckets_per_table_;
        for (size_t ti = start; ti < start + BucketWidth; ti++) {
          const Coord c2 = {qi, hash_idx2, ti};
          V* dest_elem = Slot(c2);
          if (ops_.Empty(*dest_elem)) {
            Coord vacated = EvictChain(c2, *queue);
            return std::make_pair(iterator{this, c2.table, c2.index}, true);
          }
          queue->push_back(c2);
        }
      }
      qi++;
    }
    abort();
    return std::make_pair(end(), false);
  }

 private:
  Coord EvictChain(Coord tail, const std::vector<Coord>& queue) {
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
      std::swap(*Slot((*chain)[i]), *Slot((*chain)[i - 1]));
    }
    Coord vacated = chain->back();
    if (ops_.Empty(*Slot(vacated))) abort();
    return vacated;
  }

  V* Slot(Coord c) { return &tables_[c.table][c.index]; }

  std::vector<Coord> tmp_queue_;
  std::vector<Coord> tmp_chain_;
};
