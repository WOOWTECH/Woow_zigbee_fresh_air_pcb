/* 極簡測試框架（主機端 gcc 編譯；不依賴 ESP-IDF） */
#pragma once
#include <stdio.h>
#include <string.h>
extern int ul_fail, ul_run;
#define TEST(name) static void name(void)
#define RUN(name) do { int f0 = ul_fail; ul_run++; name(); printf("%s %s\n", ul_fail == f0 ? "PASS" : "FAIL", #name); } while (0)
#define CHECK(c) do { if (!(c)) { ul_fail++; printf("  %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a != _b) { ul_fail++; \
    printf("  %s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
