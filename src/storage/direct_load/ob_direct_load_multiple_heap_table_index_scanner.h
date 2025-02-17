// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <suzhi.yt@oceanbase.com>

#pragma once

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block_reader.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTableDataDesc;
class ObDirectLoadMultipleHeapTable;

class ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  virtual ~ObIDirectLoadMultipleHeapTableIndexScanner() = default;
  virtual int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) = 0;
  TO_STRING_EMPTY();
};

class ObDirectLoadMultipleHeapTableIndexWholeScanner
  : public ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  ObDirectLoadMultipleHeapTableIndexWholeScanner();
  virtual ~ObDirectLoadMultipleHeapTableIndexWholeScanner();
  int init(ObDirectLoadMultipleHeapTable *heap_table,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int init(const ObDirectLoadTmpFileHandle &index_file_handle,
           int64_t file_size,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) override;
private:
  ObDirectLoadMultipleHeapTableIndexBlockReader index_block_reader_;
  bool is_inited_;
};

class ObDirectLoadMultipleHeapTableTabletIndexWholeScanner
  : public ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  ObDirectLoadMultipleHeapTableTabletIndexWholeScanner();
  virtual ~ObDirectLoadMultipleHeapTableTabletIndexWholeScanner();
  int init(ObDirectLoadMultipleHeapTable *heap_table,
           const common::ObTabletID &tablet_id,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) override;
  TO_STRING_KV(KP_(heap_table), K_(tablet_id));
private:
  int locate_left_border();
private:
  ObDirectLoadMultipleHeapTable *heap_table_;
  common::ObTabletID tablet_id_;
  ObDirectLoadMultipleHeapTableIndexBlockReader index_block_reader_;
  bool is_iter_end_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
