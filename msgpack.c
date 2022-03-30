#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#include <janet.h>

// #include "mpack.h"

// NOTE: I don't use the mpack.h API for encoding, I simply encode everything myself

static uint64_t ensure_bigendian(uint64_t val) {
    union {
        int i;
        char c[sizeof(int)];
    } x;
    x.i = 1;
    if (x.c[0] == 1) {
        // little endian
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap64(val);
        #elif defined(_MSC_VER)
            return _byteswap_uint64(val);
        #else
        val = (val & 0x00ff00ff00ff00ffL) << 8 | (val >>> 8) & 0x00ff00ff00ff00ffL;
        return (val << 48) | ((val & 0xffff0000L) << 16) |
            ((val >> 16) & 0xffff0000L) | (val >>> 48);
        #endif
    } else {
        return val;
    }
}

enum msgpack_string_type {
    MSGPACK_STRING_STRING = 0,
    MSGPACK_BYTES_STRING = 1
};
struct enum_entry {
    const char *name;
    int value;
};
static const struct enum_entry MSGPACK_STRING_TYPE_ENUM[] = {
    {"string", MSGPACK_STRING_STRING},
    {"bytes", MSGPACK_BYTES_STRING},
    {NULL, 0}
};
static const struct enum_entry JANET_TYPE_ENUM[] = {
    {"number", JANET_NUMBER},
    {"nil", JANET_NIL},
    {"string", JANET_STRING},
    {"buffer", JANET_BUFFER},
    {"symbol", JANET_SYMBOL},
    {"keyword", JANET_KEYWORD},
    {"struct", JANET_STRUCT},
    {"table", JANET_TABLE},
    {NULL, 0}
};
/**
 * Utility to parse an "enum" with named constants specified by the table
 */
static int parse_named_enum(Janet value, const char *enum_name, const struct enum_entry *enum_table) {
    const uint8_t *data;
    switch (janet_type(value)) {
        case JANET_SYMBOL:
            data = janet_unwrap_symbol(value);
            break;
        case JANET_KEYWORD:
            data = janet_unwrap_keyword(value);
            break;
        default:
            janet_panicf("Expected a keyword or symbol, but got a %t", value);
            assert(false);
    }
    int32_t actual_len = janet_string_length(data);
    while (enum_table->name != NULL) {
        size_t expected_len = strlen(enum_table->name);
        if (expected_len == ((size_t) actual_len)) {
            if (memcmp(enum_table->name, data, expected_len) == 0) {
                return enum_table->value;
            }
        }
        enum_table += 1;
    }
    janet_panicf(
        "Expected a %s, but got %S",
        enum_name,
        data
    );
}

struct msgpack_encoder {
    JanetBuffer *buffer;
    enum msgpack_string_type string_type;
    enum msgpack_string_type buffer_type;
};

