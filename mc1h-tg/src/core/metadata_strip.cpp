#include "core/metadata_strip.h"

#include <array>
#include <cstring>
#include <string_view>

namespace mc1h {
namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint16_t rd16(const std::uint8_t* p, bool be) {
  return be ? static_cast<std::uint16_t>((p[0] << 8) | p[1]) : static_cast<std::uint16_t>((p[1] << 8) | p[0]);
}

std::uint32_t rd32(const std::uint8_t* p, bool be) {
  return be ? (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]
            : (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}

void put32be(Bytes& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v >> 24));
  out.push_back(static_cast<std::uint8_t>(v >> 16));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v));
}

bool starts_with(const std::uint8_t* p, std::size_t n, std::string_view prefix) {
  return n >= prefix.size() && std::memcmp(p, prefix.data(), prefix.size()) == 0;
}

// Walks IFD0 of a TIFF block; calls fn(tag, entry_ptr). Returns false when malformed.
template <class Fn>
bool walk_ifd0(const std::uint8_t* tiff, std::size_t size, Fn&& fn) {
  if (tiff == nullptr || size < 8) return false;
  bool be;
  if (tiff[0] == 'M' && tiff[1] == 'M') {
    be = true;
  } else if (tiff[0] == 'I' && tiff[1] == 'I') {
    be = false;
  } else {
    return false;
  }
  if (rd16(tiff + 2, be) != 42) return false;
  const std::uint32_t ifd0 = rd32(tiff + 4, be);
  if (ifd0 < 8 || static_cast<std::size_t>(ifd0) + 2 > size) return false;
  const std::uint16_t count = rd16(tiff + ifd0, be);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::size_t e = static_cast<std::size_t>(ifd0) + 2 + 12 * static_cast<std::size_t>(i);
    if (e + 12 > size) return false;
    fn(rd16(tiff + e, be), tiff + e, be);
  }
  return true;
}

bool exif_has_tag(const std::uint8_t* tiff, std::size_t size, std::uint16_t wanted) {
  bool found = false;
  walk_ifd0(tiff, size, [&](std::uint16_t tag, const std::uint8_t*, bool) {
    if (tag == wanted) found = true;
  });
  return found;
}

std::string describe_exif(const std::uint8_t* tiff, std::size_t size, std::size_t total) {
  std::string s = "EXIF (" + std::to_string(total) + " bytes";
  if (exif_has_tag(tiff, size, 0x8825)) s += ", GPS";
  if (exif_has_tag(tiff, size, 0x010F) || exif_has_tag(tiff, size, 0x0110)) s += ", camera make/model";
  if (exif_has_tag(tiff, size, 0x0132)) s += ", date";
  return s + ")";
}

std::string hex8(unsigned v) {
  static const char* d = "0123456789ABCDEF";
  return std::string{'0', 'x', d[(v >> 4) & 15], d[v & 15]};
}

// ---------------------------------------------------------------- JPEG

struct JpegPiece {
  enum Kind { Segment, Standalone, Entropy } kind;
  std::uint8_t marker = 0;
  std::size_t offset = 0;  // Segment: payload start; Entropy: data start
  std::size_t length = 0;  // Segment: payload length; Entropy: data length
};

