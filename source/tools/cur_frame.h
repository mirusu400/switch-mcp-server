#pragma once

#include <stdbool.h>
#include "../third_party/cJSON.h"
#include <switch/types.h> // for u8, u32


int list_cur_frame(cJSON *tools);

int call_cur_frame(cJSON *contents);
int list_cur_frame_resource(cJSON *resources);
bool match_cur_frame_resource(const char *uri);
int read_cur_frame_resource(cJSON *contents, const char *uri);

Result cur_frameInitialize();
void cur_frameFinalize();
