



import logging


logger = logging.getLogger(__name__)

import socket

#HOST = "127.0.0.1"
HOST = "localhost"
PORT = 9000
BUFFER_SIZE = 1024

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        logger.info("Connected to %s:%s", HOST, PORT)

        message = "Hello reverse proxy from Python!"
        s.sendall(message.encode())

        data = s.recv(BUFFER_SIZE)
        logger.info("Received: %s", data.decode())

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()

