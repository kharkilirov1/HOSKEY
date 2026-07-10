/*
 * extract_weights_native.c
 *
 * Loads libjni_ykeyboard3.so via dlopen and calls the internal trie
 * weight lookup function to extract word frequencies.
 *
 * The .so has JNI exports but we bypass JNI by directly calling
 * the C++ trie initialization and lookup functions.
 *
 * Build (in WSL):
 *   gcc -o extract_weights extract_weights_native.c -ldl -lpthread -lm
 *
 * Run:
 *   ./extract_weights /path/to/libjni_ykeyboard3.so /path/to/main_ru /path/to/words.txt
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * We can't call JNI functions directly without JNIEnv.
 * Instead, we'll call the INTERNAL C++ functions by address offset.
 *
 * From Ghidra analysis (arm64 offsets, need x86_64 equivalents):
 *   - Trie open: loads dictionary from file path
 *   - Weight lookup: FUN_00660dca reads weight for a node
 *
 * Strategy:
 *   1. dlopen the .so
 *   2. Find checkWord JNI export (it wraps the trie lookup)
 *   3. Create a minimal fake JNIEnv that provides GetStringUTFChars etc.
 *   4. Call checkWord for each word
 *
 * The checkWord signature (from nm):
 *   Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_checkWord
 *   JNI: jboolean checkWord(JNIEnv*, jobject, jlong dict_ptr, jstring word)
 */

/* Fake JNI types */
typedef void* JNIEnv;
typedef void* jobject;
typedef void* jclass;
typedef void* jstring;
typedef int64_t jlong;
typedef uint8_t jboolean;
typedef int32_t jint;

/* JNI function table entry indices we need */
#define JNI_GetStringUTFChars_IDX  169
#define JNI_ReleaseStringUTFChars_IDX 170
#define JNI_NewStringUTF_IDX 167
#define JNI_GetStringLength_IDX 164

/* Function pointer types for JNI exports */
typedef jlong (*OpenFunc)(JNIEnv*, jobject, jstring, jlong, jlong);
typedef void  (*CloseFunc)(JNIEnv*, jobject, jlong);
typedef jboolean (*CheckWordFunc)(JNIEnv*, jobject, jlong, jstring);

/* Fake JNI string storage */
static char g_fake_string[4096];
static int g_fake_string_len;

/* Fake JNI function implementations */
static const char* fake_GetStringUTFChars(JNIEnv* env, jstring str, jboolean* isCopy) {
    if (isCopy) *isCopy = 0;
    return g_fake_string;
}

static void fake_ReleaseStringUTFChars(JNIEnv* env, jstring str, const char* utf) {
    /* no-op */
}

static jint fake_GetStringLength(JNIEnv* env, jstring str) {
    return g_fake_string_len;
}

static jstring fake_NewStringUTF(JNIEnv* env, const char* utf) {
    strncpy(g_fake_string, utf, sizeof(g_fake_string) - 1);
    g_fake_string_len = strlen(utf);
    return (jstring)g_fake_string;
}

static int fake_ExceptionCheck(JNIEnv* env) { return 0; }
static void fake_ExceptionClear(JNIEnv* env) {}
static void* fake_FindClass(JNIEnv* env, const char* name) { return NULL; }

/* Build a minimal JNI function table */
static void* g_jni_functions[512];
static void* g_jni_env_ptr;

static void init_fake_jni(void) {
    memset(g_jni_functions, 0, sizeof(g_jni_functions));
    g_jni_functions[JNI_GetStringUTFChars_IDX] = (void*)fake_GetStringUTFChars;
    g_jni_functions[JNI_ReleaseStringUTFChars_IDX] = (void*)fake_ReleaseStringUTFChars;
    g_jni_functions[JNI_GetStringLength_IDX] = (void*)fake_GetStringLength;
    g_jni_functions[JNI_NewStringUTF_IDX] = (void*)fake_NewStringUTF;
    /* ExceptionCheck at index 228 */
    g_jni_functions[228] = (void*)fake_ExceptionCheck;
    g_jni_functions[229] = (void*)fake_ExceptionClear;
    g_jni_functions[6] = (void*)fake_FindClass;

    g_jni_env_ptr = g_jni_functions;
}

