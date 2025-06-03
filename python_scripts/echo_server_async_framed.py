from uuid import uuid4
import logging
import time
import socket
import struct

logger = logging.getLogger(__name__)


#HOST = "127.0.0.1"
HOST = "localhost"
PORT = 8080
BUFFER_SIZE = 1024


def recv_all(sock, n):
    data = b''
    while len(data) < n:
        packet = sock.recv(n - len(data))
        if not packet:
            raise ConnectionError("Failed to recv..")
        data += packet
    return data


def recv_framed(sock):
    header = recv_all(sock, 4)
    (length,) = struct.unpack('!I', header)

    payload = recv_all(sock, length)
    return payload


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:

        p_uuid = uuid4()
        s.connect((HOST, PORT))
        logger.info("Connected s to %s:%s", HOST, PORT)
        while True:
            msg = f"Hello framed echo server from Python: {p_uuid}"
            long_msg = msg * 100
            message = long_msg.encode()
            header = struct.pack('!I', len(message))
            s.sendall(header + message)
            logger.info('len sent: %s', len(message))

            response = recv_framed(s)
            result = response.decode()

            #logger.info("Received: %s", result)
            logger.info('len received: %s', len(result))
            time.sleep(5)
            #break


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()

