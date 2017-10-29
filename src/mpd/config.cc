#include <string>
#include <fstream>
#include <map>
#include <vector>
#include "path.hh"
#include "config.hh"

ConfigFile::ConfigFile(const std::string & filename): values_()
{
  if (!roost::exists(filename)) {
    throw std::runtime_error(filename + " does not exist");
  }

  // open the file and read one line by one line
  // improved from http://www.adp-gmbh.ch/cpp/config_file.html
  std::string line;
  std::string name;
  std::string value;
  std::string section;

  int pos;
  int line_count = 0;
  std::ifstream file(filename);
  while (std::getline(file, line)) {
    line_count++;
    trim(line); /* trim all the white space */
    if (!line.length()) continue;
    if (line[0] == '#') continue; /* comments */
    if (line[0] == ';') continue;

    /* section */
    if (line[0] == '[') {
      section = trim(line.substr(1, line.find(']') - 1));
      values_.insert(std::map<std::string, std::map<std::string, std::string>>
          ::value_type(section, std::map<std::string, std::string>()));
      continue;
    }

    if (!section.length())
      throw std::runtime_error("Missing section name at line:" +
          std::to_string(line_count));

    pos = line.find('=');
    name  = trim(line.substr(0, pos));
    value = trim(line.substr(pos + 1));

    auto map = values_[section];
    map.insert(std::map<std::string, std::string>::value_type(name, value));
  }

  file.close();
}

std::string ConfigFile::get_value(const std::string & section,
    const std::string & name)
{
  if (values_.find(section) == values_.end())
    return "";
  auto map = values_[section];
  if (map.find(name) == map.end())
    return "";
  else
    return map[name];
}


void ConfigFile::get_sections(std::vector<std::string> & result)
{
  /* use reference to avoid unnecessary copying */
  for(auto it : values_) {
    result.emplace_back(it.first);
  }
}

inline std::string ConfigFile::trim(const std::string & s)
{
  return s;
}
