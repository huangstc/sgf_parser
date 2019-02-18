# sgf_parser
A [SGF](https://senseis.xmp.net/?SmartGameFormat) parser in C++.

### User Guide

To use the library in your project, you need to install [Bazel](https://bazel.build/) and put the following rule in Bazel's `WORKSPACE` file.
```
http_archive(
    name = "com_github_huangstc_sgf_parser",
    url = "https://github.com/huangstc/sgf_parser/archive/master.zip",
    strip_prefix = "sgf_parser-master",
)
```

Then add this library to the dependency list of your program:
```
cc_binary(
    name = "my_program",
    srcs = ["my_program.cc"],
    deps = [
      "@com_github_huangstc_sgf_parser//:sgf_parser",
      "... other deps ...",
    ],
)
```

Add include its header:
```cc
#include "sgf_parser/parser.h"
...
const std::string sgf = ReadFileToString(filename);
GameRecord game;
string errors;
CHECK(SimpleParseSgf(sgf, &game, nullptr, &errors)) << errors;
LOG(INFO) << game.DebugString();
...    
```
