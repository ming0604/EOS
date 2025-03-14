import socket
import sys
import threading
import os
import time
import signal
import json
import pygame


SCREEN_WIDTH = 800
SCREEN_HEIGHT = 600

# Global variables
client_socket = None
fd_device = None
player_died = False

def clear():
    global client_socket, fd_device
    # Close the socket and device file
    if client_socket:
        client_socket.close()
        print("client socket is closed")
    if fd_device:
        os.close(fd_device)
        fd_device = None
        print("device file is closed")

# signal handler for cleaning up the resources and exiting the program
def sigint_handler(signum, frame):
    pygame.quit()
    clear()
    exit(0)

class GameObject():
    def __init__(self, name):
        self.name = name
        self.rect = None
        self.surface = None
    #set a simple surface and rect of the object
    def set_simple_surface(self, color, surface_width, surface_height):
        #create a surface with the given size
        self.surface = pygame.Surface((surface_width, surface_height))
        #fill the surface with the given color
        self.surface.fill(color)
        #get the rect of the surface
        self.rect = self.surface.get_rect()

    # set the image surface and rect of the object
    def set_image_surface(self, image_path, image_width, image_height):
        #load the image from the given path
        self.surface = pygame.image.load(image_path)
        #scale the image to the given size
        self.surface = pygame.transform.scale(self.surface, (image_width, image_height))
        #get the rect of the image
        self.rect = self.surface.get_rect()

    # display the object on the screen
    def display_pose(self,screen,x,y):
        # if the object has surface and rect, display the object on the screen
        if self.surface and self.rect:
            #set the object's rect center at the given position
            self.rect.center = (x,y)
            #display the object on the screen
            screen.blit(self.surface, self.rect)
        else:
            print("The object {} does not have surface and rect, so it can't be displayed".format(self.name))


# create the player objects
def create_player_objects(player_images,image_width,image_height):
    player_objs = []
    for player_image in player_images:
        player_index = player_images.index(player_image)
        player = GameObject('Player {}'.format(player_index+1))
        player.set_image_surface(player_image,image_width,image_height)
        player_objs.append(player)
    return player_objs

# create the obstacle object
def create_obstacle_object(obstacle_image,image_width,image_height):
    obstacle = GameObject('UFO')
    obstacle.set_image_surface(obstacle_image,image_width,image_height)
    return obstacle

# create meteor object
def create_meteor_object(meteor_image,image_width,image_height):
    meteor = GameObject('Meteor')
    meteor.set_image_surface(meteor_image,image_width,image_height)
    return meteor

# create the bullet object
def create_bullet_objects(player_num, colors, surface_width, surface_height):
    bullet_objs = []
    for i in range(player_num):
        bullet = GameObject('bullet{}'.format(i+1))
        bullet.set_simple_surface(colors[i], surface_width, surface_height)
        bullet_objs.append(bullet)
    return bullet_objs

