#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include <system_error>
#include <pqxx/pqxx>

#include "filesystem.hh"
#include "tokenize.hh"
#include "exception.hh"

using namespace std;

void print_usage(const string & program_name)
{
  cerr <<
  "Usage: " << program_name << " <input_file> <pgdb_connection_string> <table>\n\n"
  "<input_file>               input file from notifier\n"
  "<pgdb_connection_string>   connection string for postgres db. E.g.,\n"
  "                           postgresql://[user[:password]@][netloc][:port][/dbname]\n"
  "<table>                    table to log event to\n"
  "<resolution>               video resolution\n"
  "<quality>                  crf"
  << endl;
}

inline string get_file_basename(const string & path) {
  vector<string> parts = split(path, "/");
  return parts[parts.size() - 1];
}

inline string create_table_stmt(const string &table_name) {
  ostringstream ss;
  ss << "CREATE TABLE IF NOT EXISTS " << table_name << " ("
    << "timestamp varchar(20) NOT NULL,"
    << "resolution varchar(20 NOT NULL,"
    << "crf integer NOT NULL,"
    << "ssim decimal NOT NULL,"
    << "created_at timestamp NOT NULL);";
  return ss.str();
}

inline string insert_stmt(const string &table_name, const string &input_file,
                          const string &resolution, const int crf,
                          const double ssim, const time_t created_at) {
  string basename = get_file_basename(input_file);
  string timestamp = split_filename(basename).first;
  ostringstream ss;
  ss << "INSERT INTO " << table_name
    << " (timestamp,resolution,crf,ssim, created_at) VALUES ("
    << "'" << timestamp << "','" << resolution << "'," << crf
    << "," << ssim << ",to_timestamp(" << created_at << "));";
  return ss.str();
}

double parse_ssim_file(const string &ssim_file) {
  ifstream ifs(ssim_file);
  stringstream ss;
  ss << ifs.rdbuf();
  return stod(ss.str());
}

int main(int argc, char * argv[])
{
  if (argc < 6) {
    print_usage("ssimreporter");
    return EXIT_FAILURE;
  }

  string input_file, pgdb_conn_string, table_name, resolution;
  input_file = argv[1];
  pgdb_conn_string = argv[2];
  table_name = argv[3];
  resolution = argv[4];
  int crf = stoi(argv[5]);

  struct stat t_stat;
  if (stat(input_file.c_str(), &t_stat) != 0) {
    cerr << "Could not stat " << input_file << endl;
    return EXIT_FAILURE;
  }

  time_t creation_time = t_stat.st_ctime;
  if (t_stat.st_size == 0) {
    cerr << "Ssim file has length 0" << endl;
    return EXIT_FAILURE;
  }

  double ssim = parse_ssim_file(input_file);

  /* Generate SQL statements */
  string sql = create_table_stmt(table_name) +
    insert_stmt(table_name, input_file, resolution, crf, ssim, creation_time);
  cerr << "SQL: " << sql << endl;

  /* Insert the new record into the database. Failure is non-fatal. */
  try {
    pqxx::connection db_conn(pgdb_conn_string);
    if (!db_conn.is_open()) {
      throw runtime_error("could not open db");
    }

    pqxx::work transaction(db_conn);
    transaction.exec(sql);
    transaction.commit();

    db_conn.disconnect();
  } catch (const exception &e){
    cerr << "Failed report ssim: " << e.what() << endl;
  }

  return EXIT_SUCCESS;
}
