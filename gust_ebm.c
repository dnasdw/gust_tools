/*
  gust_ebm - Ebm file processor for Gust (Koei/Tecmo) PC games
  Copyright © 2019 VitaSmith

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "util.h"
#include "parson.h"

#pragma pack(push, 1)
typedef struct {
    uint32_t type;          // always seems to be set to 2
    uint32_t voice_id;      // id of the voice for the speaking character
    uint32_t unknown1;      // ???
    uint32_t name_id;       // id of the name to use for the speaking character
    uint32_t extra_id;      // seems to be -1 for system messages
    uint32_t expr_id;       // serious = 0x09, surprise = 0x0a, happy = 0x0c, etc.
    uint32_t msg_id;        // sequential id of the message
    uint32_t unknown2;      // ???
    uint32_t msg_length;    // length of msg_string
    char     msg_string[0]; // text message to display
    // Note: you can use "<CR>" within a message to force a line break as well as
    // display small images (e.g. controller buttons) with things like "<IM10>", "<IM00>"
} ebm_message;
#pragma pack(pop)

int main(int argc, char** argv)
{
    int r = -1;
    uint8_t* buf = NULL;
    FILE* file = NULL;
    JSON_Value* json = NULL;

    if (argc != 2) {
        printf("%s %s (c) 2019 VitaSmith\n\n"
            "Usage: %s <file>\n\n"
            "Convert a .ebm file to or from an editable JSON file.\n\n",
            appname(argv[0]), GUST_TOOLS_VERSION_STR, appname(argv[0]));
        return 0;
    }

    if (strstr(argv[argc - 1], ".json") != NULL) {
        json = json_parse_file_with_comments(argv[argc - 1]);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", argv[argc - 1]);
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        printf("Creating '%s' from JSON...\n", filename);
        create_backup(filename);
        file = fopen(filename, "wb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Cannot create file '%s'\n", filename);
            goto out;
        }
        uint32_t nb_messages = (uint32_t)json_object_get_number(json_object(json), "nb_messages");
        if (fwrite(&nb_messages, sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write number of messages\n");
            goto out;
        }
        JSON_Array* json_messages = json_object_get_array(json_object(json), "messages");
        if (json_array_get_count(json_messages) != nb_messages) {
            fprintf(stderr, "ERROR: Number of messages doesn't match the array size\n");
            goto out;
        }
        for (uint32_t i = 0; i < nb_messages; i++) {
            JSON_Object* json_message = json_array_get_object(json_messages, i);
            ebm_message message = { 0 };
            message.type = (uint32_t)json_object_get_number(json_message, "type");
            message.voice_id = (uint32_t)json_object_get_number(json_message, "voice_id");
            message.unknown1 = (uint32_t)json_object_get_number(json_message, "unknown1");
            message.name_id = (uint32_t)json_object_get_number(json_message, "name_id");
            message.extra_id = (uint32_t)json_object_get_number(json_message, "extra_id");
            message.expr_id = (uint32_t)json_object_get_number(json_message, "expr_id");
            message.msg_id = (uint32_t)json_object_get_number(json_message, "msg_id");
            message.unknown2 = (uint32_t)json_object_get_number(json_message, "unknown2");
            const char* msg_string = json_object_get_string(json_message, "msg_string");
            message.msg_length = (uint32_t)strlen(msg_string) + 1;
            if (fwrite(&message, sizeof(ebm_message), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write message entry\n");
                goto out;
            }
            if (fwrite(msg_string, 1, message.msg_length, file) != message.msg_length) {
                fprintf(stderr, "ERROR: Can't write message entry\n");
                goto out;
            }
        }
        r = 0;
    } else if (strstr(argv[argc - 1], ".ebm") != NULL) {
        printf("Converting '%s' to JSON...\n", basename(argv[argc - 1]));
        uint32_t buf_size = read_file(basename(argv[argc - 1]), &buf);
        if (buf_size == 0)
            goto out;
        uint32_t nb_messages = getle32(buf);
        if (buf_size < sizeof(uint32_t) + nb_messages * sizeof(ebm_message)) {
            fprintf(stderr, "ERROR: Invalid number of entries");
            goto out;
        }

        // Store the data we'll need to reconstruct the archibe to a JSON file
        json = json_value_init_object();
        json_object_set_string(json_object(json), "name", basename(argv[argc - 1]));
        json_object_set_number(json_object(json), "nb_messages", nb_messages);
        JSON_Value* json_messages = json_value_init_array();
        ebm_message* entry = (ebm_message*) &buf[sizeof(uint32_t)];
        assert(sizeof(ebm_message) == 36);
        for (uint32_t i = 0; i < nb_messages; i++) {
            JSON_Value* json_message = json_value_init_object();
            uint32_t str_length = entry->msg_length;
            json_object_set_number(json_object(json_message), "type", (double)entry->type);
            json_object_set_number(json_object(json_message), "voice_id", (double)entry->voice_id);
            if (entry->unknown1 != 0)
                json_object_set_number(json_object(json_message), "unknown1", (double)entry->unknown1);
            json_object_set_number(json_object(json_message), "name_id", (double)entry->name_id);
            if (entry->extra_id != 0)
                json_object_set_number(json_object(json_message), "extra_id", (double)entry->extra_id);
            json_object_set_number(json_object(json_message), "expr_id", (double)entry->expr_id);
            json_object_set_number(json_object(json_message), "msg_id", (double)entry->msg_id);
            if (entry->unknown2 != 0)
                json_object_set_number(json_object(json_message), "unknown2", (double)entry->unknown2);
            // Don't store msg_length since we'll reconstruct it
            json_object_set_string(json_object(json_message), "msg_string", entry->msg_string);
            json_array_append_value(json_array(json_messages), json_message);
            entry = &entry[1];
            entry = (ebm_message*) &((uint8_t*)entry)[str_length];
        }
        json_object_set_value(json_object(json), "messages", json_messages);
        json_serialize_to_file_pretty(json, change_extension(argv[argc - 1], ".json"));
        r = 0;
    } else {
        fprintf(stderr, "ERROR: You must specify a .ebm or .json file");
    }

out:
    json_value_free(json);
    free(buf);
    if (file != NULL)
        fclose(file);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

#ifdef _CRTDBG_MAP_ALLOC
    _CrtDumpMemoryLeaks();
#endif
    return r;
}
