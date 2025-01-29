#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define DATA_BUFFER_SIZE 4096

// Funkcja obsługująca komunikację z klientem
void handle_client(int cfd, const char* client_ip, uint16_t client_port) {
    char buf[BUFFER_SIZE];
    char data_buf[DATA_BUFFER_SIZE];
    char command[BUFFER_SIZE];
    char argument[BUFFER_SIZE];
    int binary_mode = 1; // Domyślny tryb binarny

    while (1) {
        // Zerowanie buforów
        bzero(buf, BUFFER_SIZE);
        bzero(command, BUFFER_SIZE);
        bzero(argument, BUFFER_SIZE);
        bzero(data_buf, DATA_BUFFER_SIZE);

        // Odczyt danych od klienta
        int rc = read(cfd, buf, BUFFER_SIZE);
        if (rc <= 0) {
            perror("Error");
            break;
        }

        // Parsowanie komendy i argumentu
        int parsed = sscanf(buf, "%s \"%[^\"]\"", command, argument);
        if (parsed < 2) {
            sscanf(buf, "%s %s", command, argument);
        }

        printf("[%s:%d] Received command: %s %s\n", client_ip, client_port, command, argument);
        
        // Obsługa różnych komend
        if (strcmp(command, "exit") == 0) { // Wyjście  (EXIT)
            break;
        } else if (strcmp(command, "ascii") == 0) { // Ustaw tryb ASCII  (ASCII)
            binary_mode = 0;
            write(cfd, "Mode set to ASCII.\n", 19);
        } else if (strcmp(command, "binary") == 0) { // Ustaw tryb binarny  (BINARY)
            binary_mode = 1;
            write(cfd, "Mode set to Binary.\n", 20);
        } else if (strcmp(command, "mkdir") == 0) { // Tworzenie katalogu  (MKDIR)
            if (mkdir(argument, 0777) == 0) {
                write(cfd, "OK\n", 3);
            } else {
                write(cfd, "Failed to create directory.\n", 28);
            }
        } else if (strcmp(command, "rmdir") == 0) { // Usuwanie katalogu  (RMDIR)
            if (rmdir(argument) == 0) {
                write(cfd, "OK\n", 3);
            } else {
                write(cfd, "Failed to remove directory. Make sure the directory is empty.\n", 62);
            }
        } else if (strcmp(command, "rm") == 0) { // Usuwanie pliku  (RM)
            if (remove(argument) == 0) {
                write(cfd, "OK\n", 3);
            } else {
                write(cfd, "Failed to remove file.\n", 23);
            }
        } else if (strcmp(command, "put") == 0) { // Wysyłanie pliku na serwer  (PUT)
            int fd = open(argument, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                write(cfd, "Failed to create a file.\n", 25);
                continue;
            }
            write(cfd, "Ready\n", 6);

            int bytes;
            int totalBytesRead = 0;
            while ((bytes = read(cfd, data_buf, DATA_BUFFER_SIZE)) > 0) {
                // Sprawdzenie, czy odebrano znacznik końca pliku
                if (bytes >= strlen("#EOF#\n")) {
                    if (strncmp(data_buf, "#EOF#\n", strlen("#EOF#\n")) == 0) {
                        break;
                    }
                    if (strncmp(&data_buf[bytes - strlen("#EOF#\n")], "#EOF#\n", strlen("#EOF#\n")) == 0) {
                        bytes -= strlen("#EOF#\n");
                        write(fd, data_buf, bytes);
                        break;
                    }
                }

                if (!binary_mode) {
                    // Tryb ASCII: konwersja CRLF na LF
                    char converted_buf[DATA_BUFFER_SIZE];
                    int converted_len = 0;

                    for (int i = 0; i < bytes; i++) {
                        if (data_buf[i] == '\r') {
                            if (i + 1 < bytes && data_buf[i + 1] == '\n') {
                                converted_buf[converted_len++] = '\n';
                                i++; // Pominięcie LF
                            } else {
                                converted_buf[converted_len++] = data_buf[i];
                            }
                        } else {
                            converted_buf[converted_len++] = data_buf[i];
                        }
                    }

                    // Zapis przekształconych danych do pliku
                    write(fd, converted_buf, converted_len);
                    totalBytesRead += converted_len;
                } else {
                    // Tryb binarny: zapis bez zmian
                    write(fd, data_buf, bytes);
                    totalBytesRead += bytes;
                }
            }

            if (totalBytesRead > 0) {
                write(cfd, "Success\n", 8);
            } else {
                write(cfd, "Failed to receive file data.\n", 29);
            }
            close(fd);
        } else if (strcmp(command, "get") == 0) { // Pobieranie pliku z serwera (GET)
            int fd = open(argument, O_RDONLY);
            if (fd < 0) {
                write(cfd, "Failed to open file.\n", 21);
                continue;
            }

            // Wysyłanie potwierdzenia gotowości do wysyłki danych
            write(cfd, "Ready\n", 6);

            int bytes;
            while ((bytes = read(fd, data_buf, DATA_BUFFER_SIZE)) > 0) {
                if (!binary_mode) {
                    // Tryb ASCII: konwersja LF na CRLF
                    char converted_buf[DATA_BUFFER_SIZE * 2];
                    int converted_len = 0;

                    for (int i = 0; i < bytes; i++) {
                        if (data_buf[i] == '\n') {
                            converted_buf[converted_len++] = '\r';
                            converted_buf[converted_len++] = '\n';
                        } else if (data_buf[i] == '\r') {
                            converted_buf[converted_len++] = '\r';
                            converted_buf[converted_len++] = '\n';
                        } else {
                            converted_buf[converted_len++] = data_buf[i];
                        }
                    }

                    // Wysyłanie przekształconych danych do klienta
                    write(cfd, converted_buf, converted_len);
                } else {
                    // Tryb binarny: wysyłanie danych bez zmian
                    write(cfd, data_buf, bytes);
                }
            }

            // Zamknięcie pliku po odczycie
            close(fd);

            // Wysyłanie znacznika końca transmisji
            write(cfd, "#EOF#\n", strlen("#EOF#\n"));
        } else if (strcmp(command, "ls") == 0) { // Wyświetlanie listy plików/katalogów (LS)
            DIR *dir;
            struct dirent *entry;
            char response[BUFFER_SIZE] = "";
            char path[BUFFER_SIZE] = "/";
            char full_path[2*BUFFER_SIZE];

            if (strlen(argument) > 0) {
                strncpy(path, argument, BUFFER_SIZE - 1);
            }

            dir = opendir(path);
            if (dir == NULL) {
                snprintf(response, BUFFER_SIZE, "403NoAccess\n");
                write(cfd, response, strlen(response));
                continue;
            }

            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue; 
                }

                // Tworzenie pełnej ścieżki do pliku/katalogu
                if (path[strlen(path) - 1] == '/') {
                    snprintf(full_path, 2*BUFFER_SIZE, "%s%s", path, entry->d_name);
                } else {
                    snprintf(full_path, 2*BUFFER_SIZE, "%s/%s", path, entry->d_name);
                }

                // Pobieranie informacji o pliku/katalogu
                struct stat entry_stat;
                if (stat(full_path, &entry_stat) == 0) {
                    if (S_ISDIR(entry_stat.st_mode)) {
                        strcat(response, full_path);
                    } else {
                        strcat(response, entry->d_name);
                    }
                    strcat(response, ";"); 
                }
            }
            closedir(dir);
            write(cfd, response, strlen(response));
            write(cfd, "\n", 1);
        } else {
            printf("[%s:%d] Ping or unknown command.\n", client_ip, client_port);
        }
    }
    printf("Client disconnected: %s:%d\n", client_ip, client_port);
}

