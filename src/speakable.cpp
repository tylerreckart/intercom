#include "intercom/speakable.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>

namespace intercom {
namespace {

bool is_alnum(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool is_alpha(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool is_digit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool is_space(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool starts_with(std::string_view s, std::size_t i, std::string_view p) {
  return i + p.size() <= s.size() && s.substr(i, p.size()) == p;
}

bool ordinal_suffix_at(std::string_view s, std::size_t i) {
  if (i + 2 > s.size()) return false;
  const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 1])));
  if (!((a == 's' && b == 't') || (a == 'n' && b == 'd') || (a == 'r' && b == 'd') ||
        (a == 't' && b == 'h'))) {
    return false;
  }
  return i + 2 == s.size() || !is_alpha(s[i + 2]);
}

const char* expected_ordinal_suffix(int n) {
  const int rem100 = n % 100;
  if (rem100 >= 11 && rem100 <= 13) return "th";
  switch (n % 10) {
    case 1:
      return "st";
    case 2:
      return "nd";
    case 3:
      return "rd";
    default:
      return "th";
  }
}

std::string cardinal_under_100(int n);
std::string ordinal_under_100(int n);

std::string unit_cardinal(int n) {
  static const char* const k[] = {"zero",    "one",     "two",       "three",    "four",
                                  "five",    "six",     "seven",     "eight",    "nine",
                                  "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                  "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  return k[n];
}

std::string unit_ordinal(int n) {
  static const char* const k[] = {
      "zeroth",     "first",     "second",      "third",      "fourth",
      "fifth",      "sixth",     "seventh",     "eighth",     "ninth",
      "tenth",      "eleventh",  "twelfth",     "thirteenth", "fourteenth",
      "fifteenth",  "sixteenth", "seventeenth", "eighteenth", "nineteenth"};
  return k[n];
}

std::string tens_cardinal(int n) {
  static const char* const k[] = {"",       "",      "twenty",  "thirty", "forty",
                                  "fifty",  "sixty", "seventy", "eighty", "ninety"};
  return k[n];
}

std::string tens_ordinal(int n) {
  static const char* const k[] = {"",          "",         "twentieth", "thirtieth", "fortieth",
                                  "fiftieth",  "sixtieth", "seventieth", "eightieth", "ninetieth"};
  return k[n];
}

std::string cardinal_under_100(int n) {
  if (n < 20) return unit_cardinal(n);
  std::string s = tens_cardinal(n / 10);
  if (n % 10) {
    s.push_back(' ');
    s += unit_cardinal(n % 10);
  }
  return s;
}

std::string ordinal_under_100(int n) {
  if (n < 20) return unit_ordinal(n);
  if (n % 10 == 0) return tens_ordinal(n / 10);
  return tens_cardinal(n / 10) + " " + unit_ordinal(n % 10);
}

std::string cardinal_under_1000(int n) {
  if (n < 100) return cardinal_under_100(n);
  std::string s = unit_cardinal(n / 100) + " hundred";
  if (n % 100) {
    s += " and ";
    s += cardinal_under_100(n % 100);
  }
  return s;
}

std::string ordinal_words(int n) {
  if (n < 0 || n > 99999) return {};
  if (n < 100) return ordinal_under_100(n);
  if (n < 1000) {
    if (n % 100 == 0) return unit_cardinal(n / 100) + " hundredth";
    return unit_cardinal(n / 100) + " hundred and " + ordinal_under_100(n % 100);
  }
  const int thousands = n / 1000;
  const int rem = n % 1000;
  std::string s = cardinal_under_1000(thousands) + " thousand";
  if (rem == 0) {
    s += "th";
    return s;
  }
  s += " ";
  if (rem < 100) s += "and ";
  if (rem < 100) s += ordinal_under_100(rem);
  else if (rem % 100 == 0) s += unit_cardinal(rem / 100) + " hundredth";
  else s += unit_cardinal(rem / 100) + " hundred and " + ordinal_under_100(rem % 100);
  return s;
}

void skip_space(std::string_view s, std::size_t& i) {
  while (i < s.size() && is_space(s[i])) ++i;
}

// If s[i] is '{', return inner text and index just after the matching '}'.
bool take_brace(std::string_view s, std::size_t& i, std::string* inner) {
  if (i >= s.size() || s[i] != '{') return false;
  int depth = 0;
  const std::size_t start = i + 1;
  for (std::size_t j = i; j < s.size(); ++j) {
    if (s[j] == '{') ++depth;
    else if (s[j] == '}') {
      --depth;
      if (depth == 0) {
        *inner = std::string(s.substr(start, j - start));
        i = j + 1;
        return true;
      }
    }
  }
  return false;
}

std::string collapse_ws(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  bool prev_space = true;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (is_space(c)) {
      if (!prev_space) {
        out.push_back(' ');
        prev_space = true;
      }
    } else {
      out.push_back(c);
      prev_space = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

void append_spaced(std::string* out, std::string_view word) {
  if (word.empty()) return;
  if (!out->empty() && !is_space(out->back()) && word.front() != ' ' &&
      word.front() != '.' && word.front() != ',' && word.front() != '?' &&
      word.front() != '!' && word.front() != ';' && word.front() != ':') {
    out->push_back(' ');
  }
  out->append(word);
}

const std::unordered_map<std::string, std::string>& latex_words() {
  static const std::unordered_map<std::string, std::string> k{
      {"times", "times"},
      {"cdot", "times"},
      {"ast", "times"},
      {"star", "times"},
      {"div", "divided by"},
      {"pm", "plus or minus"},
      {"mp", "minus or plus"},
      {"leq", "less than or equal to"},
      {"le", "less than or equal to"},
      {"geq", "greater than or equal to"},
      {"ge", "greater than or equal to"},
      {"neq", "not equal to"},
      {"ne", "not equal to"},
      {"approx", "approximately"},
      {"equiv", "equivalent to"},
      {"sim", "similar to"},
      {"infty", "infinity"},
      {"pi", "pi"},
      {"theta", "theta"},
      {"alpha", "alpha"},
      {"beta", "beta"},
      {"gamma", "gamma"},
      {"delta", "delta"},
      {"Delta", "delta"},
      {"lambda", "lambda"},
      {"mu", "mu"},
      {"sigma", "sigma"},
      {"omega", "omega"},
      {"sum", "the sum of"},
      {"prod", "the product of"},
      {"int", "the integral of"},
      {"partial", "partial"},
      {"infty", "infinity"},
      {"sin", "sine"},
      {"cos", "cosine"},
      {"tan", "tangent"},
      {"cot", "cotangent"},
      {"sec", "secant"},
      {"csc", "cosecant"},
      {"log", "log"},
      {"ln", "natural log"},
      {"exp", "e to the"},
      {"min", "min"},
      {"max", "max"},
      {"lim", "the limit of"},
      {"to", "to"},
      {"rightarrow", "to"},
      {"leftarrow", "from"},
      {"Rightarrow", "implies"},
      {"cdotp", "times"},
      {"ldots", "dot dot dot"},
      {"cdots", "dot dot dot"},
      {"dots", "dot dot dot"},
      {"circ", "degrees"},
      {"deg", "degrees"},
      {",", " "},
      {";", " "},
      {"!", " "},
      {" ", " "},
      {"left", ""},
      {"right", ""},
      {"big", ""},
      {"Big", ""},
      {"bigg", ""},
      {"Bigg", ""},
      {"bigl", ""},
      {"bigr", ""},
      {"displaystyle", ""},
      {"textstyle", ""},
      {"scriptstyle", ""},
      {"quad", ""},
      {"qquad", ""},
  };
  return k;
}

std::string expand_math(std::string_view s);

std::string expand_scripts(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] == '^') {
      ++i;
      skip_space(s, i);
      std::string exp;
      if (i < s.size() && s[i] == '{') {
        if (!take_brace(s, i, &exp)) {
          out.push_back('^');
          continue;
        }
        exp = expand_math(exp);
      } else if (i < s.size() && s[i] == '\\') {
        std::size_t j = i + 1;
        if (j < s.size() && is_alpha(s[j])) {
          while (j < s.size() && is_alpha(s[j])) ++j;
        } else if (j < s.size()) {
          ++j;
        }
        exp = expand_math(s.substr(i, j - i));
        i = j;
      } else if (i < s.size()) {
        exp.assign(1, s[i]);
        ++i;
        exp = expand_math(exp);
      }
      const std::string t = collapse_ws(exp);
      if (t == "2") append_spaced(&out, "squared");
      else if (t == "3") append_spaced(&out, "cubed");
      else if (t == "1") append_spaced(&out, "to the first");
      else if (!t.empty()) {
        append_spaced(&out, "to the");
        append_spaced(&out, t);
      }
      continue;
    }
    if (s[i] == '_') {
      ++i;
      skip_space(s, i);
      std::string sub;
      if (i < s.size() && s[i] == '{') {
        if (!take_brace(s, i, &sub)) {
          out.push_back('_');
          continue;
        }
      } else if (i < s.size()) {
        sub.assign(1, s[i]);
        ++i;
      }
      sub = collapse_ws(expand_math(sub));
      if (!sub.empty()) append_spaced(&out, sub);
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

std::string expand_math(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 32);
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] == '\\') {
      if (i + 1 >= s.size()) break;
      const char n = s[i + 1];
      if (n == '\\') {
        append_spaced(&out, " ");
        i += 2;
        continue;
      }
      if (!is_alpha(n)) {
        // Markdown/LaTeX escape: \* \_ \$ \{ etc. Keep the character, never
        // the word "backslash".
        out.push_back(n);
        i += 2;
        continue;
      }
      std::size_t j = i + 1;
      while (j < s.size() && is_alpha(s[j])) ++j;
      const std::string cmd(s.substr(i + 1, j - (i + 1)));
      i = j;
      skip_space(s, i);

      if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac") {
        std::string num, den;
        skip_space(s, i);
        if (!take_brace(s, i, &num)) continue;
        skip_space(s, i);
        if (!take_brace(s, i, &den)) continue;
        append_spaced(&out, expand_math(num));
        append_spaced(&out, "over");
        append_spaced(&out, expand_math(den));
        continue;
      }
      if (cmd == "sqrt") {
        std::string nroot;
        if (i < s.size() && s[i] == '[') {
          const auto close = s.find(']', i + 1);
          if (close != std::string_view::npos) {
            nroot = std::string(s.substr(i + 1, close - (i + 1)));
            i = close + 1;
          }
        }
        skip_space(s, i);
        std::string inner;
        if (i < s.size() && s[i] == '{') {
          if (!take_brace(s, i, &inner)) continue;
        } else if (i < s.size()) {
          inner.assign(1, s[i]);
          ++i;
        }
        if (!nroot.empty() && nroot != "2") {
          append_spaced(&out, "the");
          append_spaced(&out, expand_math(nroot));
          append_spaced(&out, "root of");
        } else {
          append_spaced(&out, "the square root of");
        }
        append_spaced(&out, expand_math(inner));
        continue;
      }
      if (cmd == "text" || cmd == "mathrm" || cmd == "mathbf" || cmd == "mathit" ||
          cmd == "mathsf" || cmd == "mathtt" || cmd == "operatorname" ||
          cmd == "textrm" || cmd == "textbf" || cmd == "textit" || cmd == "emph" ||
          cmd == "overline" || cmd == "underline" || cmd == "hat" || cmd == "bar" ||
          cmd == "vec" || cmd == "tilde" || cmd == "dot" || cmd == "ddot" ||
          cmd == "mathcal" || cmd == "mathbb" || cmd == "boxed") {
        std::string inner;
        if (take_brace(s, i, &inner)) {
          append_spaced(&out, expand_math(inner));
        }
        continue;
      }
      if (cmd == "left" || cmd == "right" || cmd == "big" || cmd == "Big" ||
          cmd == "biggl" || cmd == "biggr") {
        if (i < s.size() && s[i] == '.') ++i;
        continue;
      }

      auto it = latex_words().find(cmd);
      if (it != latex_words().end()) {
        if (!it->second.empty()) append_spaced(&out, it->second);
        continue;
      }
      // Unknown \command → speak the name, not "backslash".
      append_spaced(&out, cmd);
      continue;
    }

