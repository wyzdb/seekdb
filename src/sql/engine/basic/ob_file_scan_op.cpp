/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/basic/ob_file_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "common/timezone/ob_time_convert.h"
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <algorithm>
#include <strings.h>

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObFileScanOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(opened_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(MY_SPEC.path_.ptr()) || MY_SPEC.path_.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(fp_ = fopen(MY_SPEC.path_.ptr(), "rb"))) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("open file failed", K(ret), K(MY_SPEC.path_), K(errno));
  } else {
    opened_ = true;
    eof_ = false;
    header_skipped_ = false;
    if (MY_SPEC.header_) {
      bool has_row = false;
      if (OB_FAIL(read_record(has_row))) {
      } else if (has_row) {
        header_skipped_ = true;
      }
    }
  }
  return ret;
}

int ObFileScanOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else if (NULL != fp_) {
    rewind(fp_);
  }
  eof_ = false;
  header_skipped_ = false;
  if (OB_SUCC(ret) && MY_SPEC.header_) {
    bool has_row = false;
    if (OB_FAIL(read_record(has_row))) {
    } else if (has_row) {
      header_skipped_ = true;
    }
  }
  return ret;
}

int ObFileScanOp::read_record(bool &has_row)
{
  int ret = OB_SUCCESS;
  has_row = false;
  record_.clear();
  quoted_.clear();
  if (NULL == fp_ || eof_) {
    has_row = false;
  } else {
    std::string field;
    bool in_quotes = false;
    bool field_started = false;
    bool field_quoted = false;
    int c = 0;
    bool got_any = false;
    while (OB_SUCC(ret) && EOF != (c = fgetc(fp_))) {
      got_any = true;
      if (in_quotes) {
        if ('"' == c) {
          int n = fgetc(fp_);
          if ('"' == n) {
            field.push_back('"');
          } else {
            in_quotes = false;
            if (EOF != n) ungetc(n, fp_);
          }
        } else {
          field.push_back((char)c);
        }
        field_started = true;
      } else {
        if ('"' == c && !field_started && field.empty()) {
          in_quotes = true;
          field_started = true;
          field_quoted = true;
        } else if (c == MY_SPEC.delimiter_) {
          record_.push_back(field);
          quoted_.push_back(field_quoted);
          field.clear();
          field_started = false;
          field_quoted = false;
        } else if ('\n' == c || '\r' == c) {
          if ('\r' == c) {
            int n = fgetc(fp_);
            if ('\n' != n && EOF != n) ungetc(n, fp_);
          }
          record_.push_back(field);
          quoted_.push_back(field_quoted);
          has_row = true;
          break;
        } else {
          field.push_back((char)c);
          field_started = true;
        }
      }
    }
    if (OB_SUCC(ret) && !has_row && (got_any || !field.empty())) {
      record_.push_back(field);
      quoted_.push_back(field_quoted);
      has_row = true;
    }
    if (OB_SUCC(ret) && !has_row) {
      eof_ = true;
    }
  }
  return ret;
}

int ObFileScanOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  bool has_row = false;
  if (OB_FAIL(try_check_status())) {
  } else if (!opened_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(read_record(has_row))) {
  } else if (!has_row) {
    ret = OB_ITER_END;
  } else {
    clear_evaluated_flag();
    ObTimeConvertCtx cvrt_ctx(ctx_.get_my_session()->get_timezone_info(), false);
    ObDateSqlMode date_sql_mode;
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.output_.count(); ++i) {
      ObExpr *expr = MY_SPEC.output_.at(i);
      int32_t col_idx = i < MY_SPEC.output_cols_.count() ? MY_SPEC.output_cols_.at(i) : -1;
      ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
      if (col_idx < 0 || col_idx >= (int32_t)MY_SPEC.cols_.count()) {
        datum.set_null();
      } else {
        const ObFileColumn &col = MY_SPEC.cols_.at(col_idx);
        if (col_idx >= (int32_t)record_.size()) {
          datum.set_null();
        } else {
          const std::string &val = record_[col_idx];
          bool is_null = false;
          bool q = col_idx < (int32_t)quoted_.size() ? quoted_[col_idx] : false;
          if (!q && (val.empty() || (!MY_SPEC.nullstr_.empty() &&
              val.size() == (size_t)MY_SPEC.nullstr_.length() &&
              0 == strncasecmp(val.c_str(), MY_SPEC.nullstr_.ptr(), val.size())))) {
            is_null = true;
          } else if (!q && val.empty()) {
            is_null = true;
          }
          if (is_null) {
            datum.set_null();
          } else {
            switch (col.type_) {
              case ObFileColType::FILE_COL_BOOLEAN:
                datum.set_bool(0 == strncasecmp(val.c_str(), "true", 4));
                break;
              case ObFileColType::FILE_COL_BIGINT:
                datum.set_int(strtoll(val.c_str(), NULL, 10));
                break;
              case ObFileColType::FILE_COL_DOUBLE:
                datum.set_double(strtod(val.c_str(), NULL));
                break;
              case ObFileColType::FILE_COL_DATE: {
                int32_t date = 0;
                if (OB_FAIL(ObTimeConverter::str_to_date(ObString((int32_t)val.size(), val.c_str()), date, date_sql_mode))) {
                  datum.set_null();
                } else {
                  datum.set_date(date);
                }
                break;
              }
              case ObFileColType::FILE_COL_DATETIME: {
                std::string tmp = val;
                for (size_t k = 0; k < tmp.size(); ++k) {
                  if ('T' == tmp[k]) tmp[k] = ' ';
                }
                int64_t dt = 0;
                ObScale scale = 0;
                if (OB_FAIL(ObTimeConverter::str_to_datetime(ObString((int32_t)tmp.size(), tmp.c_str()),
                                                             cvrt_ctx, dt, &scale, date_sql_mode))) {
                  datum.set_null();
                } else {
                  datum.set_datetime(dt);
                }
                break;
              }
              default:
                datum.set_string(val.data(), (uint32_t)val.size());
                break;
            }
          }
        }
      }
      expr->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFileScanOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (NULL != fp_) {
    fclose(fp_);
    fp_ = NULL;
  }
  opened_ = false;
  return ret;
}

void ObFileScanOp::destroy()
{
  if (NULL != fp_) {
    fclose(fp_);
    fp_ = NULL;
  }
  ObOperator::destroy();
}

} // namespace sql
} // namespace oceanbase