// Struktura reprezentująca klienta
struct cln {
    int cfd; // Deskryptor pliku klienta
    struct sockaddr_in caddr; // Adres klienta
};

// Funkcja wątku obsługującego klienta
void* cthread(void* arg) {
    struct cln* c = (struct cln*)arg;
    printf("[%lu] new connection from: %s:%d\n", (unsigned long int)pthread_self(),  inet_ntoa((struct in_addr)c->caddr.sin_addr), ntohs(c->caddr.sin_port));
    handle_client(c->cfd, inet_ntoa((struct in_addr)c->caddr.sin_addr), ntohs(c->caddr.sin_port));
    close(c->cfd);
    free(c);
    return 0;
}

int main(int argc, char **argv) {
    pthread_t tid;
    int sfd, on = 1;
    struct sockaddr_in saddr;
    socklen_t sl;
    memset(&saddr, 0, sizeof(saddr));
    signal(SIGPIPE, SIG_IGN); // Ignorowanie sygnału SIGPIPE

    // Ustawienia adresu serwera
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(1234);
    
    // Tworzenie gniazda
    sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    // Ustawienie opcji gniazda
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    
    // Powiązanie gniazda z adresem
    bind(sfd, (struct sockaddr*) &saddr, sizeof(saddr));
    
    // Nasłuchiwanie na gnieździe
    listen(sfd, 10);

    printf("FTP Server is running...\n");
    
    while (1) {
        // Akceptowanie połączenia od klienta
        struct cln* c = malloc(sizeof(struct cln));
        sl = sizeof(c->caddr);
        c->cfd = accept(sfd, (struct sockaddr*)&c->caddr, &sl);
        
        // Tworzenie nowego wątku dla klienta
        pthread_create(&tid, NULL, cthread, c);
        pthread_detach(tid); // Odłączenie wątku
    }

    close(sfd);
    return 0;
}