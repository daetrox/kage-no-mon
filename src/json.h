// Parser JSON mínimo (lectura) para las respuestas de RetroAchievements.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct JsonValue
{
  enum class Type
  {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
  } type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JsonValue> arr;
  std::vector<std::pair<std::string, JsonValue>> obj; // conserva el orden

  const JsonValue& operator[](const std::string& key) const
  {
    static const JsonValue null;
    for (const auto& [k, v] : obj)
      if (k == key)
        return v;
    return null;
  }
  std::string AsString() const
  {
    if (type == Type::String)
      return str;
    if (type == Type::Number)
    {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.0f", num);
      return buf;
    }
    return {};
  }
  double AsNumber() const { return type == Type::Number ? num : (type == Type::String ? std::atof(str.c_str()) : 0); }
  bool IsNull() const { return type == Type::Null; }
};

class JsonParser
{
public:
  static bool Parse(const std::string& text, JsonValue* out)
  {
    JsonParser p(text);
    p.SkipWs();
    if (!p.ParseValue(out))
      return false;
    return true;
  }

private:
  explicit JsonParser(const std::string& t) : s(t) {}
  const std::string& s;
  size_t i = 0;

  void SkipWs()
  {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
      i++;
  }

  static void AppendUtf8(std::string& out, unsigned cp)
  {
    if (cp < 0x80)
      out += static_cast<char>(cp);
    else if (cp < 0x800)
    {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool ParseString(std::string* out)
  {
    if (s[i] != '"')
      return false;
    i++;
    while (i < s.size() && s[i] != '"')
    {
      char c = s[i++];
      if (c != '\\')
      {
        *out += c;
        continue;
      }
      if (i >= s.size())
        return false;
      char e = s[i++];
      switch (e)
      {
        case 'n': *out += '\n'; break;
        case 't': *out += '\t'; break;
        case 'r': *out += '\r'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'u':
        {
          if (i + 4 > s.size())
            return false;
          unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
          i += 4;
          // pares sustitutos UTF-16
          if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
          {
            unsigned lo = static_cast<unsigned>(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              i += 6;
            }
          }
          AppendUtf8(*out, cp);
          break;
        }
        default: *out += e; break;
      }
    }
    if (i >= s.size())
      return false;
    i++;
    return true;
  }

  bool ParseValue(JsonValue* v)
  {
    SkipWs();
    if (i >= s.size())
      return false;
    const char c = s[i];
    if (c == '{')
    {
      v->type = JsonValue::Type::Object;
      i++;
      SkipWs();
      if (i < s.size() && s[i] == '}')
      {
        i++;
        return true;
      }
      for (;;)
      {
        SkipWs();
        std::string key;
        if (!ParseString(&key))
          return false;
        SkipWs();
        if (i >= s.size() || s[i] != ':')
          return false;
        i++;
        JsonValue child;
        if (!ParseValue(&child))
          return false;
        v->obj.emplace_back(std::move(key), std::move(child));
        SkipWs();
        if (i < s.size() && s[i] == ',')
        {
          i++;
          continue;
        }
        if (i < s.size() && s[i] == '}')
        {
          i++;
          return true;
        }
        return false;
      }
    }
    if (c == '[')
    {
      v->type = JsonValue::Type::Array;
      i++;
      SkipWs();
      if (i < s.size() && s[i] == ']')
      {
        i++;
        return true;
      }
      for (;;)
      {
        JsonValue child;
        if (!ParseValue(&child))
          return false;
        v->arr.push_back(std::move(child));
        SkipWs();
        if (i < s.size() && s[i] == ',')
        {
          i++;
          continue;
        }
        if (i < s.size() && s[i] == ']')
        {
          i++;
          return true;
        }
        return false;
      }
    }
    if (c == '"')
    {
      v->type = JsonValue::Type::String;
      return ParseString(&v->str);
    }
    if (s.compare(i, 4, "true") == 0)
    {
      v->type = JsonValue::Type::Bool;
      v->b = true;
      i += 4;
      return true;
    }
    if (s.compare(i, 5, "false") == 0)
    {
      v->type = JsonValue::Type::Bool;
      i += 5;
      return true;
    }
    if (s.compare(i, 4, "null") == 0)
    {
      i += 4;
      return true;
    }
    char* end = nullptr;
    v->num = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i)
      return false;
    v->type = JsonValue::Type::Number;
    i = static_cast<size_t>(end - s.c_str());
    return true;
  }
};
