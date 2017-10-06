#include <iostream>

using namespace std;

int main(int argc, char* argv[])
{
  if (argc > 0) {
    cerr << "Usage: " << argv[0] << endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
