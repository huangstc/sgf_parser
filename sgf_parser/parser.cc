#include "sgf_parser/parser.h"

#include <math.h>       /* fabs */
#include <fstream>
#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"

namespace sgf_parser {

using absl::string_view;
using absl::StrAppend;
using absl::StrCat;
using std::string;

string ReadFileToString(const string& filename) {
  string sgf;
  string line;
  std::ifstream myfile(filename);
  if (myfile.is_open()) {
    while (std::getline (myfile, line)) {
      absl::StrAppend(&sgf, line, "\n");
    }
    myfile.close();
  }
  return sgf;
}

GameRecord::GameRecord()
    : board_width(0), board_height(0), komi(0.0f), handicap(0), timelimit(-1),
      result(0.0f), resigned(false) {
}

void GameRecord::Reset() {
  board_width = 0;
  board_height = 0;
  komi = 0.0f;
  handicap = 0;
  timelimit = -1;
  result = 0.0f;
  resigned = false;

  black_name.clear();
  black_rank.clear();
  white_name.clear();
  white_rank.clear();
  date.clear();
  rule.clear();
}

// Game record:
std::vector<GoPos> handicap_stones;  // Black stones in a handicapped game.
std::vector<GoMove> moves;           // Moves.

string GameRecord::DebugString() const {
  string debug;
  StrAppend(&debug, "Board Size: [", board_width, "*" , board_height, "]  ");
  StrAppend(&debug, "Komi: ", komi, "  ");
  StrAppend(&debug, "Handicap: ", handicap, "  ");
  StrAppend(&debug, "Time limit: ", timelimit, " seconds.\n");
  StrAppend(&debug, "Black: ", black_name, " Rank: ", black_rank, "  ");
  StrAppend(&debug, "White: ", white_name, " Rank: ", white_rank, "\n");
  StrAppend(&debug, "Date: ", date, "  Rule: ", rule, "  ");
  StrAppend(&debug, "Result: ", (result > 0 ? "B" : "W"), " wins by ");
  if (resigned) {
    StrAppend(&debug, "resigned\n");
  } else {
    StrAppend(&debug, "+", fabs(result), "\n");
  }
  if (!black_stones.empty()) {
    StrAppend(&debug, "Black stones: ");
    for (const auto p : black_stones) {
      StrAppend(&debug, "[", p.first, ",", p.second, "] ");
    }
    StrAppend(&debug, "\n");
  }
  if (!white_stones.empty()) {
    StrAppend(&debug, "White stones: ");
    for (const auto p : white_stones) {
      StrAppend(&debug, "[", p.first, ",", p.second, "] ");
    }
    StrAppend(&debug, "\n");
  }
  StrAppend(&debug, "Moves:\n");
  for (const auto& move : moves) {
    StrAppend(&debug, (move.player == GoMove::BLACK ? "B" : "W"));
    if (move.pass) {
      StrAppend(&debug, " passed  ");
    } else {
      StrAppend(&debug, "[", move.move.first, ",", move.move.second, "] ");
    }
  }
  return debug;
}

#define LOG_ERROR(msg) do {                                      \
  if (errors != nullptr) absl::StrAppend(errors, (msg), "\n");   \
  LOG(WARNING) << "SGF parser error: " << (msg);                 \
} while (0)

#define RETURN_IF(condition, msg, retval) do {                   \
  if ((condition)) {                                             \
    LOG_ERROR((msg));                                            \
    return (retval);                                             \
  }                                                              \
} while (0)