static void encode_msgpack_int(struct msgpack_encoder *encoder, int64_t value, bool actually_unsigned);
static inline void encode_int_without_tag(JanetBuffer *buffer, uint64_t target, uint8_t needed_bytes);
static inline void encode_int_tagged(JanetBuffer *buffer, uint64_t target, uint8_t needed_bytes, uint8_t tag_start) {
    uint8_t tag;
    switch (needed_bytes) {
        case 1:
            tag = tag_start;
            break;
        case 2:
            tag = tag_start + 1;
            break;
        case 4:
            tag = tag_start + 2;
            break;
        case 8:
            tag = tag_start + 3;
            break;
        default:
            assert(false);
    }
    janet_buffer_push_u8(buffer, tag);
    encode_int_without_tag(buffer, target, needed_bytes);
}
static void encode_msgpack_string(struct msgpack_encoder *encoder, const uint8_t *bytes, uint32_t len, enum msgpack_string_type string_type);
static void encode_msgpack_collection_length(struct msgpack_encoder *encoder, int32_t len, uint8_t inline_bitmask, int8_t tag_start) {
    assert((inline_bitmask & 15) == 0);
    assert(len >= 0);
    JanetBuffer *buffer = encoder->buffer;
    if (len <= 15) {
        janet_buffer_push_u8(buffer, inline_bitmask | (uint8_t) len);
    } else if (len <= 0xFFFF) {
        janet_buffer_push_u8(buffer, tag_start);
        encode_int_without_tag(buffer, (uint32_t) len, 2);
    } else {
        janet_buffer_push_u8(buffer, tag_start + 1);
        encode_int_without_tag(buffer, (uint32_t) len, 4);
    }
}
static const char *encode_msgpack(struct msgpack_encoder *encoder, Janet value, int depth) {
    if (depth > JANET_RECURSION_GUARD) return "recursed too deeply";
#define checked_call(call) do { \
        const char *sub_err = call; \
        if (sub_err != NULL) return sub_err; \
    } while (false)
    switch (janet_type(value)) {
        case JANET_NIL: {
            janet_buffer_push_u8(encoder->buffer, 0xC0);
            break;
        }
        case JANET_BOOLEAN:
            if (janet_unwrap_boolean(value)) {
                janet_buffer_push_u8(encoder->buffer, 0xC3);
            } else {
                janet_buffer_push_u8(encoder->buffer, 0xC2);
            }
            break;
        case JANET_NUMBER:
            if (janet_checkint(value)) {
                encode_msgpack_int(encoder, janet_unwrap_integer(value), false);
            } else {
                union bytesvalue {
                    double d;
                    uint64_t i;
                } bytes;
                // use union to safely reinterpret bits
                bytes.d = janet_unwrap_number(value);
                janet_buffer_push_u8(encoder->buffer, 0xCB);
                janet_buffer_push_u64(encoder->buffer, ensure_bigendian(bytes.i));
            }
            break;
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            const uint8_t *data;
            int32_t len;
            janet_bytes_view(value, &data, &len);
            // keyword & symbol are unconditionally strings
            encode_msgpack_string(encoder, data, len, MSGPACK_STRING_STRING);
            break;
        }
        case JANET_STRING:
        case JANET_BUFFER: {
            // string & buffer have configurable serialization types
            enum msgpack_string_type string_type = janet_type(value) == JANET_STRING ? encoder->string_type : encoder->buffer_type;
            const uint8_t *data;
            int32_t len;
            janet_bytes_view(value, &data, &len);
            encode_msgpack_string(encoder, data, len, string_type);
            break;
        }
        case JANET_ABSTRACT:
            #ifdef JANET_INT_TYPES
            switch (janet_is_int(value)) {
                case JANET_INT_S64:
                    encode_msgpack_int(encoder, janet_unwrap_s64(value), false);
                    return NULL;
                case JANET_INT_U64:
                    encode_msgpack_int(encoder, (int64_t) janet_unwrap_u64(value), /* actually unsigned */ true);
                    return NULL;
                default:
                    goto unknown_type;
            }
            #endif // JANET_INT_TYPES
            goto unknown_type;
        case JANET_TUPLE:
        case JANET_ARRAY: {
            const Janet *items;
            int32_t len;
            janet_indexed_view(value, &items, &len);
            encode_msgpack_collection_length(
                encoder,
                len,
                0x90,
                0xDC
            );
            for (int32_t i = 0; i < len; i++) {
                checked_call(encode_msgpack(encoder, items[i], depth + 1));
            }
            break;
        }
        case JANET_TABLE:
        case JANET_STRUCT: {
            const JanetKV *kvs;
            int32_t count, capacity;
            janet_dictionary_view(value, &kvs, &count, &capacity);
            encode_msgpack_collection_length(
                encoder,
                count,
                0x80,
                0xDE
            );
            for (int32_t i = 0; i < capacity; i++) {
                if (janet_checktype(kvs[i].key, JANET_NIL))  continue;
                checked_call(encode_msgpack(encoder, kvs[i].key, depth + 1));                
                checked_call(encode_msgpack(encoder, kvs[i].value, depth + 1));
            }
            break;
        }
        default:
            goto unknown_type;
    }
    return NULL;
unknown_type:
    // TODO: Some type info here would be nice
    return "type not supported";
}
union byteify {
    uint64_t val;
    char bytes[8];
};
static inline void encode_int_without_tag(JanetBuffer *buffer, uint64_t target, uint8_t needed_bytes) {
    janet_buffer_ensure(buffer, buffer->capacity, needed_bytes + 1);
    switch (needed_bytes) {
        case 1:
            janet_buffer_push_u8(buffer, (uint8_t) target);
            break;
        case 2:
            janet_buffer_push_u8(buffer, (uint8_t) (target >> 8));
            janet_buffer_push_u8(buffer, (uint8_t) (target & 0xFF));
            break;
        case 4:
            janet_buffer_push_u8(buffer, (uint8_t) (target >> 24));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 16) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 8) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) (target & 0xFF));
            break;
        case 8:
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 56) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 48) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 40) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 32) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 24) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 16) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) ((target >> 8) & 0xFF));
            janet_buffer_push_u8(buffer, (uint8_t) (target & 0xFF));
            break;
        default:
            assert(false);
    }
}
static void encode_msgpack_string(struct msgpack_encoder *encoder, const uint8_t *bytes, uint32_t len, enum msgpack_string_type desired_type) {
    JanetBuffer *buffer = encoder->buffer;
    if (len < 32 && desired_type == MSGPACK_STRING_STRING) {
        janet_buffer_push_u8(buffer, 0xA0 | len); 
        janet_buffer_push_bytes(buffer, bytes, len);
    } else {
        uint8_t needed_len_bytes;
        if (len <= 0xFF) {
            needed_len_bytes = 1;
        } else if (len <= 0xFFFF) {
            needed_len_bytes = 2;
        } else {
            assert(len <= 0xFFFFFFFF);
            needed_len_bytes = 4;
        }
        encode_int_tagged(buffer, len, needed_len_bytes, desired_type == MSGPACK_STRING_STRING ? 0xD9 : 0xC4);
        janet_buffer_push_bytes(buffer, bytes, len);
    }
}
static void encode_msgpack_int(struct msgpack_encoder *encoder, int64_t signed_value, bool actually_unsigned) {
    JanetBuffer *buffer = encoder->buffer;
    union byteify byteify = {.val=(uint64_t) signed_value};
    char bytes[8];
    memcpy(byteify.bytes, bytes, 8);
    if (signed_value >= 0 || actually_unsigned) {
        uint64_t value = (uint64_t) signed_value;
        if (value <= 127) {
            janet_buffer_push_u8(buffer, (uint8_t) value);
        } else {
            uint8_t needed_bytes;
            if (signed_value <= 0xFF) {
                needed_bytes = 1;
            } else if (signed_value <= 0xFFFF) {
                needed_bytes = 2;
            } else if (signed_value <= 0xFFFFFFFF) {
                needed_bytes = 4;
            } else {
                needed_bytes = 8;
            }
            encode_int_tagged(buffer, value, needed_bytes, 0xCC);
        }
    } else {
        assert(signed_value < 0);
        if (signed_value >= -32) {
            janet_buffer_push_u8(buffer, (uint8_t) 0xE0 | (signed_value + 32));
        } else {
            uint8_t needed_bytes;
            uint64_t value;
            if (signed_value >= INT8_MIN) {
                needed_bytes = 1;
                value = (uint8_t) ((int8_t) signed_value);
            } else if (signed_value >= INT16_MIN) {
                needed_bytes = 2;
                value = (uint16_t) ((int16_t) signed_value);
            } else if (signed_value >= ((int64_t) INT32_MIN)) {
                needed_bytes = 4;
                value = (uint32_t) ((int32_t) signed_value);
            } else {
                needed_bytes = 8;
                value = (uint64_t) ((int64_t) signed_value);
            }
            encode_int_tagged(buffer, value, needed_bytes, 0xD0);
        }
    }
}

