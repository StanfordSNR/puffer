#ifndef TV_ENCODER_ARGPARSER_HH
#define TV_ENCODER_ARGPARSER_HH

#include <string>
#include <set>
#include <map>
#include <sstream>

class ArgParser
{
private:
  std::string name_;
  std::set<std::string> arguments_;
  std::set<std::string> required_args_;
  std::map<std::string, std::string> arg_values_;
  std::map<std::string, std::string> arg_helps_;

public:
  ArgParser(const std::string &name);
  ~ArgParser();
  bool parse(int argc, const char* argv[]);
  template<typename T> T get_arg_value(const std::string &arg,
      const T &default_value)
  {
    if (this->arguments_.find(arg) == this->arguments_.end())
      return default_value;
    else {
      if (this->arg_values_.find(arg) == this->arg_values_.end())
        return default_value;
      else {
        std::string arg_value = this->arg_values_[arg];
        std::stringstream  stream(arg_value);
        // try to convert type
        T value;
        stream >> value;
        if (stream.fail())
          return default_value;
        return value;
      }
    }
  }
  bool add_argument(const std::string &arg, const std::string &desc);
  bool add_argument(const std::string &arg, const std::string &desc, bool required);
  void print_help();
};

#endif /* TV_ENCODER_ARGPARSER_HH */
