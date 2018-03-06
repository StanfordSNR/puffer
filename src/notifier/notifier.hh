#ifndef NOTIFIER_HH
#define NOTIFIER_HH

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

#include "signalfd.hh"
#include "poller.hh"
#include "inotify.hh"
#include "child_process.hh"

class Notifier
{
public:
  Notifier(const std::string & src_dir,
           const std::string & src_ext,
           const std::optional<std::string> & dst_dir_opt,
           const std::optional<std::string> & dst_ext_opt,
           const std::optional<std::string> & tmp_dir_opt,
           const std::string & program,
           const std::vector<std::string> & prog_args);

  void process_existing_files();

  int loop();

private:
  std::string src_dir_, src_ext_;

  bool check_mode_;  /* true if both dst_dir_opt and dst_ext_opt exist */
  std::string dst_dir_, dst_ext_;

  std::string tmp_dir_;
  std::string program_;
  std::vector<std::string> prog_args_;

  ProcessManager process_manager_;
  Inotify inotify_;

  std::unordered_map<pid_t, std::string> prefixes_;

  /* helper functions */
  inline std::string get_src_path(const std::string & prefix);
  inline std::string get_dst_path(const std::string & prefix);
  inline std::string get_tmp_path(const std::string & prefix);

  void run_as_child(const std::string & prefix);
};

#endif /* NOTIFIER_HH */
