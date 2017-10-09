#include "argparser.hh"
#include <set>
#include <string>
#include <iostream>
#include <iomanip>

using namespace std;

ArgParser::ArgParser(const string &name) :
  name_(name), arguments_(set<string>()),  required_args_(set<string>()),
  arg_values_(map<string, string>()), arg_helps_(map<string, string>())
{}

ArgParser::~ArgParser()
{}

bool ArgParser::parse(int argc, const char **argv)
{
  int i = 0;
  while (++i < argc) {
    const char *arg = argv[ i ];
    if (this->arguments_.find(arg) != this->arguments_.end()) {
      if (i == argc - 1) {
        return false; // missing the last arg value
      }
      // we found one arg
      this->arg_values_[ arg ] = argv[ ++i ];
      this->required_args_.erase(arg);
    }
  }

  // if some required args is missing, an exception will be thrown
  if (!this->required_args_.empty()) {
    return false;
  }
  return true;
}

bool ArgParser::add_argument(const string &arg, const string &desc)
{
  return this->add_argument(arg, desc, false);
}

bool ArgParser::add_argument(const string &arg, const string &desc, bool required)
{
  if (this->arguments_.find(arg) != this->arguments_.end())
    return false;
  this->arguments_.insert(arg);
  this->arg_helps_[ arg ] = desc;

  if (required) {
    this->required_args_.insert(arg);
  }

  return true;
}

void ArgParser::print_help()
{
  cout << this->name_ << endl;
  for (auto it : this->arguments_) {
    cout << '\t' << left << setw(20) << setfill(' ') << it << ": " << this->arg_helps_[ it ] << endl;
  }
}
