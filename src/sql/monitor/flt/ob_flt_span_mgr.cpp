// Copyright 2010-2016 Alibaba Inc. All Rights Reserved.
// Author:
//   juehui.lgy@alibaba-inc.com
// Normalizer:
//   juehui.lgy@alibaba-inc.com
// this file defines implementation of full link trace span manager


#define USING_LOG_PREFIX SERVER

#include "sql/monitor/flt/ob_flt_span_mgr.h"
#include "share/ob_define.h"
#include "lib/time/ob_time_utility.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/stat/ob_session_stat.h"
#include "lib/alloc/alloc_func.h"
#include "lib/thread/thread_mgr.h"
#include "lib/rc/ob_rc.h"
#include "share/rc/ob_context.h"
#include "observer/ob_server.h"
#include "sql/session/ob_basic_session_info.h"
#include "lib/trace/ob_trace.h"

namespace oceanbase
{
using namespace oceanbase::share::schema;
namespace sql
{
  ObFLTSpanMgr* get_flt_span_manager() {
    return MTL(ObFLTSpanMgr*);
  }

  int ObFLTSpanMgr::init(uint64_t tenant_id, const int64_t max_mem_size, const int64_t queue_size)
  {
    int ret = OB_SUCCESS;
    if (inited_) {
      ret = OB_INIT_TWICE;
    } else if (OB_FAIL(queue_.init("SqlFltSpanRec", queue_size, tenant_id))) {
      SERVER_LOG(WARN, "Failed to init ObMySQLRequestQueue", K(ret));
    } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::ReqMemEvict, tg_id_))) {
      SERVER_LOG(WARN, "create failed", K(ret));
    } else if (OB_FAIL(TG_START(tg_id_))) {
      SERVER_LOG(WARN, "init timer fail", K(ret));
    } else if (OB_FAIL(allocator_.init(FLT_SPAN_PAGE_SIZE,
                                      "SqlFltSpanRec",
                                       tenant_id,
                                       INT64_MAX))) {
      SERVER_LOG(WARN, "failed to init allocator", K(ret));
    } else {
      mem_limit_ = max_mem_size;
      tenant_id_ = tenant_id;
      inited_ = true;
      destroyed_ = false;
    }
    if ((OB_FAIL(ret)) && (!inited_)) {
      destroy();
    }
    return ret;
  }

  void ObFLTSpanMgr::destroy()
  {
    if (!destroyed_) {
      TG_DESTROY(tg_id_);
      clear_queue();
      queue_.destroy();
      allocator_.destroy();
      inited_ = false;
      destroyed_ = true;
    }
  }

  int ObFLTSpanMgr::mtl_init(ObFLTSpanMgr* &span_mgr)
  {
    int ret = OB_SUCCESS;
    span_mgr = OB_NEW(ObFLTSpanMgr, "SqlFltSpanRec");
    if (nullptr == span_mgr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for ObMySQLRequestManager", K(ret));
    } else {
      uint64_t tenant_id = lib::current_resource_owner_id();
      int64_t mem_limit = lib::get_tenant_memory_limit(tenant_id);
      int64_t queue_size = MAX_QUEUE_SIZE;
      if (OB_FAIL(span_mgr->init(tenant_id, mem_limit, queue_size))) {
        LOG_WARN("failed to init request manager", K(ret));
      } else {
        // do nothing
      }
    }
    if (OB_FAIL(ret) && span_mgr != nullptr) {
      // cleanup
      common::ob_delete(span_mgr);
      span_mgr = nullptr;
    }
    return ret;
  }

  void ObFLTSpanMgr::mtl_destroy(ObFLTSpanMgr* &span_mgr)
  {
    common::ob_delete(span_mgr);
    span_mgr = nullptr;
  }


  int ObFLTSpanMgr::record_span(ObFLTSpanData &span_data)
  {
    int ret = OB_SUCCESS;
    if (!inited_) {
      ret = OB_NOT_INIT;
    } else {
      ObFLTSpanRec *rec = NULL;
      char *buf = NULL;
      //alloc mem from allocator
      int64_t pos = sizeof(ObFLTSpanRec);
      int64_t total_sz = sizeof(ObFLTSpanRec)
                        + min(span_data.trace_id_.length(), OB_MAX_SPAN_LENGTH)
                        + min(span_data.span_id_.length(), OB_MAX_SPAN_LENGTH)
                        + min(span_data.parent_span_id_.length(), OB_MAX_SPAN_LENGTH)
                        + min(span_data.span_name_.length(), OB_MAX_SPAN_LENGTH)
                        + min(span_data.tags_.length(), OB_MAX_SPAN_TAG_LENGTH)
                        + min(span_data.logs_.length(), OB_MAX_SPAN_TAG_LENGTH);
      if (NULL == (buf = (char*)allocator_.alloc(total_sz))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        if (REACH_TIME_INTERVAL(100 * 1000)) {
          SERVER_LOG(WARN, "record concurrent fifoallocator alloc mem failed", K(sizeof(ObFLTSpanRec)), K(ret));
        }
      } else {
        rec = new(buf)ObFLTSpanRec();
        rec->allocator_ = &allocator_;
        rec->data_ = span_data;
        //deep copy trace id
        if (!span_data.trace_id_.empty()) {
          const int len = min(span_data.trace_id_.length(), OB_MAX_SPAN_LENGTH);
          MEMCPY(buf + pos, span_data.trace_id_.ptr(), len);
          rec->data_.trace_id_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //deep copy span id
        if (!span_data.span_id_.empty()) {
          const int len = min(span_data.span_id_.length(), OB_MAX_SPAN_LENGTH);
          MEMCPY(buf + pos, span_data.span_id_.ptr(), len);
          rec->data_.span_id_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //deep copy parent span id
        if (!span_data.parent_span_id_.empty()) {
          const int len = min(span_data.parent_span_id_.length(), OB_MAX_SPAN_LENGTH);
          MEMCPY(buf + pos, span_data.parent_span_id_.ptr(), len);
          rec->data_.parent_span_id_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //deep copy span name
        if (!span_data.span_name_.empty()) {
          const int len = min(span_data.span_name_.length(), OB_MAX_SPAN_LENGTH);
          MEMCPY(buf + pos, span_data.span_name_.ptr(), len);
          rec->data_.span_name_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //deep copy tags
        if (!span_data.tags_.empty()) {
          const int len = min(span_data.tags_.length(), OB_MAX_SPAN_TAG_LENGTH);
          MEMCPY(buf + pos, span_data.tags_.ptr(), len);
          rec->data_.tags_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //deep copy logs_
        if (!span_data.logs_.empty()) {
          const int len = min(span_data.logs_.length(), OB_MAX_SPAN_TAG_LENGTH);
          MEMCPY(buf + pos, span_data.logs_.ptr(), len);
          rec->data_.logs_.assign_ptr(buf + pos, len);
          pos += len;
        }
        //push into queue
        if (OB_SUCC(ret)) {
          int64_t req_id = 0;
          if (OB_FAIL(queue_.push(rec, req_id))) {
            //sql audit槽位已满时会push失败, 依赖后台线程进行淘汰获得可用槽位
            if (REACH_TIME_INTERVAL(2 * 1000 * 1000)) {
              SERVER_LOG(WARN, "push into queue failed", K(ret));
            }
            if (ret == OB_ENTRY_NOT_EXIST) {
              release_old(RELEASE_QUEUE_SIZE);
            }
            allocator_.free(rec);
            rec = NULL;
          } else {
            rec->data_.req_id_ = req_id;
          }
        } else {
          allocator_.free(rec);
          rec = NULL;
        }
      }
    } // end
    return ret;
  }

  // evict old span and release memory
  int ObFLTSpanMgr::release_old(int64_t limit)
  {
    void* span = NULL;
    int64_t count = 0;
    while(count++ < limit && NULL != (span = queue_.pop())) {
      free(span);
    }
    return common::OB_SUCCESS;
  }
} // end of namespace obmysql
} // end of namespace oceanbase
