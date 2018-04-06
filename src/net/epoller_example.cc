#include <iostream>
#include <string>
#include <deque>
#include <map>

#include "socket.hh"
#include "file_descriptor.hh"
#include "epoller.hh"

using namespace std;

/* map client fd to socket instance */
map<int, TCPSocket> clients;

/* queue of keyboard input data */
deque<string> pending_data;

int main()
{
  /* create Epoller */
  auto epoller = make_shared<Epoller>();

  FileDescriptor keyboard(STDIN_FILENO);
  epoller->add_events(keyboard, EPOLLIN);
  epoller->set_callback(keyboard, EPOLLIN,
    [&keyboard]() {
      pending_data.emplace_back(keyboard.read());
      return 0;
    }
  );

  /* create TCP listening socket */
  TCPSocket listening_socket;
  listening_socket.set_blocking(false);
  listening_socket.set_reuseaddr();
  listening_socket.bind(Address("0", 12345));
  listening_socket.listen();

  epoller->add_events(listening_socket, EPOLLIN);
  epoller->set_callback(listening_socket, EPOLLIN,
    [epoller_weak_ptr = weak_ptr<Epoller>(epoller), &listening_socket]() {
      auto epoller = epoller_weak_ptr.lock();
      if (not epoller) {
        cerr << "Epoller is gone" << endl;
        return -1;
      }

      cerr << "EPOLLIN event from listening socket" << endl;

      TCPSocket client_orig = listening_socket.accept();
      int client_fd = client_orig.fd_num();
      clients.emplace(client_fd, move(client_orig));

      TCPSocket & client = clients.at(client_fd);
      epoller->add_events(client, EPOLLIN);
      epoller->set_callback(client, EPOLLIN,
        [epoller_weak_ptr = weak_ptr<Epoller>(epoller), client_fd]() {
          auto epoller = epoller_weak_ptr.lock();
          if (not epoller) {
            cerr << "Epoller is gone" << endl;
            return -1;
          }

          cerr << "EPOLLIN event from client " << client_fd << endl;

          TCPSocket & client = clients.at(client_fd);

          const string data = client.read();

          if (data.empty()) {
            epoller->deregister(client);
            clients.erase(client.fd_num());
          } else {
            cerr << client_fd << ": received " << data;

            if (not pending_data.empty()) {
              epoller->modify_events(client, EPOLLIN | EPOLLOUT);
            }
          }

          return 0;
        }
      );

      epoller->set_callback(client, EPOLLOUT,
        [epoller_weak_ptr = weak_ptr<Epoller>(epoller), client_fd]() {
          auto epoller = epoller_weak_ptr.lock();
          if (not epoller) {
            cerr << "Epoller is gone" << endl;
            return -1;
          }

          cerr << "EPOLLOUT event from client " << client_fd << endl;

          TCPSocket & client = clients.at(client_fd);

          client.write(pending_data.front());
          pending_data.pop_front();

          if (pending_data.empty()) {
            epoller->modify_events(client, EPOLLIN);
          }

          return 0;
        }
      );

      return 0;
    }
  );

  int cnt = 0;
  for (;;) {
    cout << "--- Round " << cnt++ << " ---" << endl;

    epoller->poll(-1);
  }
}
