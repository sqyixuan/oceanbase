// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <suzhi.yt@oceanbase.com>

#pragma once

#include "storage/blocksstable/ob_datum_rowkey.h"
#include "storage/direct_load/ob_direct_load_external_row.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatumUtils;
} // namespace blocksstable
namespace storage
{

struct ObDirectLoadSSTableScanMergeLoserTreeItem
{
public:
  ObDirectLoadSSTableScanMergeLoserTreeItem()
    : external_row_(nullptr), iter_idx_(0), equal_with_next_(false)
  {
  }
  ~ObDirectLoadSSTableScanMergeLoserTreeItem() = default;
  void reset()
  {
    external_row_ = nullptr;
    iter_idx_ = 0;
    equal_with_next_ = false;
  }
  TO_STRING_KV(KPC_(external_row), K_(iter_idx), K_(equal_with_next));
public:
  const ObDirectLoadExternalRow *external_row_;
  int64_t iter_idx_;
  bool equal_with_next_; // for simple row merger
};

class ObDirectLoadSSTableScanMergeLoserTreeCompare
{
public:
  ObDirectLoadSSTableScanMergeLoserTreeCompare();
  ~ObDirectLoadSSTableScanMergeLoserTreeCompare();
  int init(const blocksstable::ObStorageDatumUtils *datum_utils);
  int cmp(const ObDirectLoadSSTableScanMergeLoserTreeItem &lhs,
          const ObDirectLoadSSTableScanMergeLoserTreeItem &rhs,
          int64_t &cmp_ret);
public:
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  blocksstable::ObDatumRowkey lhs_rowkey_;
  blocksstable::ObDatumRowkey rhs_rowkey_;
};

} // namespace storage
} // namespace oceanbase
