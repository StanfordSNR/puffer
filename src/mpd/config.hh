#ifndef __CONFIG_HH
#define __CONFIG_HH

#include <string>
#include <map>
#include <vector>

class ConfigFile
{
public:
  ConfigFile(const std::string & filename);

  void get_sections(std::vector<std::string> & result);
  std::string get_value(const std::string & section, const std::string & name);

private:
  inline std::string ltrim(const std::string & s);
  inline std::string rtrim(const std::string & s);
  inline std::string trim(const std::string & s);

  std::map<std::string, std::map<std::string, std::string>> values_;
};

#endif