def game_ready_display(screen, player_id):

    # Draw game ready and your play id message
    #set the font
    font = pygame.font.SysFont("Arial", 36)
    #player color list
    player_colors = ['Red','Green']
    #create the text surface for ready message and player id message
    ready_text_surface = font.render("Game Ready", True, (255, 255, 255))
    player_id_text_surface = font.render("You are Player {} ({})".format(player_id,player_colors[player_id-1]), True, (255, 0, 0) if player_id == 1 else (0, 255, 0))
    #set the text position at about the center of the screen
    ready_text_rect = ready_text_surface.get_rect(center=(screen.get_width()//2, screen.get_height()//2 - 20))
    player_id_text_rect = player_id_text_surface.get_rect(center=(screen.get_width()//2, screen.get_height()//2 + 20))
    #draw the text on the screen
    screen.blit(ready_text_surface, ready_text_rect)
    screen.blit(player_id_text_surface, player_id_text_rect)
    #update the screen
    pygame.display.flip()


def update_game_display(game_state, my_player_id, screen, player_objs, obstacle_obj, meteor_obj, bullet_objs):
 
    # set the font
    font = pygame.font.SysFont("Arial", 36)

    # Draw obstacles
    for obstacle in game_state['obstacles']:
        obstacle_obj.display_pose(screen,obstacle['x'],obstacle['y'])

    # Draw bullets
    for bullet in game_state['bullets']:
        #check if the bullet owner is valid
        if bullet['owner'] <= len(bullet_objs):
            #display the different color bullet based on the different player
            bullet_objs[bullet['owner']-1].display_pose(screen,bullet['x'],bullet['y'])      
        else:
            print("Invalid bullet owner: {}".format(bullet['owner']))

    # Draw players and its score
    for player in game_state['players']:
        # check if the player is alive, if the player is alive, display the player on the screen
        if player['isAlive'] == True:
            # get the player index at the player_objs list
            player_index = player['id']-1
            # display the player on the screen
            player_objs[player_index].display_pose(screen,player['x'],player['y'])
            if(player['isShootingDisabled'] == True):
                #display the meteor on the player(player shooting is disabled)
                meteor_obj.display_pose(screen,player['x'],player['y']-50)

        elif player['isAlive'] == False:
            # if the player is the user, set the player_died to True, and display the death message and final score
            if player['id'] == my_player_id:
                global player_died 
                player_died = True
                #display the user player's death message and final score
                death_text = font.render("You are dead", True, (255, 255, 255))
                death_text_rect = death_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2 - 20))
                screen.blit(death_text, death_text_rect)
                final_score_text = font.render("Your final score:{}".format(player['score']), True, (255, 255, 255))
                final_score_text_rect = final_score_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2 + 20))
                screen.blit(final_score_text, final_score_text_rect)

        # Draw all players' scores 
        score_text = font.render("Player {} score:{}".format(player['id'],player['score']), True, (255, 255, 255))
        screen.blit(score_text, (10, 50+40*player['id']))

    # Draw game time
    time = game_state['gameRemainingTime']
    text = font.render("time:{}".format(time), True, (255, 255, 255))
    screen.blit(text, (10, 10))
    
    #update the screen
    pygame.display.flip()