StripResult strip_jpeg(const Bytes& in, const StripOptions& opt) {
  StripResult r;
  r.report.format = "jpeg";
  const std::size_t n = in.size();
  std::vector<JpegPiece> pieces;
  std::size_t pos = 2;
  bool eoi = false;
  auto fail = [&](const std::string& msg) {
    r.ok = false;
    r.error = "jpeg: " + msg + " at offset " + std::to_string(pos);
    r.data.clear();
    return r;
  };

  while (pos < n) {
    if (in[pos] != 0xFF) return fail("expected marker");
    while (pos < n && in[pos] == 0xFF) ++pos;
    if (pos >= n) return fail("truncated marker");
    const std::uint8_t m = in[pos++];
    if (m == 0xD9) {
      eoi = true;
      break;
    }
    if (m == 0xD8) return fail("unexpected SOI");
    if (m == 0x00) return fail("invalid marker 0x00");
    if ((m >= 0xD0 && m <= 0xD7) || m == 0x01) {
      pieces.push_back({JpegPiece::Standalone, m, pos, 0});
      continue;
    }
    if (pos + 2 > n) return fail("truncated segment length");
    const std::size_t len = (std::size_t(in[pos]) << 8) | in[pos + 1];
    if (len < 2 || pos + len > n) return fail("bad segment length");
    pieces.push_back({JpegPiece::Segment, m, pos + 2, len - 2});
    pos += len;
    if (m == 0xDA) {  // SOS: entropy-coded data follows until the next marker
      const std::size_t start = pos;
      for (;;) {
        if (pos >= n) return fail("truncated scan data (no EOI)");
        if (in[pos] != 0xFF) {
          ++pos;
          continue;
        }
        if (pos + 1 >= n) return fail("truncated scan data");
        const std::uint8_t b = in[pos + 1];
        if (b == 0x00 || (b >= 0xD0 && b <= 0xD7)) {
          pos += 2;
          continue;
        }
        if (b == 0xFF) {  // fill byte
          ++pos;
          continue;
        }
        break;
      }
      pieces.push_back({JpegPiece::Entropy, 0, start, pos - start});
    }
  }
  if (!eoi) return fail("missing EOI");
  const std::size_t trailer = n - pos;

  // Decide what to keep.
  std::vector<bool> keep(pieces.size(), true);
  Bytes jfif_payload;
  bool have_jfif = false;
  bool icc_seen = false;
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    const JpegPiece& p = pieces[i];
    if (p.kind != JpegPiece::Segment) continue;
    const std::uint8_t* d = in.data() + p.offset;
    const std::size_t len = p.length;
    const std::uint8_t m = p.marker;
    if (m == 0xE0) {
      if (starts_with(d, len, std::string_view("JFIF\0", 5)) && len >= 14 && !have_jfif) {
        have_jfif = true;
        jfif_payload.assign(d, d + 14);
        if (d[12] != 0 || d[13] != 0 || len > 14) r.report.removed.push_back("JFIF thumbnail");
        jfif_payload[12] = 0;
        jfif_payload[13] = 0;
        r.report.kept.push_back("JFIF header");
      } else if (starts_with(d, len, std::string_view("JFXX\0", 5))) {
        r.report.removed.push_back("JFXX extension (thumbnail)");
      } else {
        r.report.removed.push_back("APP0 (" + std::to_string(len) + " bytes)");
      }
      keep[i] = false;  // JFIF is re-emitted from jfif_payload
    } else if (m == 0xE1) {
      keep[i] = false;
      if (starts_with(d, len, std::string_view("Exif\0\0", 6))) {
        if (r.report.orientation == 0) r.report.orientation = exif_orientation(d + 6, len - 6);
        r.report.removed.push_back(describe_exif(d + 6, len - 6, len));
      } else if (starts_with(d, len, std::string_view("http://ns.adobe.com/xap/1.0/\0", 29))) {
        r.report.removed.push_back("XMP (" + std::to_string(len) + " bytes)");
      } else if (starts_with(d, len, std::string_view("http://ns.adobe.com/xmp/extension/\0", 35))) {
        r.report.removed.push_back("extended XMP (" + std::to_string(len) + " bytes)");
      } else {
        r.report.removed.push_back("APP1 (" + std::to_string(len) + " bytes)");
      }
    } else if (m == 0xE2) {
      if (starts_with(d, len, std::string_view("ICC_PROFILE\0", 12)) && opt.keep_icc) {
        if (!icc_seen) r.report.kept.push_back("ICC profile");
        icc_seen = true;
      } else {
        keep[i] = false;
        if (starts_with(d, len, std::string_view("ICC_PROFILE\0", 12))) {
          r.report.removed.push_back("ICC profile");
        } else if (starts_with(d, len, std::string_view("MPF\0", 4))) {
          r.report.removed.push_back("MPF multi-picture index");
        } else {
          r.report.removed.push_back("APP2 (" + std::to_string(len) + " bytes)");
        }
      }
    } else if (m == 0xEE && starts_with(d, len, "Adobe")) {
      r.report.kept.push_back("Adobe APP14 (colour transform)");
    } else if (m >= 0xE3 && m <= 0xEF) {
      keep[i] = false;
      std::string what = (m == 0xED) ? "APP13 Photoshop/IPTC" : "APP" + std::to_string(m - 0xE0);
      r.report.removed.push_back(what + " (" + std::to_string(len) + " bytes)");
    } else if (m == 0xFE) {
      keep[i] = false;
      r.report.removed.push_back("comment (" + std::to_string(len) + " bytes)");
    }
  }
  if (trailer > 0) r.report.removed.push_back("data after EOI (" + std::to_string(trailer) + " bytes)");

  // Emit.
  Bytes& out = r.data;
  out.reserve(n);
  out.push_back(0xFF);
  out.push_back(0xD8);
  auto put_segment = [&](std::uint8_t marker, const std::uint8_t* payload, std::size_t len) {
    out.push_back(0xFF);
    out.push_back(marker);
    out.push_back(static_cast<std::uint8_t>((len + 2) >> 8));
    out.push_back(static_cast<std::uint8_t>((len + 2) & 0xFF));
    out.insert(out.end(), payload, payload + len);
  };
  if (have_jfif) put_segment(0xE0, jfif_payload.data(), jfif_payload.size());
  if (opt.keep_orientation && r.report.orientation >= 2 && r.report.orientation <= 8) {
    Bytes exif = {'E', 'x', 'i', 'f', 0, 0};
    Bytes tiff = minimal_exif_tiff(r.report.orientation);
    exif.insert(exif.end(), tiff.begin(), tiff.end());
    put_segment(0xE1, exif.data(), exif.size());
    r.report.orientation_written = true;
    r.report.kept.push_back("orientation " + std::to_string(r.report.orientation) + " (minimal EXIF)");
  }
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    if (!keep[i]) continue;
    const JpegPiece& p = pieces[i];
    switch (p.kind) {
      case JpegPiece::Segment: put_segment(p.marker, in.data() + p.offset, p.length); break;
      case JpegPiece::Standalone:
        out.push_back(0xFF);
        out.push_back(p.marker);
        break;
      case JpegPiece::Entropy:
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(p.offset),
                   in.begin() + static_cast<std::ptrdiff_t>(p.offset + p.length));
        break;
    }
  }
  out.push_back(0xFF);
  out.push_back(0xD9);
  r.ok = true;
  return r;
}

