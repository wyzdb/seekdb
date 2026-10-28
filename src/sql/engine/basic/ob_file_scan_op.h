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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_OP_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/resolver/dml/ob_dml_stmt.h"
#include <vector>
#include <string>
#include <stdio.h>

namespace oceanbase
{
namespace sql
{
class ObFileScanSpec : public ObOpSpec
{
public:
  ObFileScanSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      path_(),
      format_(0),
      delimiter_(','),
      quote_('"'),
      escape_('"'),
      header_(false),
      nullstr_(),
      cols_(alloc),
      output_cols_(alloc) {}
  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(format), K_(delimiter));
  common::ObString path_;
  int8_t format_;
  char delimiter_;
  char quote_;
  char escape_;
  bool header_;
  common::ObString nullstr_;
  common::ObFixedArray<ObFileColumn, common::ObIAllocator> cols_;
  common::ObFixedArray<int32_t, common::ObIAllocator> output_cols_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFileScanSpec);
};

class ObFileScanOp : public ObOperator
{
public:
  ObFileScanOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      fp_(NULL),
      opened_(false),
      eof_(false),
      header_skipped_(false),
      record_(),
      quoted_() {}
  virtual ~ObFileScanOp() {}
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  int read_record(bool &has_row);
private:
  FILE *fp_;
  bool opened_;
  bool eof_;
  bool header_skipped_;
  std::vector<std::string> record_;
  std::vector<bool> quoted_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFileScanOp);
};

} // namespace sql
} // namespace oceanbase
#endif
