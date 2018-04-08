#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <deque>
#include <map>

#include "socket.hh"
#include "file_descriptor.hh"
#include "epoller.hh"

using namespace std;

/* map client fd to socket instance */
map<int, shared_ptr<TCPSocket>> clients;

/* queue of keyboard input data */
deque<string> pending_data;

int main()
{
  /* create Epoller */
  auto epoller = make_shared<Epoller>();

  auto keyboard = make_shared<FileDescriptor>(STDIN_FILENO);
  epoller->register_fd(keyboard, EPOLLIN);
  epoller->set_callback(keyboard->fd_num(), EPOLLIN,
    [keyboard_weak = weak_ptr<FileDescriptor>(keyboard)]() {
      auto keyboard = keyboard_weak.lock();
      assert(keyboard);

      pending_data.emplace_back(keyboard->read());
      return 0;
    }
  );

  /* create TCP listening socket */
  auto listening_socket = make_shared<TCPSocket>();
  listening_socket->set_blocking(false);
  listening_socket->set_reuseaddr();
  listening_socket->bind(Address("0", 12345));
  listening_socket->listen();

  epoller->register_fd(listening_socket, EPOLLIN);
  epoller->set_callback(listening_socket->fd_num(), EPOLLIN,
    [epoller_weak = weak_ptr<Epoller>(epoller),
     listening_socket_weak = weak_ptr<TCPSocket>(listening_socket)]() {
      auto epoller = epoller_weak.lock();
      assert(epoller);

      auto listening_socket = listening_socket_weak.lock();
      assert(listening_socket);

      cerr << "Epoller " << epoller->fd_num()
           << ": EPOLLIN event from listening socket" << endl;

      auto client = make_shared<TCPSocket>(listening_socket->accept());
      clients.emplace(client->fd_num(), client);

      epoller->register_fd(client, EPOLLIN);
      epoller->set_callback(client->fd_num(), EPOLLIN,
        [epoller_weak = weak_ptr<Epoller>(epoller),
         client_weak = weak_ptr<TCPSocket>(client)]() {
          auto epoller = epoller_weak.lock();
          assert(epoller);

          auto client = client_weak.lock();
          assert(client);

          cerr << "Epoller " << epoller->fd_num()
               << ": EPOLLIN event from client " << client->fd_num() << endl;

          const string data = client->read();

          if (data.empty()) {
            clients.erase(client->fd_num());
          } else {
            cerr << "Client " << client->fd_num() << ": received " << data;

            if (not pending_data.empty()) {
              epoller->modify_events(client->fd_num(), EPOLLIN | EPOLLOUT);
            }
          }

          return 0;
        }
      );

      epoller->set_callback(client->fd_num(), EPOLLOUT,
        [epoller_weak = weak_ptr<Epoller>(epoller),
         client_weak = weak_ptr<TCPSocket>(client)]() {
          auto epoller = epoller_weak.lock();
          assert(epoller);

          auto client = client_weak.lock();
          assert(client);

          cerr << "Epoller " << epoller->fd_num()
               << ": EPOLLOUT event from client " << client->fd_num() << endl;

          client->write(pending_data.front());
          pending_data.pop_front();

          if (pending_data.empty()) {
            epoller->modify_events(client->fd_num(), EPOLLIN);
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
