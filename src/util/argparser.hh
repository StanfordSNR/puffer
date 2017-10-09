#ifndef TV_ENCODER_ARGPARSER_HH
#define TV_ENCODER_ARGPARSER_HH

#include <string>
#include <set>
#include <map>
#include <sstream>

using namespace std;

class ArgParser
{
  private:
    string name_;
    set<string> arguments_;
    set<string> required_args_;
    map<string, string> arg_values_;
    map<string, string> arg_helps_;

  public:
    ArgParser(const string &name);
    ~ArgParser();
    bool parse(int argc, const char* argv[]);
    template<typename T> T get_arg_value(const string &arg, 
        const T &default_value)
    {
      if (this->arguments_.find(arg) == this->arguments_.end())
        return default_value;
      else {
        if (this->arg_values_.find(arg) == this->arg_values_.end())
          return default_value;
        else { 
          string arg_value = this->arg_values_[arg];
          stringstream  stream(arg_value);
          // try to convert type
          T value;
          stream >> value;
          if (stream.fail())
            return default_value;
          return value;
        }
      }
    }
    bool add_argument(const string &arg, const string &desc);
    bool add_argument(const string &arg, const string &desc, bool required);
    void print_help();
};

#endif //TV_ENCODER_ARGPARSER_HH
