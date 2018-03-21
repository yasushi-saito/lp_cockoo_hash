#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>

#include "lp_cockoo_hash.h"

namespace {
using Key = int;
constexpr Key kEmpty = -1;

struct Value {
  Value() { key = kEmpty; }
  Key key;
  int value;
};

struct HashOpts {
  static constexpr int NumHashes = 2;
  static constexpr int BucketWidth = 2;

  Value* Alloc(int n) { return new Value[n](); }
  void Free(Value* array, int n) { delete[] array; }

  size_t Hash(int hash_index, Key k) const { return k + hash_index; }
  size_t Hash(int hash_index, const Value& v) { return v.key + hash_index; }

  void Init(int hash_index, size_t hash, Key k, Value* v) { v->key = k; }
  bool Equals(size_t hash, Key k, const Value& v) const { return k == v.key; }
  bool Empty(const Value& v) const { return v.key == kEmpty; }
};

using Table = LpCockooHash<int, Value, HashOpts>;

}  // namespace

TEST(CockooTest, Basic) {
  Table t(10);

  std::mt19937 rand(0);
  for (int i = 0; i < 10; i++) {
    int k = rand() % 1000000;
    std::cout << i << ": Insert: " << k << "\n";
    auto p = t.insert(k);
    ASSERT_EQ(p.second, true);
    auto it = p.first;
    it->value = k + 1;
  }

  rand = std::mt19937(0);
  for (int i = 0; i < 10; i++) {
    int k = rand() % 1000000;
    std::cout << "Find: " << k << "\n";
    auto it = t.find(k);
    ASSERT_FALSE(it == t.end());
    ASSERT_EQ(it->value, k + 1);
    ASSERT_EQ(it->key, k);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
