#include "app/console.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace mc1h {

EchoOff::EchoOff() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
    saved_ = mode;
    active_ = SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT)) != 0;
  }
#else
  termios t{};
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &t) == 0) {
    saved_ = t.c_lflag;
    t.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    active_ = tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0;
  }
#endif
}

EchoOff::~EchoOff() {
  if (!active_) return;
#ifdef _WIN32
  SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), static_cast<DWORD>(saved_));
#else
  termios t{};
  if (tcgetattr(STDIN_FILENO, &t) == 0) {
    t.c_lflag = static_cast<tcflag_t>(saved_);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
  }
#endif
  std::cout << std::endl;
}

Console::Console() : s_(std::make_shared<State>()) { thread_ = std::thread(&Console::run, s_); }

Console::~Console() {
  bool blocked;
  {
    std::lock_guard<std::mutex> lock(s_->mu);
    s_->stop = true;
    blocked = s_->reading;
  }
  s_->cv.notify_all();
  // A thread blocked in std::getline cannot be interrupted portably; it owns
  // a reference to the state and exits after its read returns.
  if (blocked) {
    thread_.detach();
  } else {
    thread_.join();
  }
}

void Console::request_line(const std::string& prompt, bool secret) {
  {
    std::lock_guard<std::mutex> lock(s_->mu);
    if (s_->request || s_->reading || s_->eof) return;
    s_->request = std::make_pair(prompt, secret);
  }
  s_->cv.notify_all();
}

bool Console::request_pending() const {
  std::lock_guard<std::mutex> lock(s_->mu);
  return s_->request.has_value() || s_->reading;
}

std::optional<std::string> Console::poll_line() {
  std::lock_guard<std::mutex> lock(s_->mu);
  if (s_->lines.empty()) return std::nullopt;
  std::string line = std::move(s_->lines.front());
  s_->lines.pop_front();
  return line;
}

bool Console::eof() const {
  std::lock_guard<std::mutex> lock(s_->mu);
  return s_->eof && s_->lines.empty();
}

void Console::print(const std::string& line) {
  std::lock_guard<std::mutex> lock(s_->out_mu);
  std::cout << line << std::endl;
}

void Console::run(std::shared_ptr<State> s) {
  for (;;) {
    std::pair<std::string, bool> req;
    {
      std::unique_lock<std::mutex> lock(s->mu);
      s->cv.wait(lock, [&] { return s->stop || s->request.has_value(); });
      if (s->stop) return;
      req = *s->request;
      s->request.reset();
      s->reading = true;
    }
    {
      std::lock_guard<std::mutex> lock(s->out_mu);
      std::cout << req.first << std::flush;
    }
    std::string line;
    bool ok;
    {
      std::optional<EchoOff> echo;
      if (req.second) echo.emplace();
      ok = static_cast<bool>(std::getline(std::cin, line));
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::lock_guard<std::mutex> lock(s->mu);
    s->reading = false;
    if (!ok) {
      s->eof = true;
      return;
    }
    if (s->stop) return;
    s->lines.push_back(std::move(line));
  }
}

}  // namespace mc1h