static Janet janet_msgpack_encode(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 32);
    struct msgpack_encoder encoder = {
        .buffer = buffer,
        .string_type = MSGPACK_STRING_STRING,
        .buffer_type = MSGPACK_BYTES_STRING,
    };
    if (argc > 1) {
        const JanetKV *jstruct = NULL;
        switch (janet_type(argv[1])) {
            case JANET_SYMBOL:
            case JANET_KEYWORD:
                encoder.string_type = (enum msgpack_string_type) parse_named_enum(
                    argv[1], "msgpack string type ('string or 'bytes)",
                    MSGPACK_STRING_TYPE_ENUM
                );
                encoder.buffer_type = encoder.string_type;
                break;
            case JANET_TABLE:
                jstruct = janet_table_to_struct(janet_unwrap_table(argv[1]));
            case JANET_STRUCT: {
                if (janet_type(argv[1]) == JANET_STRUCT) {
                    // Guard against the fallthrough ;)
                    assert(jstruct == NULL);
                    jstruct = janet_unwrap_struct(argv[1]);
                }
                assert(jstruct != NULL);
                int32_t capacity = janet_struct_capacity(jstruct);
                for (int32_t i = 0; i < capacity; i++) {
                    JanetKV kv = jstruct[i];
                    if (janet_checktype(kv.key, JANET_NIL)) continue;
                    JanetType type_key = (JanetType) parse_named_enum(
                        kv.key, "Janet type name",
                        JANET_TYPE_ENUM
                    );
                    enum msgpack_string_type type_value = (enum msgpack_string_type) parse_named_enum(
                        kv.value, "msgpack string type",
                        MSGPACK_STRING_TYPE_ENUM
                    );
                    switch (type_key) {
                        case JANET_STRING:
                            encoder.string_type = type_value;
                            break;
                        case JANET_BUFFER:
                            encoder.buffer_type = type_value;
                            break;
                        default:
                            janet_panicf("Expected either 'string or 'buffer, but got %T", type_key);
                    }
                }
                break;
            }
            default:
                janet_panicf("Expected either a keyword, symbol, table or struct, but got %t", argv[1]);
                break;
        }

    }
    const char *err_msg = encode_msgpack(&encoder, argv[0], 0);
    if (err_msg != NULL) janet_panicf("encode error: %s", err_msg);
    return janet_wrap_buffer(buffer);
}

/****************/
/* Module Entry */
/****************/

static const JanetReg cfuns[] = {
    {"encode", janet_msgpack_encode,
        "(msgpack/encode x &opt encoded-string-type buf)\n\n"
        "Encodes a janet value into msgpack: https://msgpack.org/"
        "string-type specifies the msgpack type to use for Janet strings/buffers."
        "This may be either 'string or 'bytes, or a table mapping Janet types -> encoded types"
        "For example, {:buffer 'bytes :string 'bytes}"
        "If buf is provided, the formated mspack is append to buf instead of a new buffer. "
        "Returns the modifed buffer."
    },
    /*
    {"decode", json_decode,
        "(json/decode json-source &opt keywords nils)\n\n"
        "Returns a janet object after parsing JSON. If keywords is truthy, string "
        "keys will be converted to keywords. If nils is truthy, null will become nil instead "
        "of the keyword :null."
    },*/
    {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns(env, "msgpack", cfuns);
}