static void set_jni_string(const char* str) {
    strncpy(g_fake_string, str, sizeof(g_fake_string) - 1);
    g_fake_string_len = strlen(str);
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <libjni_ykeyboard3.so> <main_ru_dict> <words_file>\n", argv[0]);
        fprintf(stderr, "  words_file: one word per line (UTF-8)\n");
        fprintf(stderr, "  Output: words_with_weights.tsv to stdout\n");
        return 1;
    }

    const char* so_path = argv[1];
    const char* dict_path = argv[2];
    const char* words_path = argv[3];

    /* Load the .so */
    void* handle = dlopen(so_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    fprintf(stderr, "Loaded: %s\n", so_path);

    /* Find JNI exports */
    OpenFunc open_fn = (OpenFunc)dlsym(handle,
        "Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_open");
    CloseFunc close_fn = (CloseFunc)dlsym(handle,
        "Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_close");
    CheckWordFunc check_fn = (CheckWordFunc)dlsym(handle,
        "Java_ru_yandex_androidkeyboard_nativecode_Native_00024BinaryDictionary_checkWord");

    if (!open_fn || !check_fn) {
        fprintf(stderr, "Failed to find JNI symbols: open=%p check=%p close=%p\n",
                open_fn, check_fn, close_fn);
        dlclose(handle);
        return 1;
    }
    fprintf(stderr, "Found: open=%p check=%p close=%p\n", open_fn, check_fn, close_fn);

    /* Init fake JNI */
    init_fake_jni();
    JNIEnv* env = &g_jni_env_ptr;

    /* Open dictionary */
    set_jni_string(dict_path);
    fprintf(stderr, "Opening dictionary: %s\n", dict_path);
    jlong dict_ptr = open_fn(env, NULL, (jstring)g_fake_string, 0, 0);
    fprintf(stderr, "Dictionary handle: 0x%lx\n", (unsigned long)dict_ptr);

    if (dict_ptr == 0) {
        fprintf(stderr, "Failed to open dictionary\n");
        dlclose(handle);
        return 1;
    }

    /* Read words file */
    FILE* wf = fopen(words_path, "r");
    if (!wf) {
        fprintf(stderr, "Cannot open words file: %s\n", words_path);
        dlclose(handle);
        return 1;
    }

    char line[1024];
    int checked = 0, known = 0;

    /* Skip CSV header if present */
    if (fgets(line, sizeof(line), wf)) {
        if (strncmp(line, "word", 4) == 0) {
            /* header, skip */
        } else {
            /* not header, rewind */
            rewind(wf);
        }
    }

    while (fgets(line, sizeof(line), wf)) {
        /* Strip newline and comma-separated weight */
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char* comma = strchr(line, ',');
        if (comma) *comma = '\0';
        char* tab = strchr(line, '\t');
        if (tab) *tab = '\0';

        if (strlen(line) < 2) continue;

        set_jni_string(line);
        jboolean result = check_fn(env, NULL, dict_ptr, (jstring)g_fake_string);

        /* Output: word\tweight (1 = known, 0 = unknown) */
        /* TODO: if we can hook the internal weight, output actual frequency */
        printf("%s\t%d\n", line, result ? 1 : 0);

        checked++;
        if (result) known++;

        if (checked % 10000 == 0) {
            fprintf(stderr, "Progress: %d checked, %d known\n", checked, known);
        }
    }

    fprintf(stderr, "Done: %d checked, %d known\n", checked, known);

    fclose(wf);

    /* Close dictionary */
    if (close_fn) {
        close_fn(env, NULL, dict_ptr);
    }

    dlclose(handle);
    return 0;
}
