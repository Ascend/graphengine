/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __CCE_RUNTIME_STARS_H
#define __CCE_RUNTIME_STARS_H

#include "base.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @ingroup rt_stars
 * @brief launch stars task.
 * used for send star sqe directly.
 * @param [in] taskSqe     stars task sqe
 * @param [in] sqeLen      stars task sqe length
 * @param [in] stream      associated stream
 * @return RT_ERROR_NONE for ok, others failed
 */
RTS_API rtError_t rtStarsTaskLaunch(const void *taskSqe, uint32_t sqeLen, rtStream_t stream);


/**
 * @ingroup rt_stars
 * @brief create cdq instance.
 * @param [in] batchNum     batch number
 * @param [in] batchSize    batch size
 * @param [in] queName      cdq name
 * @return RT_ERROR_NONE for ok, ACL_ERROR_RT_NO_CDQ_RESOURCE for no cdq resources
 */
RTS_API rtError_t rtCdqCreate(uint32_t batchNum, uint32_t batchSize, const char *queName);

/**
 * @ingroup rt_stars
 * @brief destroy cdq instance.
 * @param [in] queName      cdq name
 * @return RT_ERROR_NONE for ok, others failed
 */
RTS_API rtError_t rtCdqDestroy(const char *queName);

/**
 * @ingroup rt_stars
 * @brief get free batch in the queue.
 * @param [in] queName      cdq name
 * @param [in] timeout      batch size
 * @param [out] batchId     batch index
 * @return RT_ERROR_NONE for ok, ACL_ERROR_RT_WAIT_TIMEOUT for timeout
 */
RTS_API rtError_t rtCdqAllocBatch(const char *queName, int32_t timeout, uint32_t *batchId);

/**
 * @ingroup rt_stars
 * @brief launch a write_cdqm task on the stream.
 * When the task is executed, the data information will be inserted into the cdqe index position of the queue.
 * @param [in] queName      cdq name
 * @param [in] cdqeIndex    cdqe index
 * @param [in] data         cdqe infomation
 * @param [in] dataSize     data size
 * @param [in] stream       launch task on the stream
 * @return RT_ERROR_NONE for ok, others failed
 */
RTS_API rtError_t rtCdqEnQueue(const char *queName, uint32_t cdqeIndex, void *data, uint32_t dataSize,
    rtStream_t stream);

/**
 * @ingroup rt_stars
 * @brief launch a write_cdqm task on the stream.
 * When the task is executed, the data information will be inserted into the cdqe index position of the queue.
 * @param [in] queName      cdq name
 * @param [in] cdqeIndex    cdqe index
 * @param [in] data         cdqe infomation
 * @param [in] dataSize     data size
 * @param [in] stream       launch task on the stream
 * @return RT_ERROR_NONE for ok, others failed
 */
RTS_API rtError_t rtCdqEnQueuePtrMode(const char *queName, uint32_t cdqeIndex, const void *ptrAddr,
    rtStream_t stream);

#if defined(__cplusplus)

}
#endif
#endif // __CCE_RUNTIME_STARS_H
