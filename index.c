#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern int object_write(ObjectType, const void*, size_t, ObjectID*);

// ─────────────────────────────
// LOAD
// ─────────────────────────────
int index_load(Index *index) {

    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {

        IndexEntry *e = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];

        int ret = fscanf(f, "%o %64s %lu %u %s\n",
            &e->mode,
            hex,
            &e->mtime_sec,
            &e->size,
            e->path);

        if (ret != 5) break;

        hex_to_hash(hex, &e->hash);

        index->count++;
    }

    fclose(f);
    return 0;
}

// ─────────────────────────────
// SAVE
// ─────────────────────────────
int index_save(const Index *index) {

    FILE *f = fopen(INDEX_FILE, "w");
    if (!f) return -1;

    for (int i = 0; i < index->count; i++) {

        const IndexEntry *e = &index->entries[i];
        char hex[HASH_HEX_SIZE + 1];

        hash_to_hex(&e->hash, hex);

        fprintf(f, "%o %s %lu %u %s\n",
            e->mode,
            hex,
            e->mtime_sec,
            e->size,
            e->path);
    }

    fclose(f);
    return 0;
}

// ─────────────────────────────
// ADD
// ─────────────────────────────
int index_add(Index *index, const char *path) {

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    void *data = malloc(size);
    if (!data) {
        fclose(f);
        return -1;
    }

    fread(data, 1, size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        free(data);
        return -1;
    }

    free(data);

    if (index->count >= MAX_INDEX_ENTRIES) return -1;

    IndexEntry *e = &index->entries[index->count];

    e->mode = 0100644;
    e->size = size;
    e->mtime_sec = 0;
    strcpy(e->path, path);
    e->hash = id;

    index->count++;

    return index_save(index);
}

// ─────────────────────────────
// STATUS
// ─────────────────────────────
int index_status(const Index *index) {

    printf("Staged changes:\n");

    if (index->count == 0) {
        printf("  (nothing to show)\n");
    } else {
        for (int i = 0; i < index->count; i++) {
            printf("  staged: %s\n", index->entries[i].path);
        }
    }

    printf("\nUnstaged changes:\n");
    printf("  (nothing to show)\n");

    printf("\nUntracked files:\n");
    printf("  (not implemented)\n");

    return 0;
}
