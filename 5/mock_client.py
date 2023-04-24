import socket

HOST = "127.0.0.1"
PORT = 8080

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.settimeout(2)
    while True:
        try:
            text = input()
            if text == 'get':
                print(s.recv(1024))
            elif len(text) > 1:
                s.sendall(bytes(text, 'utf-8'))
                print('Send')
        except Exception:
            pass
