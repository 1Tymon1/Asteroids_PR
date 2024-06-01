#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#define PLAYER_CODE     0b10000001
#define PROJECTILE_CODE 0b10000010
#define ASTEROID_CODE   0b10000100
#define DELETUS_CODE    0b10001000

#include <SFML/Graphics.hpp>
#include <cmath>
#include <stdio.h>
#include <list>
#include <io.h>
#include <thread>

#include <winsock2.h>

#include "Projectile.hpp"
#include "Spaceship.h"
#include "Asteroid.h"

using namespace sf;

#pragma pack(push,1)
typedef struct {
    unsigned char ID;
    unsigned char Shot;
    char KeyboardKeys;
    float XPosition;
    float YPosition;
    float XVelocity;
    float YVelocity;
    float angle;
    float XPositionShot;
    float YPositionShot;
    float XVelocityShot;
    float YVelocityShot;
} Frame;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _sendFrameEntity {
    unsigned char header;     //kod okreslajacy co to za rodzaj ramki (0b1000001 - gracz, 0b10000010 - pocisk)
    short ID;
    float posX;
    float posY;
    float speedX;
    float speedY;
    float rotation;
} sendFrameEntity;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _asteroidFrame {
    unsigned char header;     //kod okreslajacy co to za rodzaj ramki (0b10000100 - asteroida)
    short ID;
    float posX;
    float posY;
    float speedX;
    float speedY;
    float rotation;
    unsigned char size;
} asteroidFrame;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _deletus {
    unsigned char header;       //kod okreslajacy co to za rodzaj ramki (inne niz 0b1000001 oraz 0b10001000)
    unsigned char toDelete;     //1- delete player 2- delete projectile 3- delete asteroid
    unsigned char ID;           //id of element to delete
} deletus;
#pragma pack(pop)

Mutex coopMutex;
Mutex connectedMutex;
Mutex projectileMutex;
Mutex asteroidMutex;

//size of temporary data packet - we should determine the details of how to send data in order to do anything further
#define DATA_PACKET_SIZE 39
//if defined packets will be send every "WAIT_TIME" seconds, instead of all the time
#define PERIODIC_PACKET_SEND
#define WAIT_TIME 0.5

