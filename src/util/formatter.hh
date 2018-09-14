#ifndef FORMATTER_HH
#define FORMATTER_HH

#include <string>
#include <vector>
#include <optional>
#include <memory>

/* A Formatter similar to the one in Python 3. Currently supported formats:
 * '{}': simple positional formatting
 * '{INDEX}': explicit positional formatting with integer index */
class Formatter
{
public:
  void parse(const std::string & format_string);
  std::string format(const std::vector<std::string> & values);

  enum class Type {literal, replacement};

  struct Field {
    Type type;

    Field(const Type type_) : type(type_) {}
    virtual ~Field() {}
  };

  struct Literal : Field {
    std::string text;

    Literal(const std::string & text_)
      : Field(Type::literal), text(text_) {}
  };

  struct Replacement : Field {
    unsigned int index;

    Replacement(const unsigned int index_)
      : Field(Type::replacement), index(index_) {}
  };

private:
  std::vector<std::unique_ptr<Field>> fields_ {};

  std::optional<bool> auto_field_numbering_ {};
  std::optional<unsigned int> auto_field_index_ {};

  void reset();
};

#endif /* FORMATTER_HH */
