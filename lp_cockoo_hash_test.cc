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
  std::string v;
};

struct HashOps {
  size_t Hash(int hash, Key k) const { return k; }
  size_t Hash(int hash, const Value& v) { return v.key; }

  bool Equals(size_t hash, Key k, const Value& v) const { return k == v.key; }
  bool Empty(const Value& v) const { return v.key == kEmpty; }
};

using Table = LpCockooHash<int, Value, HashOps>;

}  // namespace

TEST(CockooTest, Basic) {
  Table t(10);

  for (int i = 0; i < 20; i++) {
    auto p = t.insert(i);
    ASSERT_EQ(p.second, true);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