void receive(int connection, std::vector<Spaceship>& ships, std::vector<Projectile>& projectiles, std::vector<Asteroid>& asteroids, bool& isWorking) {
    int bytes_received = 0;
    int total_bytes_received = 0;
    char readBuffer[50];
    sendFrameEntity frame;
    while (isWorking) {
        total_bytes_received = 0;
        bytes_received = recv(connection, readBuffer, 1, 0);    //wczytywanie 1 bajtu do odczytania headera
        if (bytes_received <= 0) {
            // handle errors or connection closed
            perror("recv failed");
            _close(connection);
            return;
        }
        total_bytes_received += bytes_received;
        unsigned char header = readBuffer[0];   //pobieranie headera

        if (header == PLAYER_CODE || header == PROJECTILE_CODE) { //ramka gracza, asteroid i pocisk�w
            // Loop to ensure we receive the complete frame
            while (total_bytes_received < sizeof(sendFrameEntity)) {
                bytes_received = recv(connection, readBuffer + total_bytes_received, sizeof(sendFrameEntity) - total_bytes_received, 0);
                if (bytes_received <= 0) {
                    // handle errors or connection closed
                    perror("recv failed");
                    _close(connection);
                    return;
                }
                total_bytes_received += bytes_received;
            }
            // Now we have a complete frame in readBuffer
            sendFrameEntity* f = (sendFrameEntity*)readBuffer;
            if (f->header == PLAYER_CODE) {
                bool isNewShip = true;
                coopMutex.lock();
                for (int i = 0; i < ships.size(); i++) {
                    if (f->ID == ships[i].id) {
                        isNewShip = false;
                        ships[i].position.x = f->posX;
                        ships[i].position.y = f->posY;
                        ships[i].rotation = f->rotation;

                    }
                }
                if (isNewShip) {
                    ships.push_back(Spaceship(f->posX, f->posY, false, f->ID));
                }
                coopMutex.unlock();
            }
            else if (f->header == PROJECTILE_CODE) {
                bool isNewProjectile = true;
                projectileMutex.lock();
                for (int i = 0; i < projectiles.size(); i++) {
                    if (f->ID == projectiles[i].id) {
                        isNewProjectile = false;
                        projectiles[i].position.x = f->posX;
                        projectiles[i].position.y = f->posY;

                    }
                }
                if (isNewProjectile) {
                    projectiles.push_back(Projectile(f->posX, f->posY, f->speedX, f->speedY, f->ID));
                }

                projectileMutex.unlock();
            }

        }
        else if (header == ASTEROID_CODE) {
            while (total_bytes_received < sizeof(asteroidFrame)) {
                bytes_received = recv(connection, readBuffer + total_bytes_received, sizeof(asteroidFrame) - total_bytes_received, 0);
                if (bytes_received <= 0) {
                    // handle errors or connection closed
                    perror("recv failed");
                    _close(connection);
                    return;
                }
                total_bytes_received += bytes_received;
            }
            // Now we have a complete frame in readBuffer
            asteroidFrame* f = (asteroidFrame*)readBuffer;

            bool isNewAsteroid = true;
            asteroidMutex.lock();
            for (int i = 0; i < asteroids.size(); i++) {
                if (f->ID == asteroids[i].id) {
                    isNewAsteroid = false;
                    asteroids[i].position.x = f->posX;
                    asteroids[i].position.y = f->posY;
                    asteroids[i].rotation = f->rotation;
                    asteroids[i].speed.x = f->speedX;
                    asteroids[i].speed.y = f->speedY;
                    if (asteroids[i].size != (int)f->size) {
                        asteroids[i].size = (int)f->size;
                        asteroids[i].sprite.setScale(0.05 * (int)f->size, 0.05 * (int)f->size);
                    }
                }
            }
            if (isNewAsteroid) {
                asteroids.push_back(Asteroid(f->posX, f->posY, f->speedX, f->speedY, f->ID, f->size));
            }

            asteroidMutex.unlock();
        }
        else if (header == DELETUS_CODE) {
            while (total_bytes_received < sizeof(deletus)) {
                bytes_received = recv(connection, readBuffer + total_bytes_received, sizeof(deletus) - total_bytes_received, 0);
                if (bytes_received <= 0) {
                    // handle errors or connection closed
                    perror("recv failed");
                    _close(connection);
                    return;
                }
                total_bytes_received += bytes_received;
            }
            // Now we have a complete frame in readBuffer
            deletus* f = (deletus*)readBuffer;

            switch (f->toDelete) {
            case 1: //players
                coopMutex.lock();
                for (auto i = ships.begin(); i != ships.end(); i++) {
                    if (f->ID == i->id) {
                        ships.erase(i);
                        break;
                    }
                }
                coopMutex.unlock();
                break;
            case 2: //projectile
                projectileMutex.lock();
                for (auto i = projectiles.begin(); i != projectiles.end(); i++) {
                    if (f->ID == i->id) {
                        projectiles.erase(i);
                        break;
                    }
                }
                projectileMutex.unlock();
                break;
            case 3: //asteroid
                asteroidMutex.lock();
                for (auto i = asteroids.begin(); i != asteroids.end(); i++) {
                    if (f->ID == i->id) {
                        asteroids.erase(i);
                        break;
                    }
                }
                asteroidMutex.unlock();
                break;
            }
        }
        /*else {//inna ramka niz gracz i asteroida
            while (total_bytes_received < sizeof(sendFrameSerwerInfo)) {
                bytes_received = recv(sockfd, readBuffer + total_bytes_received, sizeof(sendFrameSerwerInfo) - total_bytes_received, 0);
                if (bytes_received <= 0) {
                    // handle errors or connection closed
                    perror("recv failed");
                    close(sockfd);
                    return NULL;
                }
                total_bytes_received += bytes_recei
            printf("Unknown frame. Header: %d\n", header);
        }*/
    }
    
}

