#include "toml_lines.h"

#include <cctype>
#include <climits>
#include <stdexcept>

namespace champi
{
namespace
{
class Parser
{
  public:
    Parser(std::string_view text, const std::string& source, const std::string& what,
           const std::string& tables)
        : text_(text), source_(source), what_(what), tables_(tables)
    {
    }

    std::vector<TomlLine> Parse()
    {
        std::vector<TomlLine> lines;
        for(;;)
        {
            SkipBlank(true);
            if(AtEnd())
                return lines;
            if(Peek() == '[')
                Fail(tables_);
            TomlLine line;
            line.line = line_;
            line.name = BareKey();
            SkipBlank(false);
            Expect('=');
            SkipBlank(false);
            if(Peek() == '[')
            {
                line.is_array = true;
                Next();
                for(;;)
                {
                    SkipBlank(true);
                    if(Peek() == ']')
                        break;
                    line.values.push_back(Value());
                    SkipBlank(true);
                    if(Peek() != ',')
                        break;
                    Next();
                }
                SkipBlank(true);
                Expect(']');
            }
            else
                line.values.push_back(Value());
            SkipBlank(false);
            if(!AtEnd() && Peek() != '\n')
                Fail("expected the end of the line");
            lines.push_back(std::move(line));
        }
    }

  private:
    [[noreturn]] void Fail(const std::string& what) const { TomlFail(source_, line_, what); }

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }
    char Next()
    {
        const char c = text_[pos_++];
        if(c == '\n')
            line_++;
        return c;
    }

    void Expect(char c)
    {
        if(Peek() != c)
            Fail(std::string("expected '") + c + "'");
        Next();
    }

    // Spaces, tabs and comments, and newlines too if `newlines`.
    void SkipBlank(bool newlines)
    {
        while(!AtEnd())
        {
            const char c = Peek();
            if(c == '#')
                while(!AtEnd() && Peek() != '\n')
                    Next();
            else if(c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n'))
                Next();
            else
                return;
        }
    }

    std::string BareKey()
    {
        std::string name;
        while(!AtEnd() && (std::isalnum((unsigned char)Peek()) || Peek() == '_' || Peek() == '-'))
            name += Next();
        if(name.empty())
            Fail("expected a name");
        return name;
    }

    TomlValue Value()
    {
        TomlValue   v;
        v.line       = line_;
        const char c = Peek();
        if(c == '"' || c == '\'')
        {
            // A basic string ("...") knows \" and \\; a literal one ('...') has no escapes.
            v.is_string = true;
            Next();
            while(!AtEnd() && Peek() != c && Peek() != '\n')
            {
                if(c == '"' && Peek() == '\\')
                {
                    Next();
                    if(Peek() != '"' && Peek() != '\\')
                        Fail("only \\\" and \\\\ can be escaped");
                }
                v.text += Next();
            }
            Expect(c);
            return v;
        }
        if(std::isdigit((unsigned char)c))
        {
            while(std::isdigit((unsigned char)Peek()))
            {
                const int d = Next() - '0';
                v.number    = v.number > (LONG_MAX - d) / 10 ? LONG_MAX : v.number * 10 + d;
            }
            return v;
        }
        Fail("expected " + what_ + ", or a [list] of them");
    }

    std::string_view   text_;
    const std::string& source_;
    const std::string& what_;
    const std::string& tables_;
    size_t             pos_  = 0;
    int                line_ = 1;
};
} // namespace

std::vector<TomlLine> ParseTomlLines(std::string_view text, const std::string& source,
                                     const std::string& what, const std::string& tables)
{
    return Parser(text, source, what, tables).Parse();
}

void TomlFail(const std::string& source, int line, const std::string& what)
{
    throw std::runtime_error(source + ":" + std::to_string(line) + ": " + what);
}

std::string TomlQuote(std::string_view s)
{
    std::string out = "\"";
    for(char c : s)
    {
        if(c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace champi