namespace internal {

void DumpTree(const GameTree& tree, int level) {
  string indent(level * 2, ' ');
  LOG(INFO) << indent << "A tree at level " << level;
  for (size_t i = 0; i < tree.sequence.size(); ++i) {
    const auto& node = tree.sequence[i];
    LOG(INFO) << indent << " Node #" << i;
    for (const auto& prop : node) {
      LOG(INFO) << indent << "  Prop ID=" << prop.id << ", Values="
                << absl::StrJoin(prop.values, ",");
    }
  }
  LOG(INFO) << indent << "Subtrees:";
  for (const auto& child : tree.children) {
    DumpTree(*child, level+1);
  }
}

void DumpRoot(const GameTree& root) {
  for (const auto& tree : root.children) {
    DumpTree(*tree, 0);
  }
}

GameTree* NewChild(GameTree* current) {
  GameTree* child = new GameTree(current);
  current->children.emplace_back(absl::WrapUnique<GameTree>(child));
  return child;
}

GameNode* NewNode(GameTree* current) {
  current->sequence.emplace_back(GameNode());
  return &current->sequence.back();
}

Property* NewProperty(string_view property_id, GameNode* node) {
  node->emplace_back(Property(property_id));
  return &node->back();
}

string_view SubstrAndStripWhitespace(
    string_view sgf, string_view::size_type start, string_view::size_type len) {
  string_view s = sgf.substr(start, len);
  s = absl::StripLeadingAsciiWhitespace(s);
  return absl::StripTrailingAsciiWhitespace(s);
}

string_view::size_type FindFirst(string_view sgf, string_view::size_type start,
                                 string_view targets, bool expect_contents) {
  VLOG(2) << "Searching at the position: " << sgf.substr(start);
  bool escaping = false;
  for (; start < sgf.size(); ++start) {
    if (escaping) {
      escaping = false;
    } else {
      const char cur = sgf[start];
      if (cur == '\\') {
        escaping = true;
      } else if (targets.find(cur) != string_view::npos) {
        return start;
      } else if (!expect_contents && !absl::ascii_isspace(cur)) {
        return string_view::npos;
      }
    }
  }
  return string_view::npos;
}

#define RETURN_IF_NPOS(pos, msg, retval) \
    RETURN_IF((pos) == string_view::npos, msg, retval)

string_view::size_type ConsumeNode(string_view sgf,
                                   string_view::size_type start,
                                   GameNode* node, string* errors) {
  enum State {
    NODE_START  = 2,     //   '['  -->  VALUE_START
    VALUE_START = 6,     //   ']'  -->  NEXT_VALUE
    NEXT_VALUE  = 7,     //   '['  -->  VALUE_START
                         //   ';'  -->  NODE_START
                         //   '('  -->  TREE_START
                         //   ')'  -->  NEXT_TREE
  };
  State state = NODE_START;
  string_view::size_type cursor = start;
  Property* current_property = nullptr;
  do {
    if (state == NODE_START) {
      VLOG(2) << "Enter state NODE_START";
      auto p = FindFirst(sgf, cursor, "[", true);
      RETURN_IF_NPOS(p, "Reach the end of of node.", p);
      auto id = SubstrAndStripWhitespace(sgf, cursor, p - cursor);
      current_property = NewProperty(id, node);
      state = VALUE_START;
      cursor = p + 1;
    } else if (state == VALUE_START) {
      VLOG(2) << "Enter state VALUE_START";
      auto p = FindFirst(sgf, cursor, "]", true);
      RETURN_IF_NPOS(p, "Missing the end of a property value.", p);
      // Extract property value.
      current_property->values.emplace_back(sgf.substr(cursor, p - cursor));
      state = NEXT_VALUE;
      cursor = p + 1;
    } else if (state == NEXT_VALUE) {
      VLOG(2) << "Enter state NEXT_VALUE";
      auto p = FindFirst(sgf, cursor, "[;()", true);
      RETURN_IF_NPOS(p, "Missing the end of a node.", p);
      string_view gap = SubstrAndStripWhitespace(sgf, cursor, p - cursor);
      if (sgf[p] == '[') {
        if (!gap.empty()) {
          current_property = NewProperty(gap, node);
        }
        state = VALUE_START;
      } else {   // ';', '(' or ')'
        if (!gap.empty()) {
          LOG_ERROR("Non-empty contents after the end of a value.");
          return string_view::npos;
        }
        return p;
      }
      cursor = p + 1;
    }
  } while (true);
}

// Return false if the input is ill-formatted.
// All errors are saved to "errors" if it is not null.
bool ParseToRoot(string_view sgf, GameTree* root, string* errors) {
  enum State {
    START = 0,         // Start of everything,   '('  -->  TREE_START
    TREE_START = 1,    // Enter a new tree,      ';'  -->  NODE_START
    NODE_START = 2,    // Start a node,          ';'  -->  NODE_START
                       //                        '('  -->  TREE_START
                       //                        ')'  -->  NEXT_TREE
    NEXT_TREE = 3,     // A tree is done,        '('  -->  TREE_START
                       //                        ')'  -->  NEXT_TREE
                       //                        EOF  -->  END
    END = 4,           // The final state.
  };

  GameTree* current_tree = root;
  State state = START;
  string_view::size_type cursor = 0;

  do {
    if (state == START) {
      auto p = FindFirst(sgf, cursor, "(", false);
      RETURN_IF_NPOS(p, "Failed in finding a tree start.", false);
      cursor = p + 1;
      state = TREE_START;
      current_tree = NewChild(current_tree);
    } else if (state == TREE_START) {
      VLOG(2) << "Tree start.";
      auto p = FindFirst(sgf, cursor, ";", false);
      RETURN_IF_NPOS(p, "Failed in finding a node start.", false);
      state = NODE_START;
      cursor = p + 1;
    } else if (state == NODE_START) {
      VLOG(2) << "Node start.";
      auto p = ConsumeNode(sgf, cursor, NewNode(current_tree), errors);
      RETURN_IF_NPOS(p, "Error in parsing a node.", false);
      if (sgf[p] == ';') {
        state = NODE_START;
      } else if (sgf[p] == ')') {
        current_tree = current_tree->parent;
        RETURN_IF(current_tree == nullptr,
                  "Trying to going up in the root tree.", false);
        state = NEXT_TREE;
      } else if (sgf[p] == '(') {
        current_tree = NewChild(current_tree);
        state = TREE_START;
      }
      cursor = p + 1;
    } else if (state == NEXT_TREE) {
      VLOG(2) << "Next tree.";
      auto p = FindFirst(sgf, cursor, "()", false);
      if (p == string_view::npos) {
        state = END;
      } else if (sgf[p] == '(') {
        current_tree = NewChild(current_tree);
        state = TREE_START;
      } else if (sgf[p] == ')') {
        current_tree = current_tree->parent;
        RETURN_IF(current_tree == nullptr,
                  "Trying to going up in the root tree.", false);
        state = NEXT_TREE;
      }
      cursor = p + 1;
    }
  } while (state != END);

  RETURN_IF(current_tree != root, "Parser ends with a bad state.", false);

  return true;
}

#undef RETURN_IF_NPOS

std::pair<const GameTree*, int> GetFurthestLeaf(const GameTree* root) {
  if (root->children.empty()) {
    // This is already a leaf node. Return this node.
    return std::make_pair(root, root->sequence.size());
  }
  const GameTree* furthest_leaf = nullptr;
  int longest_dist = -1;
  for (const auto& child : root->children) {
    auto r = GetFurthestLeaf(child.get());
    if (r.second > longest_dist) {
      furthest_leaf = r.first;
      longest_dist = r.second;
    }
  }
  return std::make_pair(furthest_leaf, longest_dist + root->sequence.size());
}

}  // namespace internal

