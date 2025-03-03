#pragma once

#include <iostream>

// Structure to ensure proper redirection and restoration of std::cin and
// std::cout
struct RedirectIO {
  RedirectIO(const RedirectIO &) = default;
  RedirectIO(RedirectIO &&) = delete;
  auto operator=(const RedirectIO &) -> RedirectIO & = default;
  auto operator=(RedirectIO &&) -> RedirectIO & = delete;
  RedirectIO(std::istream &in, std::ostream &out)
      : oldCinBuf(std::cin.rdbuf()), oldCoutBuf(std::cout.rdbuf()) {
    std::cin.rdbuf(in.rdbuf());
    std::cout.rdbuf(out.rdbuf());
  }

  ~RedirectIO() {
    std::cin.rdbuf(oldCinBuf);
    std::cout.rdbuf(oldCoutBuf);
  }

  std::streambuf *oldCinBuf;
  std::streambuf *oldCoutBuf;
};
