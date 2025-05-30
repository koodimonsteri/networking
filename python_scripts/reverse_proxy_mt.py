from uuid import uuid4
import logging
import time

logger = logging.getLogger(__name__)

import socket

#HOST = "127.0.0.1"
HOST = "localhost"
PORT = 9000
BUFFER_SIZE = 1024

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:

        p_uuid = uuid4()
        s.connect((HOST, PORT))
        logger.info("Connected s to %s:%s", HOST, PORT)
        while True:

            message = f"Hello reverse proxy from Python: {p_uuid}"
            s.sendall(message.encode())

            data = s.recv(BUFFER_SIZE)
            logger.info("Received: %s", data.decode())
            time.sleep(1)

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()

