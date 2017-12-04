#include "config.h"
#ifdef HAVE_FILESYSTEM
#include <filesystem>
namespace fs = std::filesystem;
#elif HAVE_EXPERIMENTAL_FILESYSTEM
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
