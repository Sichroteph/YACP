#pragma once
#include <cstddef>
#include <cstdint>

// Bounded streaming grammar check for optional sidecar data. No token copies or heap allocation.
// StreamingJsonParser deliberately tolerates malformed punctuation, so callers that need a
// complete document use this guard before accepting its extracted values.
class JsonSyntaxGuard {
 public:
  void feed(const char* text, size_t length) {
    for (size_t i = 0; i < length && !error; ++i) consume(text[i]);
  }
  bool complete() const { return !error && depth == 0 && rootDone && lex == Idle; }

 private:
  enum Expect : uint8_t { ObjectFirst, ObjectKey, Colon, ObjectValue, ObjectNext,
                          ArrayFirst, ArrayValue, ArrayNext };
  enum Lex : uint8_t { Idle, String, Escape, Unicode, Number, Literal } lex = Idle;
  Expect stack[32]{};
  uint8_t depth = 0, unicodeLeft = 0, numberState = 0, literalPos = 0;
  uint16_t tokenBytes = 0;
  const char* literal = nullptr;
  bool error = false, rootDone = false, key = false;
  static bool white(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  void valueDone() {
    if (!depth) rootDone = true;
    else if (stack[depth - 1] == ObjectValue) stack[depth - 1] = ObjectNext;
    else stack[depth - 1] = ArrayNext;
  }
  void consume(char c) {
    // Do not let a silently skipped oversized SAX token leave a previous value in effect.
    if (lex != Idle && ++tokenBytes >= 512) { error = true; return; }
    if (lex == Unicode) {
      if (!digit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) error = true;
      if (!--unicodeLeft) lex = String;
      return;
    }
    if (lex == Escape) {
      if (c == 'u') { unicodeLeft = 4; lex = Unicode; }
      else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') lex = String;
      else error = true;
      return;
    }
    if (lex == String) {
      if (static_cast<uint8_t>(c) < 32) error = true;
      else if (c == '\\') lex = Escape;
      else if (c == '"') {
        lex = Idle;
        if (key) stack[depth - 1] = Colon;
        else valueDone();
      }
      return;
    }
    if (lex == Literal) {
      if (c != literal[literalPos++]) error = true;
      else if (!literal[literalPos]) { lex = Idle; valueDone(); }
      return;
    }
    if (lex == Number) {
      // minus, zero, integer, decimal point, fraction, exponent, exponent sign, exponent digits.
      if (numberState == 0 && digit(c)) { numberState = c == '0' ? 1 : 2; return; }
      if ((numberState == 2 || numberState == 4 || numberState == 7) && digit(c)) return;
      if ((numberState == 1 || numberState == 2) && c == '.') { numberState = 3; return; }
      if (numberState == 3 && digit(c)) { numberState = 4; return; }
      if ((numberState == 1 || numberState == 2 || numberState == 4) && (c == 'e' || c == 'E')) { numberState = 5; return; }
      if (numberState == 5 && (c == '+' || c == '-')) { numberState = 6; return; }
      if ((numberState == 5 || numberState == 6) && digit(c)) { numberState = 7; return; }
      if (numberState != 1 && numberState != 2 && numberState != 4 && numberState != 7) { error = true; return; }
      lex = Idle;
      valueDone();
      // Consume the delimiter below, without recursion.
    }
    if (white(c)) return;
    if (!depth && rootDone) { error = true; return; }
    if (depth) {
      const auto expected = stack[depth - 1];
      if ((c == '}' && (expected == ObjectFirst || expected == ObjectNext)) ||
          (c == ']' && (expected == ArrayFirst || expected == ArrayNext))) {
        --depth; valueDone(); return;
      }
      if (expected == Colon) {
        if (c != ':') error = true;
        else stack[depth - 1] = ObjectValue;
        return;
      }
      if (expected == ObjectNext || expected == ArrayNext) {
        if (c != ',') error = true;
        else stack[depth - 1] = expected == ObjectNext ? ObjectKey : ArrayValue;
        return;
      }
      if (expected == ObjectFirst || expected == ObjectKey) {
        if (c != '"') error = true;
        else { key = true; lex = String; tokenBytes = 0; }
        return;
      }
    }
    key = false;
    tokenBytes = 0;
    if (c == '{' || c == '[') {
      if (depth == sizeof(stack)) error = true;
      else stack[depth++] = c == '{' ? ObjectFirst : ArrayFirst;
    } else if (c == '"') lex = String;
    else if (c == '-' || digit(c)) { lex = Number; numberState = c == '-' ? 0 : c == '0' ? 1 : 2; }
    else if (c == 't' || c == 'f' || c == 'n') {
      literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
      literalPos = 1; lex = Literal;
    } else error = true;
  }
};
