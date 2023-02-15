// basic file operations
#include <iostream>
#include <fstream>
using namespace std;

int main () {
    string path = "/tmp/test.txt";
    ifstream file(path,ios::in);
    cout << "Opening: " << path << "\n";
 

    string buf(1000, '\0');
    while (file.read(&buf[0], 1000)) {
        cout << "file contents " << buf << endl;
    }
  return 0;
}