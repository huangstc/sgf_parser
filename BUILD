cc_library(
    name = "sgf_parser-lib",
    srcs = ["sgf_parser.cc"],
    hdrs = ["sgf_parser.h"],
    deps = [
      "@com_google_absl//absl/memory",
      "@com_google_absl//absl/strings",
      "@com_github_google_glog//:glog",
    ],
    visibility=["//visibility:public"],
)

cc_test(
    name = "sgf_parser_test",
    srcs = ["sgf_parser_test.cc"],
    deps = [
      ":sgf_parser-lib",
      "@com_google_googletest//:gtest_main",
    ],
    data = glob(["testdata/*.sgf"]),
)
