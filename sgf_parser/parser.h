#ifndef SGF_PARSER_PARSER_H_
#define SGF_PARSER_PARSER_H_

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"

namespace sgf_parser {

// Coordinate and position.
typedef int16_t GoCoord;
typedef std::pair<GoCoord, GoCoord> GoPos;

struct GoMove {
  enum Color {
    BLACK = 1,
    WHITE = 2,
  };

  GoMove(Color c, bool ps, GoPos mv) : player(c), pass(ps), move(mv) {}

  Color player;
  bool pass;     // true if the player passes.
  GoPos move;
};

// Parsed game record.
struct GameRecord {
  GoCoord board_width;    // SZ: board size.
  GoCoord board_height;   // SZ: board size.
  float   komi;           // KM: komi.
  int     handicap;       // HA: handicap.
  int     timelimit;      // TM: time limit in seconds.

  // Pre-set black stones, usually in a handicapped game.
  std::vector<GoPos> black_stones;
  std::vector<GoPos> white_stones;  // Pre-set white stone.
  std::vector<GoMove> moves;        // Moves.

  // Game result:
  float result;   // RE: a positive number means black wins by this number of points.
  bool resigned;  // RE: true if a player resigned. In this case, result is set
                  // to -1.2 if black resigned, or 1.2 if white resigned.

  // Other information:
  std::string black_name;    // PB or BT
  std::string black_rank;    // BR
  std::string white_name;    // PW or WT
  std::string white_rank;    // WR
  std::string date;          // DT: date of the game.
  std::string rule;          // RU: rule.

  GameRecord();

  // Reset all fields to default values.
  void Reset();

  // Dump contents to a string.
  std::string DebugString() const;
};

// If "unparsed" is not null, unparsed properties are saved to this vector.
// If "errors" is not null, parsing errors are saved to this string.
bool SimpleParseSgf(const std::string& sgf, GameRecord* record,
                    std::vector<std::pair<std::string, std::string>>* unparsed,
                    std::string* errors);

// Helper function for reading a file.
std::string ReadFileToString(const std::string& filename);

// Don't use internal types and functions.
namespace internal {

/*
   EBNF definition of SGF: https://www.red-bean.com/sgf/sgf4.html

    Collection = GameTree { GameTree }
    GameTree   = "(" Sequence { GameTree } ")"
    Sequence   = Node { Node }
    Node       = ";" { Property }
    Property   = PropIdent PropValue { PropValue }
    PropIdent  = UcLetter { UcLetter }
    PropValue  = "[" CValueType "]"
    CValueType = (ValueType | Compose)
    ValueType  = (None | Number | Real | Double | Color | SimpleText |
  	            	Text | Point  | Move | Stone)
*/

struct Property {
  absl::string_view id;
  std::vector<absl::string_view> values;

  explicit Property(absl::string_view pid) : id(pid) {}

  Property(Property&& other)
      : id(std::move(other.id)),
        values(std::move(other.values)) {}
};

typedef std::vector<Property> GameNode;

struct GameTree {
  GameTree* parent = nullptr;
  std::vector<GameNode> sequence;
  std::vector<std::unique_ptr<GameTree>> children;

  explicit GameTree(GameTree* p) : parent(p) {}

  GameTree(GameTree&& other)
      : parent(other.parent),
        sequence(std::move(other.sequence)),
        children(std::move(other.children)) {
  }
};

// Finds the first occurrence of any of the characters in `targets`.
absl::string_view::size_type FindFirst(
    absl::string_view sgf, absl::string_view::size_type start,
    absl::string_view targets, bool expect_contents);

// Parse a node.
absl::string_view::size_type ConsumeNode(
    absl::string_view sgf, absl::string_view::size_type start,
    GameNode* node, std::string* errors);

// Return false if the input is ill-formatted.
// All errors are saved to "errors" if it is not null.
bool ParseToRoot(absl::string_view sgf, GameTree* root, std::string* errors);

// For debugging.
void DumpRoot(const GameTree& root);

}  // namespace internal
}  // namespace sgf_parser

#endif  // SGF_PARSER_PARSER_H_
