# Puffer: Adaptive Live Streaming with Less Rebuffering

To use Puffer:

- Clone the repository

- Take a look at the .travis.yml file and install all dependencies

- Get a copy of the tos directory from Francis, and save it in media_server/tos 
(this is the media file, and can really be any media file but the example we use
is too large to be stored on GitHub)

- Execute the following commands from inside the puffer directory:

    $ ./fetch_submodules.sh
    
    $ ./autogen.sh
    
    $ ./configure
    
    $ make -j
    
    $ cd src/media-server
    
    $ ./run_ws_server.sh server_config.yml
    
- Visit 127.0.0.1:8080 using your web browser. The simple Python HTTP server will send a Javascript file wsplayer.js to the client telling it to connect the WebSocket media server on the 8081 port.

- To fake live streaming, run ../tests/fake_live.py tos tos-live, and remove the comment in front of  tos-live in server_config.yml. Then start running the media server and input y to the fake_live.py
    

