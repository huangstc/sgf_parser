#include "sgf_parser.h"

#include <utility>

#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace sgf_parser {
namespace {

using ::absl::string_view;
using ::std::string;
using ::testing::HasSubstr;

class SgfParserIntenalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_v = 0;
  }

  bool Parse(string_view input) {
    trees_.clear();
    errors_.clear();
    return internal::ParseToCollection(input, &trees_, &errors_);
  }

  void VerifyNode(
      const internal::GameNode& node,
      const std::vector<std::pair<string, std::vector<string>>>& props) {
    ASSERT_EQ(node.size(), props.size());
    for (size_t i = 0; i < node.size(); ++i) {
      const auto& prop = node[i];
      EXPECT_EQ(prop.id, props[i].first);
      ASSERT_EQ(prop.values.size(), props[i].second.size());
      for (size_t j = 0; j < prop.values.size(); ++j) {
        EXPECT_EQ(prop.values[j], props[i].second[j]);
      }
    }
  }

  internal::TreeCollection trees_;
  string errors_;
};

TEST_F(SgfParserIntenalTest, BadStart) {
  EXPECT_FALSE(Parse("\n\n;"));
  EXPECT_THAT(errors_, HasSubstr("Failed in finding a tree start"));
}

TEST_F(SgfParserIntenalTest, NoNodeStart) {
  EXPECT_FALSE(Parse("(a;)"));
  EXPECT_THAT(errors_, HasSubstr("Failed in finding a node start"));
}

TEST_F(SgfParserIntenalTest, Simple) {
  const char kInput[] = "(;FF[4] SZ[19]AB[bd] [be]\n[af]\n\n"
                        "AW [aa] [ab] AB\n[cc];B[ce];W[gg]\n;B[cf])";
  EXPECT_TRUE(Parse(kInput));
  EXPECT_TRUE(errors_.empty());
  DumpTrees(trees_);
  ASSERT_EQ(1, trees_.size());
  ASSERT_EQ(4, trees_[0]->sequence.size());
  ASSERT_TRUE(trees_[0]->children.empty());
  VerifyNode(trees_[0]->sequence[0],
    {
      {"FF", {"4"}},
      {"SZ", {"19"}},
      {"AB", {"bd", "be", "af"}},
      {"AW", {"aa", "ab"}},
      {"AB", {"cc"}},
    });
}

class SgfParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_v = 0;
  }

  GameRecord ParseFile(const string& filename) {
    const string sgf = ReadFileToString(filename);
    GameRecord game;
    std::vector<std::pair<string, string>> unparsed;
    string errors;
    CHECK(SimpleParseSgf(sgf, &game, &unparsed, &errors)) << errors;
    for (const auto p : unparsed) {
      LOG(INFO) << "Unparsed property: " << p.first << ": " << p.second;
    }
    return game;
  }
};

TEST_F(SgfParserTest, Handicapped) {
  GameRecord game = ParseFile("testdata/handicapped.sgf");
  LOG(INFO) << "\n" << game.DebugString();
}

TEST_F(SgfParserTest, Resigned) {
  GameRecord game = ParseFile("testdata/resigned.sgf");
  LOG(INFO) << "\n" << game.DebugString();
}

}  // namespace
}  // namespace sgf_parser
