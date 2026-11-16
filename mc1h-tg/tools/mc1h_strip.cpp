// mc1h_strip — remove metadata from a JPEG/PNG without re-encoding pixels.
//   mc1h_strip [--drop-orientation] [--drop-icc] <input> <output>
// Exit codes: 0 ok, 1 strip/IO failure, 2 usage.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "core/metadata_strip.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  mc1h::StripOptions opt;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--drop-orientation") {
      opt.keep_orientation = false;
    } else if (a == "--drop-icc") {
      opt.keep_icc = false;
    } else if (!a.empty() && a[0] == '-') {
      paths.clear();
      break;
    } else {
      paths.push_back(a);
    }
  }
  if (paths.size() != 2) {
    std::cerr << "usage: mc1h_strip [--drop-orientation] [--drop-icc] <input> <output>\n";
    return 2;
  }

  std::ifstream in(paths[0], std::ios::binary);
  if (!in) {
    std::cerr << "cannot open " << paths[0] << "\n";
    return 1;
  }
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    std::cerr << "read failed: " << paths[0] << "\n";
    return 1;
  }

  mc1h::StripResult r = mc1h::strip_metadata(data, opt);
  if (!r.ok) {
    std::cerr << "error: " << r.error << "\n";
    return 1;
  }

  // Write next to the destination, then rename: no half-written output.
  fs::path out_path(paths[1]);
  fs::path tmp = out_path;
  tmp += ".mc1h-tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(r.data.data()), static_cast<std::streamsize>(r.data.size()));
    if (!out) {
      std::cerr << "write failed: " << tmp << "\n";
      return 1;
    }
  }
  std::error_code ec;
  fs::rename(tmp, out_path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    std::cerr << "rename failed: " << out_path << "\n";
    return 1;
  }

  std::cerr << r.report.format << ": " << data.size() << " -> " << r.data.size() << " bytes\n";
  for (const auto& s : r.report.removed) std::cerr << "  removed: " << s << "\n";
  for (const auto& s : r.report.kept) std::cerr << "  kept:    " << s << "\n";
  return 0;
}