// ---------------------------------------------------------------- PNG

constexpr std::array<std::uint8_t, 8> kPngSig = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

struct PngChunk {
  std::string type;
  std::size_t data_offset = 0;
  std::size_t length = 0;
};

bool is_png_kept_ancillary(const std::string& t, const StripOptions& opt) {
  // Rendering-relevant ancillary chunks (PNG 3rd ed. + APNG).
  static const char* kKeep[] = {"tRNS", "gAMA", "cHRM", "sRGB", "sBIT", "bKGD", "pHYs", "hIST",
                                "cICP", "mDCv", "cLLi", "acTL", "fcTL", "fdAT"};
  for (const char* k : kKeep) {
    if (t == k) return true;
  }
  return t == "iCCP" && opt.keep_icc;
}

StripResult strip_png(const Bytes& in, const StripOptions& opt) {
  StripResult r;
  r.report.format = "png";
  const std::size_t n = in.size();
  std::size_t pos = kPngSig.size();
  std::vector<PngChunk> chunks;
  bool iend = false;
  auto fail = [&](const std::string& msg) {
    r.ok = false;
    r.error = "png: " + msg + " at offset " + std::to_string(pos);
    r.data.clear();
    return r;
  };
  while (pos < n) {
    if (pos + 12 > n) return fail("truncated chunk header");
    const std::uint32_t len = rd32(in.data() + pos, true);
    if (len > 0x7FFFFFFFu || pos + 12 + static_cast<std::size_t>(len) > n) return fail("bad chunk length");
    std::string type(reinterpret_cast<const char*>(in.data() + pos + 4), 4);
    for (char c : type) {
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return fail("bad chunk type");
    }
    const std::uint32_t stored = rd32(in.data() + pos + 8 + len, true);
    if (crc32_png(in.data() + pos + 4, 4 + static_cast<std::size_t>(len)) != stored) {
      return fail("CRC mismatch in chunk " + type);
    }
    if (chunks.empty() && type != "IHDR") return fail("first chunk is not IHDR");
    chunks.push_back({type, pos + 8, len});
    pos += 12 + static_cast<std::size_t>(len);
    if (type == "IEND") {
      iend = true;
      break;
    }
  }
  if (!iend) return fail("missing IEND");
  const std::size_t trailer = n - pos;

  std::vector<bool> keep(chunks.size(), true);
  bool icc_reported = false;
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    const PngChunk& c = chunks[i];
    const bool critical = c.type[0] >= 'A' && c.type[0] <= 'Z';
    if (critical) continue;  // IHDR, PLTE, IDAT, IEND and unknown critical chunks must stay
    if (is_png_kept_ancillary(c.type, opt)) {
      if (c.type == "iCCP" && !icc_reported) {
        r.report.kept.push_back("ICC profile");
        icc_reported = true;
      }
      continue;
    }
    keep[i] = false;
    const std::uint8_t* d = in.data() + c.data_offset;
    if (c.type == "eXIf") {
      if (r.report.orientation == 0) r.report.orientation = exif_orientation(d, c.length);
      r.report.removed.push_back(describe_exif(d, c.length, c.length));
    } else if (c.type == "tEXt" || c.type == "zTXt" || c.type == "iTXt") {
      std::size_t kw = 0;
      while (kw < c.length && kw < 79 && d[kw] != 0) ++kw;
      r.report.removed.push_back(c.type + " '" + std::string(reinterpret_cast<const char*>(d), kw) + "' (" +
                                 std::to_string(c.length) + " bytes)");
    } else {
      r.report.removed.push_back(c.type + " (" + std::to_string(c.length) + " bytes)");
    }
  }
  if (trailer > 0) r.report.removed.push_back("data after IEND (" + std::to_string(trailer) + " bytes)");

  Bytes& out = r.data;
  out.reserve(n);
  out.insert(out.end(), kPngSig.begin(), kPngSig.end());
  bool exif_written = false;
  const bool want_exif = opt.keep_orientation && r.report.orientation >= 2 && r.report.orientation <= 8;
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    const PngChunk& c = chunks[i];
    if (want_exif && !exif_written && c.type == "IDAT") {
      Bytes tiff = minimal_exif_tiff(r.report.orientation);
      Bytes body = {'e', 'X', 'I', 'f'};
      body.insert(body.end(), tiff.begin(), tiff.end());
      put32be(out, static_cast<std::uint32_t>(tiff.size()));
      out.insert(out.end(), body.begin(), body.end());
      put32be(out, crc32_png(body.data(), body.size()));
      exif_written = true;
      r.report.orientation_written = true;
      r.report.kept.push_back("orientation " + std::to_string(r.report.orientation) + " (minimal eXIf)");
    }
    if (!keep[i]) continue;
    const std::size_t start = c.data_offset - 8;
    out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(start),
               in.begin() + static_cast<std::ptrdiff_t>(c.data_offset + c.length + 4));
  }
  r.ok = true;
  return r;
}

}  // namespace

