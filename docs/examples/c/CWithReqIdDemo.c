/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// TAOS standard API example. The same syntax as MySQL, but only a subset
// to compile: gcc -o CWithReqIdDemo CWithReqIdDemo.c -ltaos

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "taos.h"


static int DemoWithReqId() {
// ANCHOR: with_reqid
  int         ret_code  = -1;
  const char *ip        = "localhost";
  const char *user      = "root";
  const char *password  = "taosdata";

  // connect
  TAOS *taos = taos_connect(ip, user, password, NULL, 0);
  if (taos == NULL) {
    printf("failed to connect to server, reason: %s\n", taos_errstr(NULL));
    goto end;
  }

  // create database
  TAOS_RES *result = taos_query_with_reqid(taos, "CREATE DATABASE IF NOT EXISTS power", 1L);
  int code = taos_errno(result);
  if (code != 0) {
    printf("failed to create database, reason: %s\n", taos_errstr(result));
    taos_free_result(result);
    goto end;
  }
  taos_free_result(result);
  printf("success to create database\n");

  // use database
  result = taos_query_with_reqid(taos, "USE power", 2L);
  taos_free_result(result);

  // query data
  const char* sql = "SELECT * FROM power.meters";
  result = taos_query_with_reqid(taos, sql, 3L);
  code = taos_errno(result);
  if (code != 0) {
    printf("failed to select, reason: %s\n", taos_errstr(result));
    goto end;
  }

  TAOS_ROW    row         = NULL;
  int         rows        = 0;
  int         num_fields  = taos_field_count(result);
  TAOS_FIELD *fields      = taos_fetch_fields(result);

  printf("fields: %d\n", num_fields);
  printf("sql: %s, result:\n", sql);
  
  // fetch the records row by row
  while ((row = taos_fetch_row(result))) {
    char temp[1024] = {0};
    rows++;
    taos_print_row(temp, row, fields, num_fields);
    printf("%s\n", temp);
  }
  printf("total rows: %d\n", rows);
  taos_free_result(result);
  ret_code = 0;

end:
  // close & clean
  taos_close(taos);
  taos_cleanup();
  return ret_code;
// ANCHOR_END: with_reqid
}

int main(int argc, char *argv[]) {
  return DemoWithReqId();
}