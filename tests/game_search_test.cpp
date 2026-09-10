#include "game_search.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace game_search;
  const auto key = Key("冒险岛DS");
  assert(Matches(key, "冒险"));
  assert(Matches(key, "MAO XIAN DAO ds"));
  assert(Matches(key, "mxd"));
  assert(!Matches(key, "超级马里奥"));
  assert(Matches(Key("口袋妖怪"), "kou dai yao guai"));
  assert(Matches(Key("口袋妖怪"), "kdyg"));
  assert(Matches(Key("绿宝石"), "lvbaoshi"));
  assert(Matches(Key("光明之魂1"), "gmzh1"));
  assert(Matches(Key("Final Fantasy IV"), "FINAL fantasy"));
  assert(Matches(Key("寶可夢"), "baokemeng"));
  assert(Matches(Key(""), ""));
  std::string text = "abc冒险😀";
  Backspace(text); assert(text == "abc冒险");
  Backspace(text); assert(text == "abc冒");
  Backspace(text); assert(text == "abc");
  Backspace(text); assert(text == "ab");
  text.clear(); Backspace(text); assert(text.empty());
  std::cout << "game search tests passed\n";
}