std::uint32_t crc32_png(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t c = crc ^ 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

int exif_orientation(const std::uint8_t* tiff, std::size_t size) {
  int result = 0;
  walk_ifd0(tiff, size, [&](std::uint16_t tag, const std::uint8_t* e, bool be) {
    if (tag != 0x0112 || result != 0) return;
    const std::uint16_t type = rd16(e + 2, be);
    const std::uint32_t count = rd32(e + 4, be);
    if (type != 3 || count < 1) return;  // SHORT
    const std::uint16_t v = rd16(e + 8, be);
    if (v >= 1 && v <= 8) result = v;
  });
  return result;
}

std::vector<std::uint8_t> minimal_exif_tiff(int orientation) {
  const auto o = static_cast<std::uint8_t>(orientation);
  return {'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,  // header, IFD0 at 8
          0x00, 0x01,                                    // one entry
          0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, o, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00};  // no next IFD
}

StripResult strip_metadata(const std::vector<std::uint8_t>& input, const StripOptions& options) {
  if (input.size() >= 3 && input[0] == 0xFF && input[1] == 0xD8 && input[2] == 0xFF) {
    return strip_jpeg(input, options);
  }
  if (input.size() >= kPngSig.size() && std::memcmp(input.data(), kPngSig.data(), kPngSig.size()) == 0) {
    return strip_png(input, options);
  }
  StripResult r;
  r.error = "unsupported format (only JPEG and PNG are handled)";
  if (!input.empty()) r.error += ", first byte " + hex8(input[0]);
  return r;
}

}  // namespace mc1h
