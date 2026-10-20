#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ui_text {
// Single-line UI alphabet: printable ASCII and Russian А-Я/а-я/Ё/ё.
// Return the complete UTF-8 character width, or zero for invalid/unsupported text.
inline size_t decodeGlyph(const char* text, size_t available, uint16_t& codepoint) {
  if(!text || !available) return 0;
  const uint8_t a = uint8_t(text[0]);
  if(a >= 32 && a <= 126) { codepoint = a; return 1; }
  if(available < 2) return 0;
  const uint8_t b = uint8_t(text[1]);
  if((a == 0xD0 && (b == 0x81 || (b >= 0x90 && b <= 0xBF))) ||
     (a == 0xD1 && ((b >= 0x80 && b <= 0x8F) || b == 0x91))) {
    codepoint = uint16_t((a & 0x1F) << 6) | (b & 0x3F); return 2;
  }
  return 0;
}

inline bool validate(const char* text, size_t byteLength, size_t* glyphCount = nullptr) {
  if(glyphCount) *glyphCount = 0;
  if(!text && byteLength) return false;
  size_t at = 0, glyphs = 0;
  while(at < byteLength) {
    uint16_t codepoint; const size_t width = decodeGlyph(text + at, byteLength - at, codepoint);
    if(!width) return false;
    at += width; ++glyphs;
  }
  if(glyphCount) *glyphCount = glyphs;
  return true;
}

// Input is NUL-terminated; stop before an invalid character or a partial pair.
inline size_t prefixBytes(const char* text, size_t maxGlyphs) {
  if(!text) return 0;
  size_t at = 0, glyphs = 0;
  while(text[at] && glyphs < maxGlyphs) {
    uint16_t codepoint;
    const size_t available = uint8_t(text[at]) < 0x80 ? 1 : (text[at + 1] ? 2 : 1);
    const size_t width = decodeGlyph(text + at, available, codepoint);
    if(!width) break;
    at += width; ++glyphs;
  }
  return at;
}
}
