
import logging
import socket
import time

logger = logging.getLogger(__name__)

HOST = "localhost"
PORT = 8080
BUFFER_SIZE = 1024

def main():
    """
    <METHOD> <PATH> HTTP/1.1\r\n
    Header-Name: value\r\n
    \r\n
    <optional body>
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        logger.info("Connected s to %s:%s", HOST, PORT)
        req = "GET /minimal HTTP/1.1\r\nTest-Header: Wohoop\r\n\r\nHelloooo this is body!"

        s.sendall(req.encode())

        data = s.recv(BUFFER_SIZE)
        logger.info("Received: %s", data.decode())
        time.sleep(5)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()
