#include "formatter.hh"

using namespace std;

void Formatter::parse(const string & format_string)
{
  if (format_string.empty()) {
    throw runtime_error("format_string cannot be empty");
  }

  /* reset before parsing a new format string */
  reset();

  size_t pos = 0;

  while (pos < format_string.size()) {
    size_t lpos = format_string.find("{", pos);
    if (lpos == string::npos) {
      fields_.emplace_back(make_unique<Literal>(format_string.substr(pos)));
      break;
    }

    if (lpos > pos) {
      fields_.emplace_back(make_unique<Literal>(
                           format_string.substr(pos, lpos - pos)));
    }
    pos = lpos + 1;

    size_t rpos = format_string.find("}", pos);
    if (rpos == string::npos) {
      throw runtime_error("no matching }");
    }
    pos = rpos + 1;

    if (rpos - lpos == 1) {  // {}
      if (not auto_field_numbering_) {
        auto_field_numbering_ = true;
        auto_field_index_ = 0;
      } else if (not *auto_field_numbering_) {
        throw runtime_error("cannot switch from automatic field numbering "
                            "to manual field specification");
      }

      fields_.emplace_back(make_unique<Replacement>(*auto_field_index_));
      auto_field_index_ = *auto_field_index_ + 1;
    } else {  // {INDEX}
      if (not auto_field_numbering_) {
        auto_field_numbering_ = false;
      } else if (*auto_field_numbering_) {
        throw runtime_error("cannot switch from automatic field numbering "
                            "to manual field specification");
      }

      int index = stoi(format_string.substr(lpos + 1, rpos - lpos - 1));
      if (index < 0) {
        throw runtime_error("invalid negative index");
      }

      fields_.emplace_back(make_unique<Replacement>(index));
    }
  }
}

string Formatter::format(const vector<string> & values)
{
  string ret;

  for (const auto & field : fields_) {
    if (field->type == Type::literal) {
      ret += static_cast<Literal*>(field.get())->text;
    } else if (field->type == Type::replacement) {
      unsigned int index = static_cast<Replacement*>(field.get())->index;

      if (index >= values.size()) {
        throw runtime_error("index out of range");
      }

      ret += values.at(index);
    } else {
      throw runtime_error("invalid field type");
    }
  }

  return ret;
}

void Formatter::reset()
{
  fields_.clear();

  auto_field_numbering_.reset();
  auto_field_index_.reset();
}