def game_over_display(game_state, screen):
    # Clear screen with black
    screen.fill((0, 0, 0))  
    # Set the font
    font = pygame.font.SysFont("Arial", 36)

    # Draw game over message
    game_over_text = font.render("Game Over", True, (255, 255, 255))
    game_over_text_rect = game_over_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2-20))
    screen.blit(game_over_text, game_over_text_rect)

    # Draw the score of each player
    for player in game_state['players']:
        score_text = font.render("Player {} score:{}".format(player['id'],player['score']), True, (255, 255, 255))
        score_text_rect = score_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2 + 30 + 40*player['id']))
        screen.blit(score_text, score_text_rect)
    if game_state['winner'] == 0: # if the winner is 0, it means the game is tie
        winner_text = font.render("Tie", True, (255, 255, 0))
        winner_text_rect = winner_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2 + 30 + 40*(len(game_state['players']) + 1)))
        screen.blit(winner_text, winner_text_rect)
    else:
        # Draw the final winner
        winner_text = font.render("Winner is Player {}".format(game_state['winner']), True, (255, 0, 0) if game_state['winner'] == 1 else (0, 255, 0))
        winner_text_rect = winner_text.get_rect(center=(screen.get_width()//2, screen.get_height()//2 + 30 + 40*(len(game_state['players']) + 1)))
        screen.blit(winner_text, winner_text_rect)

    # Update the screen
    pygame.display.flip()

# thread for reading the button press input and sending it to the server
def button_thread_func(device_file_dir):
    global fd_device
    global client_socket
    global player_died
    # Open the device file
    fd_device = os.open(device_file_dir, os.O_RDWR)
    left_last = "no button input"
    right_last = "no button input"
    same_button_cnt = 0

    while player_died != True:
        buffer = os.read(fd_device, 2)
        left_button_state = buffer[0]
        right_button_state = buffer[1]

        # no button pressed
        if not left_button_state and not right_button_state :
            if left_last != 0 and right_last != 0:
                print("No button pressed, left button state: {}, right button state: {}".format(left_button_state, right_button_state))
            left_last = 0
            right_last = 0
        else:
            # left button pressed
            if left_button_state:
                # check if the same button is pressed multiple times, only if the same button is pressed more than 3 times, send the button state to the server
                if left_last == 1 and same_button_cnt < 3:
                    same_button_cnt += 1
                    continue
                else:
                    same_button_cnt = 0

                print("Left button pressed : {}".format(left_button_state))
                left_last == 1
               
            # right button pressed
            if right_button_state:
                # check if the same button is pressed multiple times, only if the same button is pressed more than 3 times, send the button state to the server
                if right_last == 1 and same_button_cnt < 3:
                    same_button_cnt += 1
                    continue
                else:
                    same_button_cnt = 0

                print("Right button pressed : {}".format(right_button_state))
                right_last == 1

            # Send the button state to the server
            button_state_message = "Left button state: {}, Right button state: {}".format(left_button_state, right_button_state)
            client_socket.sendall(button_state_message.encode())

        # Sleep for a while (client read the button state every 0.22 seconds)
        time.sleep(0.22)

    os.close(fd_device)
    fd_device = None


# thread for receiving game states from the server
def receive_game_thread_func():
    global client_socket
    global player_died
    my_player_id = None
    disconnected = False

    #initialize the pygame 
    pygame.init()
    #set the screen size
    main_screen = pygame.display.set_mode((SCREEN_WIDTH, SCREEN_HEIGHT))
    #set the screen title
    pygame.display.set_caption("Game")
    #define the player images, obstacle image and background image
    player_images = ['player1.png','player2.png']
    obstacle_image = 'obstacle.png'
    background_image = 'background2.jpg'
    metero_image = 'metero.png'
    #load the background image and scale it to the screen size
    background_image = pygame.image.load(background_image)
    background_image = pygame.transform.scale(background_image, (SCREEN_WIDTH, SCREEN_HEIGHT))
    #define the bullet colors
    bullet_colors = [(255, 0, 0), (0, 255, 0)]
    #number of players in the game
    player_num = len(player_images)
    #create the game objects
    player_objs = create_player_objects(player_images,40,70)
    obstacle_obj = create_obstacle_object(obstacle_image,90,60)
    metero_obj = create_meteor_object(metero_image,80,70)
    bullet_objs = create_bullet_objects(player_num,bullet_colors,5,5)
    #initialize the recv_buffer
    recv_buffer = ""

    while True:
        #close the game when the user click the close button
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                clear()
                pygame.quit()
                exit(0)
        #receive the data from the server
        data = client_socket.recv(1024)
        if not data:
            # server disconnected
            if not disconnected:
                print("recv() returned empty data from server")
                disconnected = True
            continue

        # append the received data to the recv_buffer 
        recv_buffer += data.decode('utf-8')

        # 
        while "\n" in recv_buffer:
            line, recv_buffer = recv_buffer.split("\n", 1)
            line = line.strip()
            if not line:
                # 
                continue
            try:
                game_states = json.loads(line)
            except json.JSONDecodeError:
                print("[Warning] Received invalid JSON:", line)
                continue

            #check the game state and display the game based on the game state
            if game_states['game_state'] == "Ready":
                print("Ready")
                my_player_id = game_states['players'][0]['id']
                main_screen.blit(background_image, (0, 0))
                game_ready_display(main_screen, my_player_id)
            elif game_states['game_state'] == "Game_start":
                print("Game running")
                main_screen.blit(background_image, (0, 0))
                update_game_display(game_states, my_player_id, main_screen, player_objs, obstacle_obj, metero_obj, bullet_objs)
            elif game_states['game_state'] == "Game_over":
                print("Game over")
                game_over_display(game_states, main_screen)
            else:
                print("Invalid game state:{}".format(game_states['game_state']))
                client_socket.close()
                exit(1)

    client_socket.close()
    pygame.quit()

def main():
    global client_socket
    # Register the signal handler for ctrl+c
    signal.signal(signal.SIGINT, sigint_handler)
    
    if len(sys.argv) != 4:
        print("Usage: python3 client.py <server_ip> <server_port> <device_file>")
        exit(1)

    # Get the server IP, port and device file
    server_ip = sys.argv[1]
    server_port = int(sys.argv[2])
    device_file_dir = sys.argv[3]

    # Create a socket and connect to the server
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((server_ip, server_port))
    
    # Create threads for sending and receiving data
    #button_thread = threading.Thread(target=button_thread_func, args=(device_file_dir,))
    recv_game_thread = threading.Thread(target=receive_game_thread_func)
    # Start the threads
    #button_thread.start()
    recv_game_thread.start()

    #button_thread.join()
    recv_game_thread.join()

    clear()

if __name__ == "__main__":
    main()