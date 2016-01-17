#include <cybozu/test.hpp>
#include <cybozu/util.hpp>

AUTOTEST(tokenize) {
    cybozu_assert(cybozu::tokenize("", ' ').empty());
    cybozu_assert(cybozu::tokenize(" ", ' ').empty());
    cybozu_assert(cybozu::tokenize(" a", ' ') ==
                  std::vector<std::string>{"a"});
    cybozu_assert(cybozu::tokenize("a ", ' ') ==
                  std::vector<std::string>{"a"});
    cybozu_assert(cybozu::tokenize(" abc  def ", ' ') ==
                  std::vector<std::string>{"abc", "def"});
    cybozu_assert(cybozu::tokenize(" abc  def g", ' ') ==
                  std::vector<std::string>{"abc", "def", "g"});
    cybozu_assert(cybozu::tokenize("0123", ' ') ==
                  std::vector<std::string>{"0123"});
}
