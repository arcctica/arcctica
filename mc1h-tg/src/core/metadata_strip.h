// mc1h-tg — lossless metadata removal for JPEG and PNG before sending.
//
// Pixel data is never re-encoded: JPEG entropy-coded scans and PNG IDAT/fdAT
// chunks are copied byte for byte. Only container-level metadata is removed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mc1h {

struct StripOptions {
  // Re-emit a minimal EXIF holding only the Orientation tag (values 2..8),
  // so the image is displayed the same way as before.
  bool keep_orientation = true;
  // Keep the ICC colour profile (JPEG APP2 ICC_PROFILE, PNG iCCP).
  bool keep_icc = true;
};

struct StripReport {
  std::string format;                // "jpeg" | "png"
  int orientation = 0;               // EXIF orientation found in input, 0 if none
  bool orientation_written = false;  // minimal EXIF emitted
  std::vector<std::string> removed;  // human-readable list of removed items
  std::vector<std::string> kept;
};

struct StripResult {
  bool ok = false;
  std::string error;
  std::vector<std::uint8_t> data;
  StripReport report;
};

StripResult strip_metadata(const std::vector<std::uint8_t>& input, const StripOptions& options = {});

// Exposed for tests.
std::uint32_t crc32_png(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);
// Orientation (1..8) from a TIFF structure (EXIF payload after "Exif\0\0"),
// 0 when absent or malformed.
int exif_orientation(const std::uint8_t* tiff, std::size_t size);
// Minimal big-endian TIFF with IFD0 = { Orientation }.
std::vector<std::uint8_t> minimal_exif_tiff(int orientation);

}  // namespace mc1h
