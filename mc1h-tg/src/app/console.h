// mc1h-tg — line input on a worker thread, with optional no-echo for secrets.
#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mc1h {

class Console {
 public:
  Console();
  ~Console();  // joins the reader if it is idle; detaches it if blocked on stdin

  // Asks the reader thread to print `prompt` and read one line.
  // Ignored while a previous request is still unanswered.
  void request_line(const std::string& prompt, bool secret);
  bool request_pending() const;

  // Non-blocking: the answered line, if any. std::nullopt also after EOF;
  // check eof().
  std::optional<std::string> poll_line();
  bool eof() const;

  // Thread-safe output that does not interleave with the prompt line.
  void print(const std::string& line);

 private:
  // Shared with the reader thread so a detached reader never touches freed memory.
  struct State {
    std::mutex mu;
    std::condition_variable cv;
    std::optional<std::pair<std::string, bool>> request;
    bool reading = false;
    std::deque<std::string> lines;
    bool eof = false;
    bool stop = false;
    std::mutex out_mu;
  };
  static void run(std::shared_ptr<State> s);

  std::shared_ptr<State> s_;
  std::thread thread_;
};

// Turns terminal echo off for the scope (no-op when stdin is not a terminal).
class EchoOff {
 public:
  EchoOff();
  ~EchoOff();
  EchoOff(const EchoOff&) = delete;
  EchoOff& operator=(const EchoOff&) = delete;

 private:
  bool active_ = false;
  unsigned long saved_ = 0;
};

}  // namespace mc1h
