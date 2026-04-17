#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ─────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── FIXED object_write ───────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {

    // HEADER
    char header[64];
    int header_len = sprintf(header, "%s %zu",
        type == OBJ_BLOB ? "blob" :
        type == OBJ_TREE ? "tree" : "commit", len) + 1;

    size_t total_size = header_len + len;

    unsigned char *buffer = malloc(total_size);
    if (!buffer) return -1;

    memcpy(buffer, header, header_len);
    memcpy(buffer + header_len, data, len);

    // HASH
    compute_hash(buffer, total_size, id_out);

    // DEDUP
    if (object_exists(id_out)) {
        free(buffer);
        return 0;
    }

    // PATH
    char path[512];
    object_path(id_out, path, sizeof(path));

    char dir[512];
    strncpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (!slash) {
        free(buffer);
        return -1;
    }
    *slash = '\0';

    mkdir(PES_DIR, 0755);
    mkdir(OBJECTS_DIR, 0755);
    mkdir(dir, 0755);

    // TEMP FILE
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s/tmpXXXXXX", dir);

    int fd = mkstemp(temp_path);
    if (fd < 0) {
        free(buffer);
        return -1;
    }

    ssize_t written = write(fd, buffer, total_size);
    if (written < 0 || (size_t)written != total_size) {
        close(fd);
        free(buffer);
        return -1;
    }

    fsync(fd);
    close(fd);

    // RENAME
    if (rename(temp_path, path) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
}

// ─── FIXED object_read ───────────────────────────────

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    unsigned char *buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        return -1;
    }

    if (fread(buffer, 1, size, f) != size) {
        fclose(f);
        free(buffer);
        return -1;
    }
    fclose(f);

    // VERIFY HASH
    ObjectID computed;
    compute_hash(buffer, size, &computed);

    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buffer);
        return -1;
    }

    // PARSE HEADER
    char *header_end = memchr(buffer, '\0', size);
    if (!header_end) {
        free(buffer);
        return -1;
    }

    char type_str[10];
    sscanf((char *)buffer, "%s %zu", type_str, len_out);

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else *type_out = OBJ_COMMIT;

    // DATA
    *data_out = malloc(*len_out);
    if (!*data_out) {
        free(buffer);
        return -1;
    }

    memcpy(*data_out, header_end + 1, *len_out);

    free(buffer);
    return 0;
}
