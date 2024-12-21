import socket
import threading
import os
import time
import signal
import json
import pygame

HOST = '192.168.222.100'
PORT = 8888
DEVICE = "/dev/etx_device"

client_socket = None
fd_device = None

# signal handler for cleaning up the resources and exiting the program
def sigint_handler(signum, frame):
    global client_socket, fd_device
    # Close the socket and device file
    if client_socket:
        client_socket.close()
    if fd_device:
        os.close(fd_device)
    exit(0)


# thread for reading the button press input and sending it to the server
def button_thread_func():
    global fd_device
    global client_socket
    # Open the device file
    fd_device = os.open(DEVICE, os.O_RDWR)

    while True:
        buffer = os.read(fd_device, 2)
        left_button_state = buffer[0]
        right_button_state = buffer[1]
       
        # left button pressed
        if left_button_state:
            print("Left button pressed : {}".format(left_button_state))
            time.sleep(0.22)  # when the button is pressed, wait for a while to avoid multiple inputs
            last = "left"

        # right button pressed
        if right_button_state:
            print("Right button pressed : {}".format(right_button_state))
            time.sleep(0.22)   # when the button is pressed, wait for a while to avoid multiple inputs
            last = "right"

        # no button pressed
        if not left_button_state and not right_button_state and last != "no button input":
            print("No button pressed, left button state: {}, right button state: {}".format(left_button_state, right_button_state))
            last = "no button input"

        # Send the button state to the server
        button_state_message = "Left button state: {}, Right button state: {}".format(left_button_state, right_button_state)
        client_socket.sendall(button_state_message.encode())

    os.close(fd)

# thread for receiving game states from the server
def receive_game_thread_func():
    global client_socket

    while True:
        #receive the game state json from the server
        game_state_json = client_socket.recv(1024).decode()


def main():
    global client_socket
    signal.signal(signal.SIGINT, sigint_handler)
    
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((HOST, PORT))
    
    # Create threads for sending and receiving data
    button_thread = threading.Thread(target=button_thread_func)
    recv_game_thread = threading.Thread(target=receive_game_thread_func)

    button_thread.start()
    recv_game_thread.start()


if __name__ == "__main__":
    main()