    if (s[i] == '{') {
      std::string inner;
      if (take_brace(s, i, &inner)) {
        append_spaced(&out, expand_math(inner));
        continue;
      }
    }
    if (s[i] == '&') {
      out.push_back(' ');
      ++i;
      continue;
    }

    if (!out.empty() && is_digit(out.back()) && is_alpha(s[i]) &&
        !ordinal_suffix_at(s, i)) {
      out.push_back(' ');
    } else if (!out.empty() && is_alpha(out.back()) && is_digit(s[i])) {
      out.push_back(' ');
    }
    out.push_back(s[i]);
    ++i;
  }
  return expand_scripts(out);
}

std::string take_math_until(std::string_view s, std::size_t& i, std::string_view close) {
  const auto pos = s.find(close, i);
  if (pos == std::string_view::npos) {
    std::string inner(s.substr(i));
    i = s.size();
    return inner;
  }
  std::string inner(s.substr(i, pos - i));
  i = pos + close.size();
  return inner;
}

std::string strip_math_delimiters(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (starts_with(s, i, "$$")) {
      i += 2;
      append_spaced(&out, expand_math(take_math_until(s, i, "$$")));
      continue;
    }
    if (starts_with(s, i, "\\[")) {
      i += 2;
      append_spaced(&out, expand_math(take_math_until(s, i, "\\]")));
      continue;
    }
    if (starts_with(s, i, "\\(")) {
      i += 2;
      append_spaced(&out, expand_math(take_math_until(s, i, "\\)")));
      continue;
    }
    if (s[i] == '$') {
      ++i;
      append_spaced(&out, expand_math(take_math_until(s, i, "$")));
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

std::string strip_fences(std::string s) {
  for (;;) {
    const auto open = s.find("```");
    if (open == std::string::npos) break;
    auto close = s.find("```", open + 3);
    if (close == std::string::npos) {
      s.erase(open);
      break;
    }
    s.erase(open, (close + 3) - open);
    s.insert(open, " ");
  }
  return s;
}

std::string strip_markdown(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (starts_with(s, i, "![") || (s[i] == '[' && !(i > 0 && s[i - 1] == '!'))) {
      const bool img = starts_with(s, i, "![");
      const std::size_t lb = img ? i + 1 : i;
      if (s[lb] == '[') {
        const auto rb = s.find(']', lb + 1);
        if (rb != std::string_view::npos && rb + 1 < s.size() && s[rb + 1] == '(') {
          const auto rp = s.find(')', rb + 2);
          if (rp != std::string_view::npos) {
            out.append(s.substr(lb + 1, rb - (lb + 1)));
            i = rp + 1;
            continue;
          }
        }
      }
    }
    if (starts_with(s, i, "**") || starts_with(s, i, "__")) {
      const auto mark = s.substr(i, 2);
      const auto end = s.find(mark, i + 2);
      if (end != std::string_view::npos && end < i + 80) {
        out.append(s.substr(i + 2, end - (i + 2)));
        i = end + 2;
        continue;
      }
    }
    if (s[i] == '`' ) {
      const auto end = s.find('`', i + 1);
      if (end != std::string_view::npos) {
        out.append(s.substr(i + 1, end - (i + 1)));
        i = end + 1;
        continue;
      }
    }
    // *italic* only for letter runs, so "2 * 3" stays for the symbol pass.
    if (s[i] == '*' && i + 1 < s.size() && is_alpha(s[i + 1])) {
      const auto end = s.find('*', i + 1);
      if (end != std::string_view::npos && end > i + 1 && end < i + 60 &&
          is_alpha(s[end - 1])) {
        out.append(s.substr(i + 1, end - (i + 1)));
        i = end + 1;
        continue;
      }
    }
    if (s[i] == '#' && (i == 0 || s[i - 1] == '\n')) {
      while (i < s.size() && s[i] == '#') ++i;
      while (i < s.size() && s[i] == ' ') ++i;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

std::string speak_symbols(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 32);

  bool after_word = false;
  auto emit_word = [&](std::string_view w) {
    append_spaced(&out, w);
    after_word = true;
  };

  auto last_non_space = [&]() -> char {
    for (std::size_t k = out.size(); k > 0; --k) {
      if (!is_space(out[k - 1])) return out[k - 1];
    }
    return '\0';
  };

  auto push_raw = [&](char c) {
    if (after_word && !is_space(c) && c != '.' && c != ',' && c != '?' &&
        c != '!' && c != ';' && c != ':' && c != '\'' && c != '"') {
      if (!out.empty() && !is_space(out.back())) out.push_back(' ');
    }
    if (!out.empty() && !is_space(out.back()) && is_alpha(out.back()) && is_digit(c)) {
      out.push_back(' ');
    }
    out.push_back(c);
    if (!is_space(c)) after_word = false;
  };

  std::size_t i = 0;
  auto eat = [&](std::string_view p) {
    if (!starts_with(s, i, p)) return false;
    i += p.size();
    return true;
  };

  while (i < s.size()) {
    if (eat("<=") || eat("≤")) {
      emit_word("less than or equal to");
      continue;
    }
    if (eat(">=") || eat("≥")) {
      emit_word("greater than or equal to");
      continue;
    }
    if (eat("!=") || eat("≠")) {
      emit_word("not equal to");
      continue;
    }
    if (eat("==")) {
      emit_word("equals");
      continue;
    }
    if (eat("->") || eat("→")) {
      emit_word("to");
      continue;
    }
    if (eat("=>") || eat("⇒")) {
      emit_word("implies");
      continue;
    }
    if (eat("+/-") || eat("±")) {
      emit_word("plus or minus");
      continue;
    }
    if (eat("×") || eat("·")) {
      emit_word("times");
      continue;
    }
    if (eat("÷")) {
      emit_word("divided by");
      continue;
    }
    if (eat("≈")) {
      emit_word("approximately");
      continue;
    }
    if (eat("∞")) {
      emit_word("infinity");
      continue;
    }
    if (eat("π")) {
      emit_word("pi");
      continue;
    }
    if (eat("...")) {
      if (last_non_space() != ',' && last_non_space() != '.') push_raw(',');
      continue;
    }
    if (eat("√")) {
      emit_word("the square root of");
      continue;
    }
    if (eat("²")) {
      emit_word("squared");
      continue;
    }
    if (eat("³")) {
      emit_word("cubed");
      continue;
    }

    if (s[i] == '=') {
      emit_word("equals");
      ++i;
      continue;
    }
    if (s[i] == '+') {
      emit_word("plus");
      ++i;
      continue;
    }
    if (s[i] == '%') {
      emit_word("percent");
      ++i;
      continue;
    }
    if (s[i] == '<') {
      emit_word("less than");
      ++i;
      continue;
    }
    if (s[i] == '>') {
      emit_word("greater than");
      ++i;
      continue;
    }

    if (s[i] == '/') {
      const bool left = !out.empty() && is_alnum(out.back());
      const bool right = i + 1 < s.size() && is_alnum(s[i + 1]);
      if (left && right) emit_word("over");
      else push_raw(' ');
      ++i;
      continue;
    }

    if (s[i] == '*') {
      const bool bullet =
          (i == 0 || s[i - 1] == '\n') && (i + 1 >= s.size() || is_space(s[i + 1]));
      if (!bullet) emit_word("times");
      ++i;
      continue;
    }

    if (s[i] == '^') {
      emit_word("to the");
      ++i;
      continue;
    }

    // Unicode minus should be spoken; em/en dashes are pauses, not "minus".
    if (eat("\xE2\x88\x92")) {
      emit_word("minus");
      continue;
    }
    if (eat("\xE2\x80\x94") || eat("\xE2\x80\x93") || eat("---") || eat("--")) {
      if (last_non_space() != ',' && last_non_space() != '.' && last_non_space() != '?' &&
          last_non_space() != '!' && last_non_space() != ';' && last_non_space() != ':') {
        push_raw(',');
      }
      continue;
    }

    // Hyphen: compounds and ranges stay natural; only spaced arithmetic is "minus".
    if (s[i] == '-') {
      if (i + 1 < s.size() && s[i + 1] == '-') {
        while (i < s.size() && s[i] == '-') ++i;
        if (last_non_space() != ',' && last_non_space() != '.') push_raw(',');
        continue;
      }
      std::size_t k = i + 1;
      skip_space(s, k);
      const bool spaced = k > i + 1;
      const bool next_num = k < s.size() && (is_digit(s[k]) || s[k] == '.');
      const bool next_alpha = k < s.size() && is_alpha(s[k]);
      const char prev = last_non_space();
      const bool prev_alnum = is_alnum(prev);
      const bool prev_alpha = is_alpha(prev);
      const bool prev_digit = is_digit(prev);
      const bool tight_alpha = i + 1 < s.size() && is_alpha(s[i + 1]);
      const bool tight_digit = i + 1 < s.size() && is_digit(s[i + 1]);

      if (!spaced && prev_alpha && (tight_alpha || tight_digit)) {
        push_raw(' ');
        ++i;
        continue;
      }
      if (!spaced && prev_digit && tight_digit) {
        emit_word("to");
        ++i;
        continue;
      }
      if (next_num && !prev_alnum) {
        emit_word("negative");
        i = k;
        continue;
      }
      if (spaced && (next_num || next_alpha) && prev_alnum) {
        emit_word("minus");
        ++i;
        continue;
      }
      if (spaced && next_alpha && !prev_alnum) {
        emit_word("minus");
        i = k;
        continue;
      }
      push_raw(' ');
      ++i;
      continue;
    }

    if (s[i] == '|') {
      const bool left = !out.empty() && is_alnum(out.back());
      const bool right = i + 1 < s.size() && is_alnum(s[i + 1]);
      if (left && right) emit_word("given");
      ++i;
      continue;
    }

    // Drop leftover markup so TTS never names it.
    if (s[i] == '\\' || s[i] == '{' || s[i] == '}' || s[i] == '_' || s[i] == '$' ||
        s[i] == '~' || s[i] == '`' || s[i] == '#') {
      ++i;
      continue;
    }

    push_raw(s[i]);
    ++i;
  }
  return out;
}

std::string expand_ordinals(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (std::size_t i = 0; i < s.size();) {
    if (is_digit(s[i]) && (i == 0 || !is_alnum(s[i - 1]))) {
      std::size_t j = i;
      while (j < s.size() && is_digit(s[j])) ++j;
      if (j > i && j - i <= 5) {
        int n = 0;
        bool overflow = false;
        for (std::size_t p = i; p < j; ++p) {
          if (n > (99999 - (s[p] - '0')) / 10) {
            overflow = true;
            break;
          }
          n = n * 10 + (s[p] - '0');
        }
        std::size_t k = j;
        skip_space(s, k);
        if (!overflow && ordinal_suffix_at(s, k)) {
          const char a =
              static_cast<char>(std::tolower(static_cast<unsigned char>(s[k])));
          const char b =
              static_cast<char>(std::tolower(static_cast<unsigned char>(s[k + 1])));
          const char* want = expected_ordinal_suffix(n);
          if (a == want[0] && b == want[1]) {
            const std::string words = ordinal_words(n);
            if (!words.empty()) {
              append_spaced(&out, words);
              i = k + 2;
              continue;
            }
          }
        }
      }
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

std::string ensure_spoken_stop(std::string s) {
  while (!s.empty() && is_space(s.back())) s.pop_back();
  if (s.empty()) return s;
  const char last = s.back();
  if (last != '.' && last != '!' && last != '?' && last != ',' && last != ';' &&
      last != ':') {
    s.push_back('.');
  }
  return s;
}

}  // namespace

std::string to_speakable(std::string_view text) {
  std::string s(text);
  s = strip_fences(std::move(s));
  s = strip_math_delimiters(s);
  // Any remaining TeX commands (inline, no $).
  s = expand_math(s);
  s = strip_markdown(s);
  s = speak_symbols(s);
  s = expand_ordinals(s);
  s = collapse_ws(s);
  s = ensure_spoken_stop(std::move(s));
  return s;
}

}  // namespace intercom