bool HandleProperty(const internal::Property& prop, GameRecord* record,
                    std::vector<std::pair<string, string>>* unparsed,
                    string* errors) {
  const string id = absl::AsciiStrToUpper(prop.id);
  if (id == "SZ") {
    RETURN_IF(prop.values.size() != 1, "Bad SZ property.", false);
    int size = 0;
    RETURN_IF(!absl::SimpleAtoi(prop.values[0], &size), "Bad SZ value.", false);
    record->board_width = size;
    record->board_height = size;
  } else if (id == "HA") {
    RETURN_IF(prop.values.size() != 1, "Bad HA property.", false);
    int ha = 0;
    RETURN_IF(!absl::SimpleAtoi(prop.values[0], &ha), "Bad HA value.", false);
    record->handicap = ha;
  } else if (id == "TM") {
    RETURN_IF(prop.values.size() != 1, "Bad TM property.", false);
    int tm = 0;
    if (absl::SimpleAtoi(prop.values[0], &tm)) {
      record->timelimit = tm;
    } else {
      LOG(WARNING) << "Cannot parse TM value: " << prop.values[0];
      record->timelimit = 0;
    }
  } else if (id == "KM") {
    RETURN_IF(prop.values.size() != 1, "Bad Komi property.", false);
    if (!absl::SimpleAtof(prop.values[0], &record->komi)) {
      LOG(WARNING) << "Cannot parse Komi, use default value " << prop.values[0];
      record->komi = 6.5f;
    }
  } else if (id == "RU") {
    RETURN_IF(prop.values.size() != 1, "Bad rule.", false);
    record->rule = string(prop.values[0]);
  } else if (id == "PB" || id == "BT") {
    RETURN_IF(prop.values.size() != 1, "Bad black name value.", false);
    record->black_name = string(prop.values[0]);
  } else if (id == "PW" || id == "WT") {
    RETURN_IF(prop.values.size() != 1, "Bad white name value.", false);
    record->white_name = string(prop.values[0]);
  } else if (id == "BR") {
    RETURN_IF(prop.values.size() != 1, "Bad black rank.", false);
    record->black_rank = string(prop.values[0]);
  } else if (id == "WR") {
    RETURN_IF(prop.values.size() != 1, "Bad white rank.", false);
    record->white_rank = string(prop.values[0]);
  } else if (id == "DT") {
    RETURN_IF(prop.values.size() != 1, "Bad date.", false);
    record->date = string(prop.values[0]);
  } else if (id == "RE") {
    RETURN_IF(prop.values.size() != 1, "Bad result (RE) property.", false);
    string re = absl::AsciiStrToUpper(prop.values[0]);
    // Resign, Timeout or Forfeit
    if (re.substr(0, 3) == "B+R" || re.substr(0, 3) == "B+T" ||
        re.substr(0, 3) == "B+F") {
      record->result = 1.2;    // Actually any positive number works.
      record->resigned = true;
    } else if (re.substr(0, 3) == "W+R" || re.substr(0, 3) == "W+T" ||
               re.substr(0, 3) == "W+F") {
      record->result = -1.2;  // Actually any negative number works.
      record->resigned = true;
    } else if (re.size() >= 3) {
      float score;
      RETURN_IF(!absl::SimpleAtof(re.substr(2), &score),
                "Bad result (RE) value: failed in parsing score.", false);
      if (re[0] == 'B') {
        record->result = score;
      } else if (re[0] == 'W') {
        record->result = -score;
      } else {
        LOG_ERROR("Bad result (RE) value: unknown color.");
        return false;
      }
    } else {
      LOG_ERROR("Bad result (RE) value: value too short.");
      return false;
    }
  } else if (id == "AB" || id == "AW") {
    std::vector<GoPos>* stones = (id == "AB" ? &record->black_stones
                                             : &record->white_stones);
    for (const auto& value : prop.values) {
      const string lower = absl::AsciiStrToLower(value);
      RETURN_IF(lower.size() != 2, "Bad coordinate.", false);
      GoCoord x = lower[0] - 'a';
      GoCoord y = lower[1] - 'a';
      stones->emplace_back(std::make_pair(x, y));
    }
  } else if (id == "B" || id == "W") {
    GoMove::Color color = (id == "B" ? GoMove::BLACK : GoMove::WHITE);
    for (const auto& value : prop.values) {
      const string lower = absl::AsciiStrToLower(value);
      if (lower.empty()) {
        record->moves.push_back(GoMove(color, true, std::make_pair(-1, -1)));
      } else {
        RETURN_IF(lower.size() != 2, StrCat("Bad coordinate:", value), false);
        GoCoord x = lower[0] - 'a';
        GoCoord y = lower[1] - 'a';
        record->moves.push_back(GoMove(color, false, std::make_pair(x, y)));
      }
    }
  } else if (unparsed != nullptr){
    unparsed->push_back(std::make_pair(id, absl::StrJoin(prop.values, ",")));
  }
  return true;
}