void GameplayLoop(int connection) {
    //test2323
    VideoMode desktopMode = VideoMode::getDesktopMode(); //wyci�gamy rozmiar monitora
    RenderWindow window(desktopMode, "Asteroids Clone", Style::Default); //tworzymy okno gry

    //inicjalizacja statku, oraz zmiennych odpowiadaj�cych za poruszanie.
    initSpaceshipTexture();
    Spaceship spaceship(desktopMode.width -1, desktopMode.height - 1, true, -1);
    spaceship.setSpeed(0, 0);
    double rotationSpeed = 100;
    double acceleration = 0.1 * desktopMode.width;
    double maxSpeed = 1 * desktopMode.width;
    double shotCooldown = 0.5;
    double shotTimer = 0;

    std::vector<Spaceship> ships;
    std::vector<Projectile> projectiles;
    std::vector<Asteroid> asteroids;
    initProjectileTexture();
    initAsteroidTexture();

    bool isWorking = true;
    double sendTimer = 0;
    double sendCooldown = 0.02;
    int shot = 0;
    Projectile proj(0, 0, 0, 0, -1);

    //zegar bo potem musi by� delta time �eby dzia�a�o jak ma
    Clock clock;

    //data to send
    unsigned char data[DATA_PACKET_SIZE];
    unsigned char fakeData[DATA_PACKET_SIZE];
    std::thread cum{ receive, connection, std::ref(ships), std::ref(projectiles), std::ref(asteroids), std::ref(isWorking)};

    while (window.isOpen()) {
        fakeData[0] = 12323; //id of packet (first 4 bytes of packet) 12323 - defoult id for movement
        for (int i = 1; i < DATA_PACKET_SIZE; i++) {
            fakeData[i] = 0;
        }

        Event event;
        while (window.pollEvent(event)) {
            if (event.type == Event::KeyPressed && event.key.code == Keyboard::Tab) { //debug log
                printf("top: %f\n", spaceship.getSprite().getGlobalBounds().top);
                printf("%f\n", spaceship.getSprite().getGlobalBounds().height);
                printf("left: %f\n", spaceship.getSprite().getGlobalBounds().left);
                printf("%f\n", spaceship.getSprite().getGlobalBounds().width);
            }
            if (event.type == sf::Event::Closed || event.type == Event::KeyPressed && event.key.code == Keyboard::Escape) //zamykanie okna x'em
            {
                isWorking = false;
                cum.join();
                window.close();
            }
        }

        float deltaTime = clock.restart().asSeconds();

        int keys = 0;

        // Handle input
        if (Keyboard::isKeyPressed(Keyboard::A)) {
            spaceship.rotate(-rotationSpeed * deltaTime); // lewa rotacja
            keys += 1;
        }
        if (Keyboard::isKeyPressed(Keyboard::D)) {
            spaceship.rotate(rotationSpeed * deltaTime); // prawa rotacja
            keys += 2;
        }
        if (Keyboard::isKeyPressed(Keyboard::W)) {
            spaceship.accelerate(acceleration * deltaTime); // przy�pieszenie
            keys += 4;
        }
        if (Keyboard::isKeyPressed(Keyboard::Space)) {
            if (shotTimer > shotCooldown)
            {
                proj = spaceship.shooting(desktopMode.width);
                //projectiles.push_back(proj);
                shotTimer = 0;
                shot = 1;
            }
        }
        shotTimer += deltaTime;

        Frame frame = {1, shot, keys, spaceship.position.x, spaceship.position.y, spaceship.speed.x, spaceship.speed.y, spaceship.rotation, proj.position.x, proj.position.y, proj.speed.x, proj.speed.y };

        if (sendTimer > sendCooldown) {
            send(connection, (const char*)&frame, sizeof(Frame), 0);
            sendTimer = 0;
            shot = 0;
        }
        else {
            sendTimer += deltaTime;
        }


        window.clear();

        // Update spaceship
        spaceship.update(deltaTime, desktopMode.width, desktopMode.height, window);
        projectileMutex.lock();
        for (int i = 0; i < projectiles.size(); i++) {
            projectiles[i].update(deltaTime, desktopMode.width, desktopMode.height, window);
        }
        projectileMutex.unlock();
        coopMutex.lock();
        for (int i = 0; i < ships.size(); i++) {
            ships[i].draw(window);
        }
        coopMutex.unlock();
        asteroidMutex.lock();
        for (int i = 0; i < asteroids.size(); i++) {
            asteroids[i].update(deltaTime, desktopMode.width, desktopMode.height, window);
        }
        asteroidMutex.unlock();

        window.display();
    }
}

int main() {

    //initializing Windows Socket API (windows moment)
    WSADATA wsaData;
    int iResult;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    //obtaining local ip adress
    struct hostent* host = gethostbyname("localhost");
    if (host == NULL) {
        return 45;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    //htons - converts data in pc format to network format (probably little endian to big endian idk)
    //based on this port, clients will select the appropriate server in the local network to connect to
    addr.sin_port = htons(2278);
    memcpy(&addr.sin_addr,host->h_addr_list[0],host->h_length);

    //creating socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        return 47;
    }

    //connecting with serwer using sockaddr_in adress
    if (connect(sockfd, (const struct sockaddr*)&addr, sizeof(addr))) {
        return 46;
    }
    const char* text = "connected";
    //send(sockfd, text, strlen(text), 0);

    //main gameplay loop
    GameplayLoop(sockfd);

    closesocket(sockfd);

    WSACleanup();
    return 0;
}
