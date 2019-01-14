AM_CPPFLAGS = -I$(srcdir)/../util $(CXX17_FLAGS) $(SSL_CFLAGS)
AM_CXXFLAGS = $(PICKY_CXXFLAGS) $(EXTRA_CXXFLAGS)

noinst_LIBRARIES = libnet.a

libnet_a_SOURCES = address.cc address.hh body_parser.hh \
                   chunked_parser.cc chunked_parser.hh \
                   http_header.cc http_header.hh \
                   http_message.cc http_message.hh \
                   http_message_sequence.hh http_request.cc http_request.hh \
                   http_request_parser.hh http_response.cc http_response.hh \
                   http_response_parser.cc http_response_parser.hh \
                   mime_type.cc mime_type.hh \
                   secure_socket.cc secure_socket.hh socket.cc socket.hh \
                   serialization.cc serialization.hh \
                   strict_conversions.hh strict_conversions.cc \
                   nb_secure_socket.hh nb_secure_socket.cc \
                   ws_frame.hh ws_frame.cc \
                   ws_message.hh ws_message.cc \
                   ws_message_parser.hh ws_message_parser.cc \
                   ws_server.hh ws_server.cc