bool SimpleParseSgf(const string& sgf, GameRecord* record,
                    std::vector<std::pair<string, string>>* unparsed,
                    string* errors) {
  internal::GameTree root(nullptr);
  if (!internal::ParseToRoot(sgf, &root, errors)) {
    return false;
  }
  RETURN_IF(root.children.empty(), "An empty tree collection.", false);

  // Find the furthest leaf node.
  auto leaf_with_dist = GetFurthestLeaf(&root);

  // Get the path.
  std::vector<const internal::GameTree*> path;
  const internal::GameTree* node = leaf_with_dist.first;
  while (node != &root) {
    path.push_back(node);
    node = node->parent;
  }

  while (!path.empty()) {
    const internal::GameTree* current = path.back();
    path.pop_back();
    for (const auto& node : current->sequence) {
      for (const auto& prop : node) {
        if (!HandleProperty(prop, record, unparsed, errors)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool SimpleParseSgfAndCheck(
    const std::string& sgf_file_name, GoCoord expected_board_size,
    bool check_has_result, GameRecord* record, std::string* errors) {
   const string sgf = ReadFileToString(sgf_file_name);
   if (!sgf_parser::SimpleParseSgf(sgf_file_name, record, nullptr, errors)) {
     return false;
   }
   if (expected_board_size > 0) {
     if (record->board_width != expected_board_size ||
         record->board_height != expected_board_size) {
       LOG_ERROR("Unexpected board size.");
       return false;
     }
   }
   if (check_has_result && record->result == 0.0f) {
     LOG_ERROR("The game has an unknown result.");
     return false;
   }
   return true;
}

#undef RETURN_IF
#undef LOG_ERROR

}  // namespace sgf_parser
